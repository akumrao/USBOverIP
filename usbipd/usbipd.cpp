#include "usbipd.h"
#include <iostream>
#include <newdev.h>

DEFINE_GUID(GUID_DEVINTERFACE_USB_DEVICE, 0xA5DCBF10, 0x6530, 0x11D2, 0x90,
            0x1F, 0x00, 0xC0, 0x4F, 0xB9, 0x51, 0xED);

bool ReceiveExactBytes(SOCKET s, char *buffer, int bytesToRead) {
  int totalRead = 0;
  while (totalRead < bytesToRead) {
    int bytesRead = recv(s, buffer + totalRead, bytesToRead - totalRead, 0);
    if (bytesRead <= 0)
      return false;
    totalRead += bytesRead;
  }
  return true;
}

bool BindWinUSBToDevice(const std::wstring &hardwareId,
                        const std::wstring &infPath) {
  BOOL rebootRequired = FALSE;
  std::cout << "[+] Swapping driver to WinUSB for "
            << std::string(hardwareId.begin(), hardwareId.end()) << "..."
            << std::endl;
  std::cout << "[*] Using INF Path: "
            << std::string(infPath.begin(), infPath.end()) << std::endl;

  BOOL success = UpdateDriverForPlugAndPlayDevicesW(
      NULL, hardwareId.c_str(), infPath.c_str(),
      INSTALLFLAG_FORCE | INSTALLFLAG_READONLY, &rebootRequired);

  if (!success) {
    DWORD err = GetLastError();
    std::cerr << "[-] Driver Swap Failed! Win32 Error Code: " << err << " (0x"
              << std::hex << err << ")" << std::endl;
    if (err == 0x800F022F || err == 2) {
      std::cerr << "    -> Cause: INF file not found or invalid format."
                << std::endl;
    } else if (err == 0x800F0203) {
      std::cerr << "    -> Cause: Hardware ID in INF does not match the "
                   "connected USB device."
                << std::endl;
    } else if (err == 0x80070005) {
      std::cerr
          << "    -> Cause: Access Denied. Must run server as Administrator."
          << std::endl;
    }
  } else {
    std::cout << "[+] Driver Swap Succeeded!" << std::endl;
  }

  return (success == TRUE);
}

// Opens WinUSB interface handle to route live physical USB traffic
//HANDLE OpenPhysicalWinUSBDevice(WINUSB_INTERFACE_HANDLE *phWinUsb) {
//  HDEVINFO hDevInfo =
//      SetupDiGetClassDevsW(&GUID_DEVINTERFACE_USB_DEVICE, NULL, NULL,
//                           DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
//  if (hDevInfo == INVALID_HANDLE_VALUE)
//    return INVALID_HANDLE_VALUE;
//
//  SP_DEVICE_INTERFACE_DATA interfaceData = {sizeof(SP_DEVICE_INTERFACE_DATA)};
//  DWORD index = 0;
//  HANDLE hDevice = INVALID_HANDLE_VALUE;
//
//  while (SetupDiEnumDeviceInterfaces(
//      hDevInfo, NULL, &GUID_DEVINTERFACE_USB_DEVICE, index++, &interfaceData)) {
//    DWORD detailSize = 0;
//    SetupDiGetDeviceInterfaceDetailW(hDevInfo, &interfaceData, NULL, 0,
//                                     &detailSize, NULL);
//
//    std::vector<char> buffer(detailSize);
//    PSP_DEVICE_INTERFACE_DETAIL_DATA_W pDetail =
//        (PSP_DEVICE_INTERFACE_DETAIL_DATA_W)buffer.data();
//    pDetail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
//
//    if (SetupDiGetDeviceInterfaceDetailW(hDevInfo, &interfaceData, pDetail,
//                                         detailSize, NULL, NULL)) {
//      hDevice = CreateFileW(pDetail->DevicePath, GENERIC_READ | GENERIC_WRITE,
//                            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
//                            OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
//      if (hDevice != INVALID_HANDLE_VALUE) {
//        if (WinUsb_Initialize(hDevice, phWinUsb)) {
//          SetupDiDestroyDeviceInfoList(hDevInfo);
//          return hDevice;
//        }
//        CloseHandle(hDevice);
//      }
//    }
//  }
//
//  SetupDiDestroyDeviceInfoList(hDevInfo);
//  return INVALID_HANDLE_VALUE;
//}

