# Clean or regenerate the build configuration
cmake -B build -G "Visual Studio 17 2022" -A x64

# Compile the newly named target layout configuration
cmake --build build --config Release

