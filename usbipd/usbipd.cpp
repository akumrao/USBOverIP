#include "usbipd.h"
#include <iostream>

DEFINE_GUID(GUID_DEVCLASS_USB, 0x36fc9e60, 0xc465, 0x11cf, 0x80, 0x56, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);

bool IsDeviceAuthorizedInRegistry(const std::string& hardwareId) {
    HKEY hKey;
    std::wstring baseSubKey = L"SOFTWARE\\usbipd-win\\Devices";
    
    LONG result = RegOpenKeyExW(HKEY_LOCAL_MACHINE, baseSubKey.c_str(), 0, KEY_READ, &hKey);
    if (result != ERROR_SUCCESS) {
        return false;
    }

    DWORD index = 0;
    wchar_t subKeyName[256];
    DWORD subKeyNameSize = 256;
    bool matchFound = false;
    std::wstring targetIdW(hardwareId.begin(), hardwareId.end());

    while (RegEnumKeyExW(hKey, index, subKeyName, &subKeyNameSize, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
        std::wstring currentKey(subKeyName);
        if (currentKey.find(targetIdW) != std::wstring::npos) {
            matchFound = true;
            break;
        }
        subKeyNameSize = 256;
        index++;
    }

    RegCloseKey(hKey);
    return matchFound;
}

std::vector<USBIP_DEVICE_DESC> ScanPhysicalUsbBus() {
    std::vector<USBIP_DEVICE_DESC> detectedDevices;
    HDEVINFO hDevInfo = SetupDiGetClassDevsW(&GUID_DEVCLASS_USB, nullptr, nullptr, DIGCF_PRESENT);
    if (hDevInfo == (HDEVINFO)(INVALID_HANDLE_VALUE)) return detectedDevices;

    SP_DEVINFO_DATA devInfoData;
    devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    DWORD index = 0;

    while (SetupDiEnumDeviceInfo(hDevInfo, index, &devInfoData)) {
        index++;
        wchar_t instanceId[MAX_DEVICE_ID_LEN];
        if (!SetupDiGetDeviceInstanceIdW(hDevInfo, &devInfoData, instanceId, MAX_DEVICE_ID_LEN, nullptr)) {
            continue;
        }

        std::wstring wsInstanceId(instanceId);
        if (wsInstanceId.find(L"USB\\VID_") != 0) continue;

        size_t vidPos = wsInstanceId.find(L"VID_");
        size_t pidPos = wsInstanceId.find(L"PID_");
        if (vidPos == std::wstring::npos || pidPos == std::wstring::npos) continue;

        std::wstring vidStr = wsInstanceId.substr(vidPos + 4, 4);
        std::wstring pidStr = wsInstanceId.substr(pidPos + 4, 4);
        uint16_t vid = (uint16_t)std::stoul(vidStr, nullptr, 16);
        uint16_t pid = (uint16_t)std::stoul(pidStr, nullptr, 16);

        USBIP_DEVICE_DESC devExchange = { 0 };
        strcpy_s(devExchange.path, "/sys/devices/platform/virtual_host_hub/usb1/1-1");
        strcpy_s(devExchange.busid, "1-1");
        devExchange.busnum = SWAP32(1);
        devExchange.devnum = SWAP32(2);
        devExchange.speed = SWAP32(3);
        devExchange.idVendor = SWAP16(vid);
        devExchange.idProduct = SWAP16(pid);
        devExchange.bcdDevice = SWAP16(0x0100);
        devExchange.bDeviceClass = 0x00;
        devExchange.bNumConfigurations = 1;
        devExchange.bNumInterfaces = 1;

        std::string rawHardwareStr(wsInstanceId.begin(), wsInstanceId.end());
        std::cout << "[+] Found Device -> VID: " << std::hex << vid << " PID: " << std::hex << pid;
        if (IsDeviceAuthorizedInRegistry(rawHardwareStr)) {
            std::cout << " [AUTHORIZED FOR SHARE]" << std::endl;
        } else {
            std::cout << " [LOCAL ONLY]" << std::endl;
        }
        detectedDevices.push_back(devExchange);
    }

    SetupDiDestroyDeviceInfoList(hDevInfo);
    return detectedDevices;
}

void ConnectionWorkerThread(SOCKET clientSocket) {
    std::vector<char> networkBuffer(DEFAULT_BUFLEN);
    int receivedBytes = recv(clientSocket, networkBuffer.data(), sizeof(USBIP_OP_COMMON), 0);
    if (receivedBytes <= 0) {
        closesocket(clientSocket);
        return;
    }

    USBIP_OP_COMMON* incomingHeader = (USBIP_OP_COMMON*)networkBuffer.data();
    uint16_t evaluatedCommand = ntohs(incomingHeader->commandCode);
    std::cout << "[*] Parsing Incoming Handshake Packet. Target Opcode Command: 0x" << std::hex << evaluatedCommand << std::endl;

    if (evaluatedCommand == 0x8005) {
        std::cout << "[*] Remote client requested device catalog listing. Querying host bus..." << std::endl;
        std::vector<USBIP_DEVICE_DESC> activeList = ScanPhysicalUsbBus();

        USBIP_OP_REP_DEVLIST listReply = { 0 };
        listReply.common.version = SWAP16(0x0111);
        listReply.common.commandCode = SWAP16(0x0005);
        listReply.common.status = SWAP32(0);
        listReply.numDevices = SWAP32((uint32_t)activeList.size());

        send(clientSocket, (char*)&listReply, sizeof(USBIP_OP_REP_DEVLIST), 0);
        for (auto& individualDevice : activeList) {
            send(clientSocket, (char*)&individualDevice, sizeof(USBIP_DEVICE_DESC), 0);
            uint8_t interfaceSubBlock[4] = { 0x00, 0x00, 0x00, 0x00 };
            send(clientSocket, (char*)interfaceSubBlock, sizeof(interfaceSubBlock), 0);
        }
        std::cout << "[+] Structural protocol pipeline write complete. Device maps synced to client." << std::endl;
        closesocket(clientSocket);
    }
    else if (evaluatedCommand == 0x8003) {
        std::cout << "[*] Remote client triggered OP_REQ_IMPORT sequence to capture a device." << std::endl;
        int remainingBytes = recv(clientSocket, networkBuffer.data() + sizeof(USBIP_OP_COMMON), sizeof(USBIP_OP_REQ_IMPORT) - sizeof(USBIP_OP_COMMON), 0);
        if (remainingBytes <= 0) {
            closesocket(clientSocket);
            return;
        }

        USBIP_OP_REQ_IMPORT* importReq = (USBIP_OP_REQ_IMPORT*)networkBuffer.data();
        std::cout << "[+] Client requested detachment path for Bus ID: " << importReq->busid << std::endl;

        USBIP_OP_REP_IMPORT importReply = { 0 };
        importReply.common.version = SWAP16(0x0111);
        importReply.common.commandCode = SWAP16(0x0003);
        importReply.common.status = SWAP32(0);

        strcpy_s(importReply.dev.path, "/sys/devices/platform/virtual_host_hub/usb1/1-1");
        strcpy_s(importReply.dev.busid, "1-1");
        importReply.dev.busnum = SWAP32(1);
        importReply.dev.devnum = SWAP32(2);
        importReply.dev.speed = SWAP32(3);
        importReply.dev.idVendor = SWAP16(0x0781);
        importReply.dev.idProduct = SWAP16(0x5590);
        importReply.dev.bcdDevice = SWAP16(0x0100);
        importReply.dev.bDeviceClass = 0x00;
        importReply.dev.bNumConfigurations = 1;
        importReply.dev.bNumInterfaces = 1;

        send(clientSocket, (char*)&importReply, sizeof(USBIP_OP_REP_IMPORT), 0);
        std::cout << "[+] Sent OP_REP_IMPORT acknowledgement frame. Network session is now locked open." << std::endl;

        char continuousBuffer[1024];
        while (true) {
            int liveStreamBytes = recv(clientSocket, continuousBuffer, sizeof(continuousBuffer), 0);
            if (liveStreamBytes <= 0) {
                std::cout << "[-] Client disconnected from device streaming socket pipeline." << std::endl;
                break;
            }
            std::cout << "[~] Intercepted raw data traffic packet from client size: " << std::dec << liveStreamBytes << " bytes." << std::endl;
        }
        closesocket(clientSocket);
    }
    else {
        closesocket(clientSocket);
    }
}
