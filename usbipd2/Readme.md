# Clean or regenerate the build configuration
cmake -B build -G "Visual Studio 17 2022" -A x64

# Compile the newly named target layout configuration
cmake --build build --config Debug

go solution file sln properties -> Linker -> ALL option -> UAC Execution Level
requireAdministrator (/level='requireAdministrator')


host server side

usbipd.sys	✅ Yes	✅ Yes	GPL-2.0 / Open Source	Free to modify & distribute (GPL rules apply)
VBoxUSB.sys	✅ Yes	✅ Yes	GPL-2.0 (Oracle / VirtualBox)	Free, but forces copyleft (GPL) on your codebase
WinUSB.sys	❌ No	❌ No	Proprietary (Microsot)


we are using WinUSB.sys which is available in all the windows


+-----------------------------------------------------------------------+
| SERVER HOST (Physical Machine)                                        |
|                                                                       |
|  [ Physical USB Device ]                                             |
|          │                                                            |
|          ▼                                                            |
|    WinUSB.sys  <─── (Kernel Driver: Interfaces with physical hardware) |
|          │                                                            |
|          ▼                                                            |
|    usbipd.exe  <─── (User App: Reads pipes via WinUsb_ReadPipe,        |
|          │           sends packets over TCP)                          |
+----------│------------------------------------------------------------+
           │ Network TCP/IP (Port 3240)
+----------│------------------------------------------------------------+
| CLIENT MACHINE (Virtual or Remote)                                    |
|          │                                                            |
|          ▼                                                            |
|    vhci_hcd / usbip_vhci.sys  <── (Kernel Driver: Emulates a USB Hub) |
|          │                                                            |
|          ▼                                                            |
|  [ Virtual USB Device created for Client OS ]                        |
+-----------------------------------------------------------------------+