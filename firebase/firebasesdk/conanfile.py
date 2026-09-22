from conan import ConanFile
from conan.tools.files import copy
import os

class FirebaseCppSdkConan(ConanFile):
    name = "firebase-cpp-sdk"
    version = "13.12.0"
    settings = "os", "compiler", "build_type", "arch"
    description = "Conan package wrapper for prebuilt Firebase C++ SDK binary modules"

    # Export source files relative to this folder layout
    exports_sources = "third_party/firebase_cpp_sdk/**"

    def package(self):
        src_dir = os.path.join(self.source_folder, "third_party", "firebase_cpp_sdk")
        
        if not os.path.exists(os.path.join(src_dir, "include")):
            raise Exception(f"Fatal: Could not find 'include/' inside {src_dir}.")

        # 1. Export headers cleanly
        copy(self, "*.h", 
             src=os.path.join(src_dir, "include"), 
             dst=os.path.join(self.package_folder, "include"))
        
        # 2. Extract static libraries matching the active compiler target layout
        if self.settings.os == "Windows":
            lib_path = os.path.join(src_dir, "libs", "windows", "amd64", "vs2019", "MD")
            copy(self, "*.lib", src=lib_path, dst=os.path.join(self.package_folder, "lib"))
        else:
            # Fixed to map the exact cxx11 subpath directory present on your system
            lib_path = os.path.join(src_dir, "libs", "linux", "x86_64", "cxx11")
            print(f"--> Packaging Linux library components from path: {lib_path}")
            copy(self, "*.a", src=lib_path, dst=os.path.join(self.package_folder, "lib"))

    def package_info(self):
        # Expose targets to Conan's CMakeDeps generator
        self.cpp_info.libs = ["firebase_firestore", "firebase_auth", "firebase_app"]
