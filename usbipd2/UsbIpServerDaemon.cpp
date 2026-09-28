#include "UsbIpServerDaemon.hpp"

// Utility to verify Administrator execution at startup
static bool IsRunningAsAdmin() {
    BOOL isAdmin = FALSE;
    PSID adminGroup = NULL;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(NULL, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin == TRUE;
}

void UsbIpServerDaemon::SetDeviceAttached(const std::string& busId, bool attached) {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    if (attached) {
        m_attachedDevices.insert(busId);
    } else {
        m_attachedDevices.erase(busId);
    }
}

bool UsbIpServerDaemon::IsDeviceAttached(const std::string& busId) {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    return m_attachedDevices.find(busId) != m_attachedDevices.end();
}

bool UsbIpServerDaemon::IsDeviceBound(const std::string& busId) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, REG_BOUND_PATH, 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return false;
    }
    std::wstring wBusId(busId.begin(), busId.end());
    DWORD val = 0;
    DWORD size = sizeof(val);
    LONG result = RegQueryValueExW(hKey, wBusId.c_str(), NULL, NULL, (LPBYTE)&val, &size);
    RegCloseKey(hKey);
    return (result == ERROR_SUCCESS);
}

std::wstring UsbIpServerDaemon::GetDevicePathFromBusId(const std::string& targetBusId) {
    HDEVINFO devInfo = SetupDiGetClassDevsW(&GUID_DEVCLASS_USB, NULL, NULL, DIGCF_PRESENT);
    if (devInfo == INVALID_HANDLE_VALUE) return L"";

    SP_DEVINFO_DATA devInfoData;
    devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    std::wstring foundPath = L"";

    for (DWORD i = 0; SetupDiEnumDeviceInfo(devInfo, i, &devInfoData); i++) {
        wchar_t buffer[MAX_DEVICE_ID_LEN] = {0};

        if (CM_Get_Device_IDW(devInfoData.DevInst, buffer, MAX_DEVICE_ID_LEN, 0) == CR_SUCCESS) {
            std::wstring instanceId(buffer);
            size_t slashIndex = instanceId.find_last_of(L"\\");
            if (slashIndex != std::wstring::npos) {
                std::string busId = UsbDeviceMonitor::WideToString(instanceId.substr(slashIndex + 1, 5));
                if (busId == targetBusId) {
                    SP_DEVICE_INTERFACE_DATA interfaceData;
                    interfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
                    
                    if (SetupDiCreateDeviceInterfaceW(devInfo, &devInfoData, &GUID_DEVINTERFACE_USB_DEVICE, 0, 0, &interfaceData)) {
                        DWORD detailSize = 0;
                        SetupDiGetDeviceInterfaceDetailW(devInfo, &interfaceData, NULL, 0, &detailSize, NULL);
                        
                        if (detailSize > 0) {
                            std::vector<BYTE> detailDataBuffer(detailSize);
                            PSP_DEVICE_INTERFACE_DETAIL_DATA_W pDetail = (PSP_DEVICE_INTERFACE_DETAIL_DATA_W)detailDataBuffer.data();
                            pDetail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

                            if (SetupDiGetDeviceInterfaceDetailW(devInfo, &interfaceData, pDetail, detailSize, NULL, NULL)) {
                                foundPath = pDetail->DevicePath;
                                break;
                            }
                        }
                    }
                }
            }
        }
    }

    SetupDiDestroyDeviceInfoList(devInfo);
    return foundPath;
}

bool UsbIpServerDaemon::OpenPhysicalUsbDevice(const std::string& busId, HANDLE& outDeviceHandle, WINUSB_INTERFACE_HANDLE& outWinUsbHandle) {
    std::wstring devicePath = GetDevicePathFromBusId(busId);
    if (devicePath.empty()) {
        std::cerr << "[ERROR] Could not resolve Windows Device Path for Bus ID: " << busId << std::endl;
        return false;
    }

    outDeviceHandle = CreateFileW(
        devicePath.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
        NULL
    );

    if (outDeviceHandle == INVALID_HANDLE_VALUE) {
        std::cerr << "[ERROR] Failed to open physical USB device handle." << std::endl;
        return false;
    }

    if (!WinUsb_Initialize(outDeviceHandle, &outWinUsbHandle)) {
        std::cerr << "[ERROR] WinUsb_Initialize failed (Error Code: " << GetLastError() << ")." << std::endl;
        CloseHandle(outDeviceHandle);
        outDeviceHandle = INVALID_HANDLE_VALUE;
        return false;
    }

    std::cout << "[SUCCESS] Physical WinUSB Hardware Handle Claimed for BUSID: " << busId << std::endl;
    return true;
}

