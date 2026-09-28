#include "usbipd.h"
#include <iostream>

DEFINE_GUID(GUID_DEVCLASS_USB, 0x36fc9e60, 0xc465, 0x11cf, 0x80, 0x56, 0x44,
            0x45, 0x53, 0x54, 0x00, 0x00);

bool ReceiveExactBytes(SOCKET s, char *buffer, int bytesToRead) {
  int totalRead = 0;
  while (totalRead < bytesToRead) {
    int bytesRead = recv(s, buffer + totalRead, bytesToRead - totalRead, 0);
    if (bytesRead <= 0) {
      return false;
    }
    totalRead += bytesRead;
  }
  return true;
}

bool IsDeviceAuthorizedInRegistry(const std::string &hardwareId) {
  HKEY hKey;
  std::wstring baseSubKey = L"SOFTWARE\\usbipd-win\\Devices";

  LONG result =
      RegOpenKeyExW(HKEY_LOCAL_MACHINE, baseSubKey.c_str(), 0, KEY_READ, &hKey);
  if (result != ERROR_SUCCESS) {
    return false;
  }

  DWORD index = 0;
  wchar_t subKeyName[256];
  DWORD subKeyNameSize = 256;
  bool matchFound = false;
  std::wstring targetIdW(hardwareId.begin(), hardwareId.end());

  while (RegEnumKeyExW(hKey, index, subKeyName, &subKeyNameSize, nullptr,
                       nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
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
  HDEVINFO hDevInfo =
      SetupDiGetClassDevsW(&GUID_DEVCLASS_USB, nullptr, nullptr, DIGCF_PRESENT);
  if (hDevInfo == (HDEVINFO)(INVALID_HANDLE_VALUE))
    return detectedDevices;

  SP_DEVINFO_DATA devInfoData;
  devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
  DWORD index = 0;

  while (SetupDiEnumDeviceInfo(hDevInfo, index, &devInfoData)) {
    index++;
    wchar_t instanceId[MAX_DEVICE_ID_LEN];
    if (!SetupDiGetDeviceInstanceIdW(hDevInfo, &devInfoData, instanceId,
                                     MAX_DEVICE_ID_LEN, nullptr)) {
      continue;
    }

    std::wstring wsInstanceId(instanceId);
    if (wsInstanceId.find(L"USB\\VID_") != 0)
      continue;

    size_t vidPos = wsInstanceId.find(L"VID_");
    size_t pidPos = wsInstanceId.find(L"PID_");
    if (vidPos == std::wstring::npos || pidPos == std::wstring::npos)
      continue;

    std::wstring vidStr = wsInstanceId.substr(vidPos + 4, 4);
    std::wstring pidStr = wsInstanceId.substr(pidPos + 4, 4);
    uint16_t vid = (uint16_t)std::stoul(vidStr, nullptr, 16);
    uint16_t pid = (uint16_t)std::stoul(pidStr, nullptr, 16);

    USBIP_DEVICE_DESC devExchange = {0};
    strcpy_s(devExchange.path,
             "/sys/devices/platform/virtual_host_hub/usb1/1-1");
    strcpy_s(devExchange.busid, "1-1");
    devExchange.busnum = SWAP32(1);
    devExchange.devnum = SWAP32(2);
    devExchange.speed = SWAP32(3); // USB_SPEED_HIGH
    devExchange.idVendor = SWAP16(vid);
    devExchange.idProduct = SWAP16(pid);
    devExchange.bcdDevice = SWAP16(0x0100);
    devExchange.bDeviceClass = 0x00;
    devExchange.bNumConfigurations = 1;
    devExchange.bNumInterfaces = 1;

    std::string rawHardwareStr(wsInstanceId.begin(), wsInstanceId.end());
    std::cout << "[+] Found Device -> VID: " << std::hex << vid
              << " PID: " << std::hex << pid;
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
  USBIP_OP_COMMON commonHeader;
  if (!ReceiveExactBytes(clientSocket, (char *)&commonHeader,
                         sizeof(USBIP_OP_COMMON))) {
    closesocket(clientSocket);
    return;
  }

  uint16_t evaluatedCommand = ntohs(commonHeader.commandCode);
  std::cout << "[*] Parsing Handshake Opcode: 0x" << std::hex
            << evaluatedCommand << std::endl;

  // Handshake 1: Device List Request (OP_REQ_DEVLIST)
  if (evaluatedCommand == 0x8005) {
    std::vector<USBIP_DEVICE_DESC> activeList = ScanPhysicalUsbBus();

    USBIP_OP_REP_DEVLIST listReply = {0};
    listReply.common.version = SWAP16(0x0111);
    listReply.common.commandCode = SWAP16(0x0005);
    listReply.common.status = SWAP32(0);
    listReply.numDevices = SWAP32((uint32_t)activeList.size());

    send(clientSocket, (char *)&listReply, sizeof(USBIP_OP_REP_DEVLIST), 0);
    for (auto &individualDevice : activeList) {
      send(clientSocket, (char *)&individualDevice, sizeof(USBIP_DEVICE_DESC),
           0);
      uint8_t interfaceSubBlock[] = {0x00, 0x00, 0x00, 0x00};
      send(clientSocket, (char *)interfaceSubBlock, sizeof(interfaceSubBlock),
           0);
    }
    closesocket(clientSocket);
  }
  // Handshake 2: Device Import Request (OP_REQ_IMPORT)
  else if (evaluatedCommand == 0x8003) {
    char busidReq[32];
    if (!ReceiveExactBytes(clientSocket, busidReq, sizeof(busidReq))) {
      closesocket(clientSocket);
      return;
    }

    USBIP_OP_REP_IMPORT importReply = {0};
    importReply.common.version = SWAP16(0x0111);
    importReply.common.commandCode = SWAP16(0x0003);
    importReply.common.status = SWAP32(0);

    strcpy_s(importReply.dev.path,
             "/sys/devices/platform/virtual_host_hub/usb1/1-1");
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

    send(clientSocket, (char *)&importReply, sizeof(USBIP_OP_REP_IMPORT), 0);
    std::cout << "[+] Sent OP_REP_IMPORT acknowledgement to Windows Client. "
                 "Bridge open."
              << std::endl;

    // Direct USB/IP Streaming Loop
    while (true) {
      USBIP_HEADER_BASIC basicHeader;
      if (!ReceiveExactBytes(clientSocket, (char *)&basicHeader,
                             sizeof(USBIP_HEADER_BASIC))) {
        std::cout << "[-] Windows Client dropped structural line link."
                  << std::endl;
        break;
      }

      uint32_t cmdType = ntohl(basicHeader.command);
      uint32_t seqNum = ntohl(basicHeader.seqnum);
      uint32_t direction = ntohl(basicHeader.direction);

      // Handle USBIP_CMD_SUBMIT (0x00000001)
      if (cmdType == 0x00000001) {
        // Read remaining payload after basic header (sizeof USBIP_CMD_SUBMIT -
        // sizeof USBIP_HEADER_BASIC)
        struct CMD_SUBMIT_TAIL {
          uint32_t transferFlags;
          int32_t transferBufferLength;
          uint32_t startFrame;
          uint32_t numberOfPackets;
          uint32_t interval;
          uint8_t setup[8];
        } tail;

        if (!ReceiveExactBytes(clientSocket, (char *)&tail, sizeof(tail))) {
          break;
        }

        int32_t reqLen = ntohl(tail.transferBufferLength);

        std::cout << "[~] Windows I/O Transaction Stream -> Command: 0x"
                  << std::hex << cmdType << " | ID Token: " << std::dec
                  << seqNum << " | Expected Size: " << reqLen << " bytes."
                  << std::endl;

        // Receive host payload if OUT transfer (direction == 0)
        if (direction == 0 && reqLen > 0) {
          std::vector<char> dataBuffer(reqLen);
          if (!ReceiveExactBytes(clientSocket, dataBuffer.data(), reqLen)) {
            break;
          }
        }

        USBIP_RET_SUBMIT retSubmit = {0};
        retSubmit.base.command = htonl(0x00000003); // RET_SUBMIT response ID
        retSubmit.base.seqnum = htonl(seqNum);
        retSubmit.base.devid = basicHeader.devid;
        retSubmit.base.direction = htonl(direction);
        retSubmit.base.ep = basicHeader.ep;
        retSubmit.status = htonl(0); // Success status

        // Send IN transfer response payload (direction == 1)
        if (direction == 1 && reqLen > 0) {
          retSubmit.actualLength = htonl(reqLen);
          send(clientSocket, (char *)&retSubmit, sizeof(USBIP_RET_SUBMIT), 0);

          std::vector<char> mockPayload(reqLen, 0);
          send(clientSocket, mockPayload.data(), reqLen, 0);
        } else {
          retSubmit.actualLength = htonl(0);
          send(clientSocket, (char *)&retSubmit, sizeof(USBIP_RET_SUBMIT), 0);
        }
      }
      // Handle USBIP_CMD_UNLINK (0x00000002)
      else if (cmdType == 0x00000002) {
        struct CMD_UNLINK_TAIL {
          uint32_t unlinkSeqnum;
          uint8_t padding[24];
        } unlinkTail;

        if (!ReceiveExactBytes(clientSocket, (char *)&unlinkTail,
                               sizeof(unlinkTail))) {
          break;
        }

        USBIP_RET_UNLINK retUnlink = {0};
        retUnlink.base.command = htonl(0x00000004); // RET_UNLINK response ID
        retUnlink.base.seqnum = htonl(seqNum);
        retUnlink.status = htonl(0); // Success (-ECONNRESET normally)

        send(clientSocket, (char *)&retUnlink, sizeof(USBIP_RET_UNLINK), 0);
      } else {
        std::cerr << "[-] Unknown Command Type Received: 0x" << std::hex
                  << cmdType << std::endl;
        break;
      }
    }
    closesocket(clientSocket);
  } else {
    closesocket(clientSocket);
  }
}