// 2. Update OpenPhysicalWinUSBDevice() with polling retries:
HANDLE OpenPhysicalWinUSBDevice(WINUSB_INTERFACE_HANDLE *phWinUsb) {
  // Retry up to 5 times to account for PnP startup latency
  for (int retry = 0; retry < 5; ++retry) {
    HDEVINFO hDevInfo =
        SetupDiGetClassDevsW(&GUID_DEVINTERFACE_USB_DEVICE, NULL, NULL,
                             DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (hDevInfo == INVALID_HANDLE_VALUE) {
      Sleep(500);
      continue;
    }

    SP_DEVICE_INTERFACE_DATA interfaceData = {sizeof(SP_DEVICE_INTERFACE_DATA)};
    DWORD index = 0;

    while (SetupDiEnumDeviceInterfaces(hDevInfo, NULL,
                                       &GUID_DEVINTERFACE_USB_DEVICE, index++,
                                       &interfaceData)) {
      DWORD detailSize = 0;
      SetupDiGetDeviceInterfaceDetailW(hDevInfo, &interfaceData, NULL, 0,
                                       &detailSize, NULL);

      std::vector<char> buffer(detailSize);
      PSP_DEVICE_INTERFACE_DETAIL_DATA_W pDetail =
          (PSP_DEVICE_INTERFACE_DETAIL_DATA_W)buffer.data();
      pDetail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

      if (SetupDiGetDeviceInterfaceDetailW(hDevInfo, &interfaceData, pDetail,
                                           detailSize, NULL, NULL)) {
        HANDLE hDevice =
            CreateFileW(pDetail->DevicePath, GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                        FILE_FLAG_OVERLAPPED, NULL);
        if (hDevice != INVALID_HANDLE_VALUE) {
          if (WinUsb_Initialize(hDevice, phWinUsb)) {
            SetupDiDestroyDeviceInfoList(hDevInfo);
            return hDevice; // Successfully opened WinUSB handle
          }
          CloseHandle(hDevice);
        }
      }
    }
    SetupDiDestroyDeviceInfoList(hDevInfo);
    Sleep(500); // Wait before attempting next retry
  }

  return INVALID_HANDLE_VALUE;
}

std::vector<USBIP_DEVICE_DESC> ScanPhysicalUsbBus() {
  std::vector<USBIP_DEVICE_DESC> detectedDevices;
  USBIP_DEVICE_DESC devExchange = {0};

  strcpy_s(devExchange.path, "/sys/devices/platform/virtual_host_hub/usb1/1-1");
  strcpy_s(devExchange.busid, "1-1");
  devExchange.busnum = SWAP32(1);
  devExchange.devnum = SWAP32(2);
  devExchange.speed = SWAP32(3);          // High Speed
  devExchange.idVendor = SWAP16(0x0781);  // SanDisk VID
  devExchange.idProduct = SWAP16(0x5590); // Product PID
  devExchange.bcdDevice = SWAP16(0x0100);
  devExchange.bDeviceClass = 0x00;
  devExchange.bNumConfigurations = 1;
  devExchange.bNumInterfaces = 1;

  detectedDevices.push_back(devExchange);
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
  std::cout << "[*] Handshake Opcode: 0x" << std::hex << evaluatedCommand
            << std::endl;

  if (evaluatedCommand == 0x8005) { // OP_REQ_DEVLIST
    std::vector<USBIP_DEVICE_DESC> activeList = ScanPhysicalUsbBus();
    USBIP_OP_REP_DEVLIST listReply = {0};
    listReply.common.version = SWAP16(0x0111);
    listReply.common.commandCode = SWAP16(0x0005);
    listReply.common.status = SWAP32(0);
    listReply.numDevices = SWAP32((uint32_t)activeList.size());

    send(clientSocket, (char *)&listReply, sizeof(USBIP_OP_REP_DEVLIST), 0);
    for (auto &device : activeList) {
      send(clientSocket, (char *)&device, sizeof(USBIP_DEVICE_DESC), 0);
      uint8_t interfaceSubBlock[] = {0x00, 0x00, 0x00, 0x00};
      send(clientSocket, (char *)interfaceSubBlock, sizeof(interfaceSubBlock),
           0);
    }
    closesocket(clientSocket);
  } else if (evaluatedCommand == 0x8003) { // OP_REQ_IMPORT
    char busidReq[32];
    if (!ReceiveExactBytes(clientSocket, busidReq, sizeof(busidReq))) {
      closesocket(clientSocket);
      return;
    }

    // Force driver swap to WinUSB on host machine
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::wstring infPath(exePath);
    infPath = infPath.substr(0, infPath.find_last_of(L"\\/")) + L"\\winusb.inf";

    BindWinUSBToDevice(L"USB\\VID_0781&PID_5590", infPath);

    Sleep(1000);

    // Initialize Physical WinUSB Handle
    WINUSB_INTERFACE_HANDLE hWinUsb = NULL;
    HANDLE hDevice = OpenPhysicalWinUSBDevice(&hWinUsb);

    if (hDevice == INVALID_HANDLE_VALUE) {
      std::cerr
          << "[-] Error: Failed to open WinUSB handle to physical USB device."
          << std::endl;
      closesocket(clientSocket);
      return;
    }

    std::cout << "[+] Physical USB Hardware Bridge Engaged!" << std::endl;

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

    send(clientSocket, (char *)&importReply, sizeof(USBIP_OP_REP_IMPORT), 0);

    // Dynamic Hardware Streaming Loop
    while (true) {
      USBIP_HEADER_BASIC basicHeader;
      if (!ReceiveExactBytes(clientSocket, (char *)&basicHeader,
                             sizeof(USBIP_HEADER_BASIC)))
        break;

      uint32_t cmdType = ntohl(basicHeader.command);
      uint32_t seqNum = ntohl(basicHeader.seqnum);
      uint32_t direction = ntohl(basicHeader.direction);
      uint32_t ep = ntohl(basicHeader.ep);

      if (cmdType == 0x00000001) { // USBIP_CMD_SUBMIT
        struct CMD_TAIL {
          uint32_t transferFlags;
          int32_t transferBufferLength;
          uint32_t startFrame;
          uint32_t numberOfPackets;
          uint32_t interval;
          uint8_t setup[8];
        } tail;

        if (!ReceiveExactBytes(clientSocket, (char *)&tail, sizeof(tail)))
          break;
        int32_t reqLen = ntohl(tail.transferBufferLength);

        std::vector<char> dataBuffer(reqLen > 0 ? reqLen : 0);

        // Host-to-Device Payload
        if (direction == 0 && reqLen > 0) {
          if (!ReceiveExactBytes(clientSocket, dataBuffer.data(), reqLen))
            break;
        }

        ULONG bytesTransferred = 0;

        // Route Control Endpoint (EP0) directly to hardware
        if (ep == 0) {
          WINUSB_SETUP_PACKET setupPacket;
          memcpy(&setupPacket, tail.setup, 8);

          WinUsb_ControlTransfer(hWinUsb, setupPacket,
                                 (PUCHAR)dataBuffer.data(), reqLen,
                                 &bytesTransferred, NULL);
        }
        // Route Bulk Endpoints (EP1, EP2, etc.) directly to hardware
        else {
          UCHAR pipeID = (direction == 1) ? (0x80 | (UCHAR)ep) : (UCHAR)ep;

          if (direction == 1) { // Bulk IN Read
            WinUsb_ReadPipe(hWinUsb, pipeID, (PUCHAR)dataBuffer.data(), reqLen,
                            &bytesTransferred, NULL);
          } else { // Bulk OUT Write
            WinUsb_WritePipe(hWinUsb, pipeID, (PUCHAR)dataBuffer.data(), reqLen,
                             &bytesTransferred, NULL);
          }
        }

        USBIP_RET_SUBMIT retSubmit = {0};
        retSubmit.base.command = htonl(0x00000003);
        retSubmit.base.seqnum = htonl(seqNum);
        retSubmit.base.devid = basicHeader.devid;
        retSubmit.base.direction = htonl(direction);
        retSubmit.base.ep = basicHeader.ep;
        retSubmit.status = htonl(0);
        retSubmit.actualLength = htonl((int32_t)bytesTransferred);

        send(clientSocket, (char *)&retSubmit, sizeof(USBIP_RET_SUBMIT), 0);
        if (direction == 1 && bytesTransferred > 0) {
          send(clientSocket, dataBuffer.data(), (int)bytesTransferred, 0);
        }
      } else if (cmdType == 0x00000002) { // USBIP_CMD_UNLINK
        char unlinkTail[28];
        if (!ReceiveExactBytes(clientSocket, unlinkTail, sizeof(unlinkTail)))
          break;

        USBIP_RET_UNLINK retUnlink = {0};
        retUnlink.base.command = htonl(0x00000004);
        retUnlink.base.seqnum = htonl(seqNum);
        retUnlink.status = htonl(0);
        send(clientSocket, (char *)&retUnlink, sizeof(USBIP_RET_UNLINK), 0);
      } else {
        break;
      }
    }

    WinUsb_Free(hWinUsb);
    CloseHandle(hDevice);
    closesocket(clientSocket);
  } else {
    closesocket(clientSocket);
  }
}