bool UsbIpServerDaemon::PopulateImportDetails(const std::string& targetBusId, op_rep_import& reply) {
    reply.header.version = htons(USBIP_VERSION);
    reply.header.command = htons(0x0003);
    reply.header.status = 0;
    strcpy_s(reply.busid, targetBusId.c_str());
    strcpy_s(reply.path, "/sys/devices/virtual/usbip_win2/dev0");
    reply.speed = htonl(3);

    HANDLE hDevice = INVALID_HANDLE_VALUE;
    WINUSB_INTERFACE_HANDLE hWinUsb = NULL;
    if (OpenPhysicalUsbDevice(targetBusId, hDevice, hWinUsb)) {
        USB_DEVICE_DESCRIPTOR devDesc{};
        ULONG lengthTransferred = 0;
        
        if (WinUsb_GetDescriptor(hWinUsb, USB_DEVICE_DESCRIPTOR_TYPE, 0, 0, (PUCHAR)&devDesc, sizeof(devDesc), &lengthTransferred)) {
            reply.idVendor = htons(devDesc.idVendor);
            reply.idProduct = htons(devDesc.idProduct);
            reply.bcdDevice = htons(devDesc.bcdDevice);
            reply.bDeviceClass = devDesc.bDeviceClass;
            reply.bDeviceSubClass = devDesc.bDeviceSubClass;
            reply.bDeviceProtocol = devDesc.bDeviceProtocol;
            reply.bNumConfigurations = devDesc.bNumConfigurations;
        }

        WinUsb_Free(hWinUsb);
        CloseHandle(hDevice);
    } else {
        reply.idVendor = htons(0x045E);
        reply.idProduct = htons(0x028E);
    }

    return true;
}

void UsbIpServerDaemon::MaintainDataTunnel(SOCKET clientSocket, const std::string& targetBusId) {
    SetDeviceAttached(targetBusId, true);

    HANDLE hDevice = INVALID_HANDLE_VALUE;
    WINUSB_INTERFACE_HANDLE hWinUsb = NULL;

    bool hardwareAvailable = OpenPhysicalUsbDevice(targetBusId, hDevice, hWinUsb);

    while (true) {
        usbip_header_basic commonPrefix;
        if (recv(clientSocket, (char*)&commonPrefix, sizeof(usbip_header_basic), MSG_WAITALL) <= 0)
            break;

        uint32_t typeCode = ntohl(commonPrefix.command);
        uint32_t uniqueSeq = ntohl(commonPrefix.seqnum);
        uint32_t epNumber = ntohl(commonPrefix.ep);
        uint32_t direction = ntohl(commonPrefix.direction);

        if (typeCode == 0x00000001) {
            usbip_cmd_submit incomingUrb;
            memcpy(&incomingUrb.basic, &commonPrefix, sizeof(usbip_header_basic));
            recv(clientSocket, (char*)&incomingUrb + sizeof(usbip_header_basic),
                 sizeof(usbip_cmd_submit) - sizeof(usbip_header_basic), MSG_WAITALL);

            uint32_t bufferLen = ntohl(incomingUrb.transfer_buffer_length);
            std::vector<char> transferPayload(bufferLen);

            if (direction == 0 && bufferLen > 0) {
                recv(clientSocket, transferPayload.data(), bufferLen, MSG_WAITALL);
            }

            ULONG bytesTransferred = 0;
            BOOL winUsbResult = FALSE;

            if (hardwareAvailable) {
                if (epNumber == 0) {
                    WINUSB_SETUP_PACKET setupPacket;
                    memcpy(&setupPacket, incomingUrb.setup, 8);

                    winUsbResult = WinUsb_ControlTransfer(
                        hWinUsb,
                        setupPacket,
                        (PUCHAR)transferPayload.data(),
                        bufferLen,
                        &bytesTransferred,
                        NULL
                    );
                } else {
                    UCHAR pipeId = (UCHAR)epNumber;
                    if (direction == 1) pipeId |= 0x80;

                    if (direction == 1) {
                        winUsbResult = WinUsb_ReadPipe(hWinUsb, pipeId, (PUCHAR)transferPayload.data(), bufferLen, &bytesTransferred, NULL);
                    } else {
                        winUsbResult = WinUsb_WritePipe(hWinUsb, pipeId, (PUCHAR)transferPayload.data(), bufferLen, &bytesTransferred, NULL);
                    }

                    // Auto-reset endpoint pipe if hardware stalls
                    if (!winUsbResult) {
                        WinUsb_ResetPipe(hWinUsb, pipeId);
                    }
                }
            } else {
                winUsbResult = TRUE;
                bytesTransferred = bufferLen;
            }

            usbip_ret_submit feedbackFrame{};
            feedbackFrame.basic.command = htonl(0x00000003);
            feedbackFrame.basic.seqnum = htonl(uniqueSeq);
            feedbackFrame.status = winUsbResult ? htonl(0) : htonl(1);
            feedbackFrame.actual_length = htonl(bytesTransferred);

            send(clientSocket, (char*)&feedbackFrame, sizeof(usbip_ret_submit), 0);

            if (direction == 1 && bytesTransferred > 0) {
                send(clientSocket, transferPayload.data(), bytesTransferred, 0);
            }
        }
    }

    // Always release handles to prevent host device locks
    if (hardwareAvailable) {
        WinUsb_Free(hWinUsb);
        CloseHandle(hDevice);
    }

    SetDeviceAttached(targetBusId, false);
    closesocket(clientSocket);
}

