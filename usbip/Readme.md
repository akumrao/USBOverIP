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
usbip_client.exe list -r 192.168.1.19
Attach a remote device:

DOS
usbip_client.exe attach -r 192.168.1.50 -b 1-1
Detach a virtual port:

DOS
usbip_client.exe detach -p 1






Windows Client (Attaching Remote USB)	usbip_vhci.sys	❌ No (Must be installed manually)
Linux / WSL2 Client (Attaching Remote USB)	vhci-hcd	✅ Yes (Built into standard Linux kernels)



.\usbip.exe list -r 192.168.0.19

.\usbip.exe attach -r 192.168.0.19 -b 1-5

.\usbip.exe detach -p 0


.\usbip.exe list -r 192.168.0.19 -t 3240
.\usbip.exe attach -r 192.168.0.19 -t 3240 -b 1-5

.\usbip.exe detach -r 192.168.0.19 -t 3240 -b 1-5


Start-Service usbipd

E:\workspace\USBOverIP\usbipdTest

pnputil /add-driver "E:\workspace\USBOverIP\usbipdTest\VBoxUSB.inf" /install