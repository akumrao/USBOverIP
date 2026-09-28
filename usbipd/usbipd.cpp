#include "usbipd.h"
#include <iostream>
#include <vector>

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
    devExchange.speed = SWAP32(3); // High speed
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
  } else if (evaluatedCommand == 0x8003) {
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
      uint32_t ep = ntohl(basicHeader.ep);

      if (cmdType == 0x00000001) { // USBIP_CMD_SUBMIT
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

        std::cout << "[~] Stream -> Cmd: 0x" << std::hex << cmdType
                  << " | EP: " << std::dec << ep << " | ID Token: " << seqNum
                  << " | Expected Size: " << reqLen << " bytes." << std::endl;

        // OUT Transfers (Host -> Device)
        if (direction == 0 && reqLen > 0) {
          std::vector<char> dataBuffer(reqLen);
          if (!ReceiveExactBytes(clientSocket, dataBuffer.data(), reqLen)) {
            break;
          }
        }

        USBIP_RET_SUBMIT retSubmit = {0};
        retSubmit.base.command = htonl(0x00000003);
        retSubmit.base.seqnum = htonl(seqNum);
        retSubmit.base.devid = basicHeader.devid;
        retSubmit.base.direction = htonl(direction);
        retSubmit.base.ep = basicHeader.ep;
        retSubmit.status = htonl(0); // Success

        // IN Transfers (Device -> Host)
        if (direction == 1 && reqLen > 0) {
          std::vector<char> payloadBuffer(reqLen, 0);

          // Parse EP0 Control Setup Request
          if (ep == 0) {
            uint8_t bmRequestType = tail.setup[0];
            uint8_t bRequest = tail.setup[1];
            uint8_t descriptorType = tail.setup[3];
            uint8_t descriptorIndex = tail.setup[2];

            // 0x06 = GET_DESCRIPTOR
            if (bRequest == 0x06) {
              // Device Descriptor (0x01)
              if (descriptorType == 0x01) {
                USB_DEVICE_DESCRIPTOR devDesc = {0};
                devDesc.bLength = sizeof(USB_DEVICE_DESCRIPTOR);
                devDesc.bDescriptorType = 0x01;
                devDesc.bcdUSB = 0x0200; // USB 2.0
                devDesc.bDeviceClass = 0x00;
                devDesc.bDeviceSubClass = 0x00;
                devDesc.bDeviceProtocol = 0x00;
                devDesc.bMaxPacketSize0 = 64;
                devDesc.idVendor = 0x0781;  // SanDisk
                devDesc.idProduct = 0x5590; // Ultra USB
                devDesc.bcdDevice = 0x0100;
                devDesc.iManufacturer = 1;
                devDesc.iProduct = 2;
                devDesc.iSerialNumber = 3;
                devDesc.bNumConfigurations = 1;

                int copyLen = (reqLen < (int)sizeof(devDesc))
                                  ? reqLen
                                  : (int)sizeof(devDesc);
                memcpy(payloadBuffer.data(), &devDesc, copyLen);
              }
              // Configuration Descriptor (0x02)
              else if (descriptorType == 0x02) {
                USB_FULL_CONFIG_PACKET fullConfig = {0};

                // Config Header
                fullConfig.config.bLength =
                    sizeof(USB_CONFIGURATION_DESCRIPTOR);
                fullConfig.config.bDescriptorType = 0x02;
                fullConfig.config.wTotalLength = sizeof(USB_FULL_CONFIG_PACKET);
                fullConfig.config.bNumInterfaces = 1;
                fullConfig.config.bConfigurationValue = 1;
                fullConfig.config.iConfiguration = 0;
                fullConfig.config.bmAttributes = 0x80; // Bus powered
                fullConfig.config.bMaxPower = 50;      // 100mA

                // Mass Storage Interface
                fullConfig.interface0.bLength =
                    sizeof(USB_INTERFACE_DESCRIPTOR);
                fullConfig.interface0.bDescriptorType = 0x04;
                fullConfig.interface0.bInterfaceNumber = 0;
                fullConfig.interface0.bAlternateSetting = 0;
                fullConfig.interface0.bNumEndpoints = 2;
                fullConfig.interface0.bInterfaceClass = 0x08; // Mass Storage
                fullConfig.interface0.bInterfaceSubClass =
                    0x06; // SCSI Transparent
                fullConfig.interface0.bInterfaceProtocol = 0x50; // Bulk-Only
                fullConfig.interface0.iInterface = 0;

                // Bulk IN Endpoint
                fullConfig.epIn.bLength = sizeof(USB_ENDPOINT_DESCRIPTOR);
                fullConfig.epIn.bDescriptorType = 0x05;
                fullConfig.epIn.bEndpointAddress = 0x81; // EP1 IN
                fullConfig.epIn.bmAttributes = 0x02;     // Bulk
                fullConfig.epIn.wMaxPacketSize = 512;
                fullConfig.epIn.bInterval = 0;

                // Bulk OUT Endpoint
                fullConfig.epOut.bLength = sizeof(USB_ENDPOINT_DESCRIPTOR);
                fullConfig.epOut.bDescriptorType = 0x05;
                fullConfig.epOut.bEndpointAddress = 0x01; // EP1 OUT
                fullConfig.epOut.bmAttributes = 0x02;     // Bulk
                fullConfig.epOut.wMaxPacketSize = 512;
                fullConfig.epOut.bInterval = 0;

                int copyLen = (reqLen < (int)sizeof(fullConfig))
                                  ? reqLen
                                  : (int)sizeof(fullConfig);
                memcpy(payloadBuffer.data(), &fullConfig, copyLen);
              }
              // String Descriptors (0x03)
              else if (descriptorType == 0x03) {
                if (descriptorIndex == 0) { // Supported Languages
                  uint8_t langDesc[] = {0x04, 0x03, 0x09,
                                        0x04}; // EN-US (0x0409)
                  int copyLen = (reqLen < (int)sizeof(langDesc))
                                    ? reqLen
                                    : (int)sizeof(langDesc);
                  memcpy(payloadBuffer.data(), langDesc, copyLen);
                } else if (descriptorIndex == 1) { // Manufacturer
                  wchar_t mfgStr[] = L"Virtual USB";
                  uint8_t len = (uint8_t)(sizeof(mfgStr));
                  payloadBuffer[0] = len + 2;
                  payloadBuffer[1] = 0x03;
                  memcpy(payloadBuffer.data() + 2, mfgStr, len);
                } else if (descriptorIndex == 2) { // Product
                  wchar_t prodStr[] = L"Emulated Mass Storage";
                  uint8_t len = (uint8_t)(sizeof(prodStr));
                  payloadBuffer[0] = len + 2;
                  payloadBuffer[1] = 0x03;
                  memcpy(payloadBuffer.data() + 2, prodStr, len);
                }
              }
            }
          }

          retSubmit.actualLength = htonl((int32_t)payloadBuffer.size());
          send(clientSocket, (char *)&retSubmit, sizeof(USBIP_RET_SUBMIT), 0);
          send(clientSocket, payloadBuffer.data(), (int)payloadBuffer.size(),
               0);
        } else {
          retSubmit.actualLength = htonl(0);
          send(clientSocket, (char *)&retSubmit, sizeof(USBIP_RET_SUBMIT), 0);
        }
      } else if (cmdType == 0x00000002) { // USBIP_CMD_UNLINK
        struct CMD_UNLINK_TAIL {
          uint32_t unlinkSeqnum;
          uint8_t padding[24];
        } unlinkTail;

        if (!ReceiveExactBytes(clientSocket, (char *)&unlinkTail,
                               sizeof(unlinkTail))) {
          break;
        }

        USBIP_RET_UNLINK retUnlink = {0};
        retUnlink.base.command = htonl(0x00000004);
        retUnlink.base.seqnum = htonl(seqNum);
        retUnlink.status = htonl(0);

        send(clientSocket, (char *)&retUnlink, sizeof(USBIP_RET_UNLINK), 0);
      } else {
        break;
      }
    }
    closesocket(clientSocket);
  } else {
    closesocket(clientSocket);
  }
}