void UsbIpServerDaemon::ExecuteProtocolEngine(SOCKET clientSocket) {
    usbip_header clientIntention;
    if (recv(clientSocket, (char*)&clientIntention, sizeof(usbip_header), MSG_WAITALL) <= 0) {
        closesocket(clientSocket);
        return;
    }
    uint16_t parsedCmd = ntohs(clientIntention.command);
    if (parsedCmd == 0x8005) {
        op_rep_devlist emptyCatalog{};
        emptyCatalog.header.version = htons(USBIP_VERSION);
        emptyCatalog.header.command = htons(0x0005);
        emptyCatalog.num_devices = htonl(0);
        send(clientSocket, (char*)&emptyCatalog, sizeof(emptyCatalog), 0);
        closesocket(clientSocket);
    } else if (parsedCmd == 0x8003) {
        char requestedId[32] = {0};
        recv(clientSocket, requestedId, 32, MSG_WAITALL);

        std::string targetBusId(requestedId);
        size_t nullPos = targetBusId.find('\0');
        if (nullPos != std::string::npos) {
            targetBusId = targetBusId.substr(0, nullPos);
        }

        op_rep_import mappingResponse{};
        if (PopulateImportDetails(targetBusId, mappingResponse)) {
            send(clientSocket, (char*)&mappingResponse, sizeof(op_rep_import), 0);
            MaintainDataTunnel(clientSocket, targetBusId);
        } else {
            closesocket(clientSocket);
        }
    }
}

void UsbIpServerDaemon::ExecuteBind(const std::string& busId) {
    HKEY hKey;
    LONG result = RegCreateKeyExW(HKEY_LOCAL_MACHINE, REG_BOUND_PATH, 0, NULL,
                                 REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL);
    if (result != ERROR_SUCCESS) {
        std::cerr << "Error: Failed to access registry. Please run as Administrator." << std::endl;
        return;
    }

    std::wstring wBusId(busId.begin(), busId.end());
    DWORD val = 1;
    result = RegSetValueExW(hKey, wBusId.c_str(), 0, REG_DWORD, (const BYTE *)&val, sizeof(val));
    RegCloseKey(hKey);

    if (result == ERROR_SUCCESS) {
        std::cout << "info: bind successful for busid " << busId << std::endl;
    } else {
        std::cerr << "Error: Could not save binding configuration for " << busId << std::endl;
    }
}

void UsbIpServerDaemon::ExecuteUnbind(const std::string& busId) {
    HKEY hKey;
    LONG result = RegOpenKeyExW(HKEY_LOCAL_MACHINE, REG_BOUND_PATH, 0, KEY_WRITE, &hKey);
    if (result != ERROR_SUCCESS) {
        std::cerr << "Error: No bound devices found or run as Administrator." << std::endl;
        return;
    }

    std::wstring wBusId(busId.begin(), busId.end());
    result = RegDeleteValueW(hKey, wBusId.c_str());
    RegCloseKey(hKey);

    if (result == ERROR_SUCCESS) {
        std::cout << "info: unbind successful for busid " << busId << std::endl;
    } else {
        std::cerr << "Error: Busid " << busId << " was not found in the bound list." << std::endl;
    }
}

