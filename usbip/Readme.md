# Clean or regenerate the build configuration
cmake -B build -G "Visual Studio 17 2022" -A x64

# Compile the newly named target layout configuration
cmake --build build --config Debug

go solution file sln properties -> Linker -> ALL option -> UAC Execution Level
requireAdministrator (/level='requireAdministrator')


DOS
cmake -B build -G "Visual Studio 17 2022" -A x64
Compile the application in Release mode:

DOS
cmake --build build --config Debug



List remote devices:

DOS
usbip_client.exe list -r 192.168.1.50
Attach a remote device:

DOS
usbip_client.exe attach -r 192.168.1.50 -b 1-1
Detach a virtual port:

DOS
usbip_client.exe detach -p 1