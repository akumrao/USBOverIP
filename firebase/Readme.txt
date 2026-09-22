conan remove firebase-cpp-sdk/13.12.0 --confirm


conan create .





conan install . --output-folder=build --build=missing -s build_type=Release

cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=build/Release/generators/conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Release


cmake --build build




#It is  possible to build your main application or source code in Debug mode while consuming its Conan dependencies in Release mode

conan install . -s '&:build_type=Debug' -s build_type=Release --output-folder=build

cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Debug
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=build/Debug/generators/conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Debug

cmake --build build








conan build . --output-folder=build

cp ../google-services.json .


windows 

conan install . --output-folder=build --build=missing -s build_type=Release
cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=conan_toolchain.cmake
cmake --build . --config Release
xcopy ..\google-services.json .\Release\ /Y







#GitHub Pages (Free Permanent Hosting) 
 
Step 1: Create a new repository on GitHub and upload both index.html and app.js. 

Step 2: Go to your repository Settings -> Pages (under Code and automation).

Step 3: Under Build and deployment > Branch, select main (or master) and / (root)

Step 4: Click Save. Your site will be live at 

https://<username>.github.io/<repo-name>/ in ~1 minute.

Crucial Step for Firestore Rules:
Because your signaling server uses Firebase Firestore, ensure your Firestore security rules allow read/write access. Go to the Firebase Console -> Firestore Database -> Rules and set:  JavaScriptrules_version = '2';
service cloud.firestore {
  match /databases/{database}/documents {
    match /rooms/{roomId}/{document=**} {
      allow read, write: if true;
    }
  }
}