void UsbIpServerDaemon::ExecuteUnbindAll() {
    HKEY hKey;
    LONG result = RegOpenKeyExW(HKEY_LOCAL_MACHINE, REG_BOUND_PATH, 0, KEY_READ | KEY_WRITE, &hKey);
    if (result != ERROR_SUCCESS) {
        return;
    }

    wchar_t valueName[256];
    DWORD valueNameSize = 256;
    std::vector<std::wstring> valuesToDelete;

    DWORD i = 0;
    while (RegEnumValueW(hKey, i, valueName, &valueNameSize, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
        valuesToDelete.push_back(valueName);
        i++;
        valueNameSize = 256;
    }

    for (const auto &val : valuesToDelete) {
        RegDeleteValueW(hKey, val.c_str());
    }

    RegCloseKey(hKey);
    std::cout << "info: unbind all successful, all older devices unbound." << std::endl;
}

void UsbIpServerDaemon::ListDevices() {
    m_monitor.ListConnectedDevices();
}

void UsbIpServerDaemon::StartDaemon() {
    m_monitor.SetDaemonReference(this);
    m_monitor.StartHotplugListener();

    WSADATA networkData;
    WSAStartup(MAKEWORD(2, 2), &networkData);
    struct addrinfo requirements{}, *resolution = NULL;
    requirements.ai_family = AF_INET;
    requirements.ai_socktype = SOCK_STREAM;
    requirements.ai_protocol = IPPROTO_TCP;
    requirements.ai_flags = AI_PASSIVE;
    getaddrinfo(NULL, USBIP_PORT, &requirements, &resolution);
    SOCKET hostingSocket = socket(resolution->ai_family, resolution->ai_socktype, resolution->ai_protocol);
    bind(hostingSocket, resolution->ai_addr, (int)resolution->ai_addrlen);
    listen(hostingSocket, SOMAXCONN);
    freeaddrinfo(resolution);

    std::cout << "[ONLINE] USBIP WinUSB Server listening on port " << USBIP_PORT << "..." << std::endl;
    while (true) {
        SOCKET acceptedClient = accept(hostingSocket, NULL, NULL);
        if (acceptedClient != INVALID_SOCKET) {
            std::thread([this, acceptedClient]() {
                this->ExecuteProtocolEngine(acceptedClient);
            }).detach();
        }
    }
    closesocket(hostingSocket);
    WSACleanup();
}

int main(int argc, char *argv[]) {
    if (!IsRunningAsAdmin()) {
        std::cerr << "[WARNING] usbip_server requires Administrator privileges for INF driver installation and registry access." << std::endl;
        std::cerr << "          Please restart the command prompt as Administrator." << std::endl;
    }

    UsbIpServerDaemon server;

    if (argc > 1) {
        std::string argument = argv[1];
        if (argument == "--list" || argument == "-l") {
            server.ListDevices();
            return 0;
        } else if (argument == "--bind" || argument == "-b") {
            if (argc > 2) {
                server.ExecuteBind(argv[2]);
                return 0;
            } else {
                std::cerr << "Error: --bind requires a BUSID argument." << std::endl;
                return 1;
            }
        } else if (argument == "--unbind" || argument == "-u") {
            if (argc > 2) {
                std::string subArg = argv[2];
                if (subArg == "all") {
                    server.ExecuteUnbindAll();
                } else {
                    server.ExecuteUnbind(subArg);
                }
                return 0;
            } else {
                std::cerr << "Error: --unbind requires a BUSID or 'all' argument." << std::endl;
                return 1;
            }
        } else if (argument == "--unbind-all") {
            server.ExecuteUnbindAll();
            return 0;
        } else {
            std::cout << "Unknown option: " << argument << "\n\n"
                      << "Usage:\n"
                      << "  usbip_server.exe --list\n"
                      << "  usbip_server.exe --bind <BUSID>\n"
                      << "  usbip_server.exe --unbind <BUSID>\n"
                      << "  usbip_server.exe --unbind all (or --unbind-all)\n"
                      << std::endl;
            return 1;
        }
    }

    server.StartDaemon();
    return 0;
}