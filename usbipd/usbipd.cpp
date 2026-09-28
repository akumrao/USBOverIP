#include "usbipd.h"
#include "vbox_usb.h"
#include <iostream>

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

// Dynamically registers and starts VBoxUSBMon.sys via SCM on demand
bool EnsureVBoxDriverLoaded() {
  wchar_t exePath[MAX_PATH];
  GetModuleFileNameW(NULL, exePath, MAX_PATH);
  std::wstring dirPath(exePath);
  dirPath = dirPath.substr(0, dirPath.find_last_of(L"\\/"));
  std::wstring sysPath = dirPath + L"\\VBoxUSBMon.sys";

  SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
  if (!hSCM)
    return false;

  SC_HANDLE hService = CreateServiceW(
      hSCM, L"VBoxUSBMon", L"VirtualBox USB Monitor Service",
      SERVICE_ALL_ACCESS, SERVICE_KERNEL_DRIVER, SERVICE_DEMAND_START,
      SERVICE_ERROR_NORMAL, sysPath.c_str(), NULL, NULL, NULL, NULL, NULL);

  if (!hService && GetLastError() == ERROR_SERVICE_EXISTS) {
    hService = OpenServiceW(hSCM, L"VBoxUSBMon", SERVICE_ALL_ACCESS);
  }

  if (hService) {
    StartServiceW(hService, 0, NULL);
    CloseServiceHandle(hService);
  }

  CloseServiceHandle(hSCM);
  return true;
}

bool GetUsbBusAndAddress(uint16_t targetVid, uint16_t targetPid,
                         uint8_t &outBus, uint8_t &outAddress,
                         std::wstring &outPath) {
  HDEVINFO hDevInfo = SetupDiGetClassDevsW(nullptr, L"USB", nullptr,
                                           DIGCF_PRESENT | DIGCF_ALLCLASSES);
  if (hDevInfo == INVALID_HANDLE_VALUE)
    return false;

  SP_DEVINFO_DATA devInfoData;
  devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
  DWORD index = 0;
  bool found = false;

  wchar_t vidStr[16], pidStr[16];
  swprintf_s(vidStr, L"VID_%04X", targetVid);
  swprintf_s(pidStr, L"PID_%04X", targetPid);

  while (SetupDiEnumDeviceInfo(hDevInfo, index++, &devInfoData)) {
    wchar_t instanceId[MAX_DEVICE_ID_LEN];
    if (!SetupDiGetDeviceInstanceIdW(hDevInfo, &devInfoData, instanceId,
                                     MAX_DEVICE_ID_LEN, nullptr))
      continue;

    std::wstring wsId(instanceId);
    if (wsId.find(vidStr) != std::wstring::npos &&
        wsId.find(pidStr) != std::wstring::npos) {
      DWORD address = 0, busNumber = 0;
      SetupDiGetDeviceRegistryPropertyW(hDevInfo, &devInfoData, SPDRP_ADDRESS,
                                        nullptr, (PBYTE)&address,
                                        sizeof(address), nullptr);
      SetupDiGetDeviceRegistryPropertyW(hDevInfo, &devInfoData, SPDRP_BUSNUMBER,
                                        nullptr, (PBYTE)&busNumber,
                                        sizeof(busNumber), nullptr);

      outBus = (uint8_t)busNumber;
      outAddress = (uint8_t)address;
      outPath = wsId;
      found = true;
      break;
    }
  }

  SetupDiDestroyDeviceInfoList(hDevInfo);
  return found;
}

HANDLE OpenVBoxUsbDriver() {
  EnsureVBoxDriverLoaded();

  HANDLE hVBox = CreateFileW(VBOXUSB_DEVICE_NAME, GENERIC_READ | GENERIC_WRITE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

  return hVBox;
}

//bool CaptureDeviceWithVBox(HANDLE hVBox, uint16_t vid, uint16_t pid) {
//  uint8_t realBus = 0, realAddress = 0;
//  if (!GetUsbBusAndAddress(vid, pid, realBus, realAddress)) {
//    std::cerr << "[-] Error: Physical USB device not found on system bus!"
//              << std::endl;
//    return false;
//  }
//
//  std::cout << "[+] Physical Hardware resolved -> Bus: " << (int)realBus
//            << " | Address: " << (int)realAddress << std::endl;
//
//  VBOXUSB_CAPTURE_REQ req = {0};
//  req.vendorId = vid;
//  req.productId = pid;
//  req.busNumber = realBus;
//  req.deviceAddress = realAddress;
//
//  DWORD bytesReturned = 0;
//  BOOL result = DeviceIoControl(hVBox, VBOXUSB_IOCTL_CAPTURE_DEVICE, &req,
//                                sizeof(req), NULL, 0, &bytesReturned, NULL);
//
//  if (!result) {
//    DWORD lastErr = GetLastError();
//    std::cerr << "[-] DeviceIoControl (CAPTURE) failed. Code: " << lastErr
//              << std::endl;
//  }
//
//  return (result == TRUE);
//}
bool CaptureDeviceWithVBox(HANDLE hVBox, uint16_t vid, uint16_t pid) {
  uint8_t realBus = 0, realAddress = 0;
  std::wstring devicePath;

  if (!GetUsbBusAndAddress(vid, pid, realBus, realAddress, devicePath)) {
    std::cerr << "[-] Error: Physical USB device not found on system bus!"
              << std::endl;
    return false;
  }

  std::cout << "[+] Physical Hardware resolved -> Bus: " << (int)realBus
            << " | Address: " << (int)realAddress << std::endl;

  VBOXUSB_CAPTURE_REQ req = {0};
  req.vendorId = vid;
  req.productId = pid;
  req.busNumber = realBus;
  req.deviceAddress = realAddress;
  req.flags = 0;

  //// Copy instance path into the struct
  //wcscpy_s(req.devicePath, 260, devicePath.c_str());

  //DWORD bytesReturned = 0;
  //BOOL result = DeviceIoControl(
  //    hVBox, VBOXUSB_IOCTL_CAPTURE_DEVICE, &req, sizeof(VBOXUSB_CAPTURE_REQ),
  //    &req, sizeof(VBOXUSB_CAPTURE_REQ), &bytesReturned, NULL);

    // Copy instance path into the struct
  wcscpy_s(req.devicePath, 260, devicePath.c_str());

  DWORD bytesReturned = 0;
  BOOL result =
      DeviceIoControl(hVBox, VBOXUSB_IOCTL_CAPTURE_DEVICE, &req,
                      sizeof(VBOXUSB_CAPTURE_REQ), // Input buffer
                      NULL, 0, // Fix: Clear output buffer configurations
                      &bytesReturned, NULL);


  if (!result) {
    DWORD lastErr = GetLastError();
    std::cerr << "[-] DeviceIoControl (CAPTURE) failed. Code: " << lastErr
              << std::endl;
  }

  return (result == TRUE);
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

    HANDLE hVBox = OpenVBoxUsbDriver();
    if (hVBox == INVALID_HANDLE_VALUE) {
      std::cerr << "[-] Error: Failed to open \\\\.\\VBoxUSBMon handle."
                << std::endl;
      closesocket(clientSocket);
      return;
    }

    if (!CaptureDeviceWithVBox(hVBox, 0x0781, 0x5590)) {
      CloseHandle(hVBox);
      closesocket(clientSocket);
      return;
    }

    std::cout << "[+] Hardware successfully captured via VBoxUSBMon!"
              << std::endl;

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

    // Live USB/IP -> VBoxUSBMon Streaming Loop
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

        if (direction == 0 && reqLen > 0) { // OUT Transfer
          if (!ReceiveExactBytes(clientSocket, dataBuffer.data(), reqLen))
            break;
        }

        std::vector<char> ioctlBuffer(sizeof(VBOXUSB_URB_HDR) +
                                      (reqLen > 0 ? reqLen : 0));
        VBOXUSB_URB_HDR *pVBoxUrb = (VBOXUSB_URB_HDR *)ioctlBuffer.data();
        pVBoxUrb->handle = 1;
        pVBoxUrb->endpoint = (direction == 1) ? (0x80 | ep) : ep;
        pVBoxUrb->transferFlags = direction;
        pVBoxUrb->bufferLength = reqLen;

        if (direction == 0 && reqLen > 0) {
          memcpy(ioctlBuffer.data() + sizeof(VBOXUSB_URB_HDR),
                 dataBuffer.data(), reqLen);
        }

        DWORD bytesReturned = 0;
        BOOL ioctlSuccess =
            DeviceIoControl(hVBox, VBOXUSB_IOCTL_SUBMIT_URB, ioctlBuffer.data(),
                            (DWORD)ioctlBuffer.size(), ioctlBuffer.data(),
                            (DWORD)ioctlBuffer.size(), &bytesReturned, NULL);




        int32_t actualTransferred = 0;
        if (ioctlSuccess && bytesReturned >= sizeof(VBOXUSB_URB_HDR)) {
          actualTransferred = pVBoxUrb->bufferLength;
        }

        USBIP_RET_SUBMIT retSubmit = {0};
        retSubmit.base.command = htonl(0x00000003);
        retSubmit.base.seqnum = htonl(seqNum);
        retSubmit.base.devid = basicHeader.devid;
        retSubmit.base.direction = htonl(direction);
        retSubmit.base.ep = basicHeader.ep;
        retSubmit.status = htonl(0);
        retSubmit.actualLength = htonl(actualTransferred);

        send(clientSocket, (char *)&retSubmit, sizeof(USBIP_RET_SUBMIT), 0);
        if (direction == 1 && actualTransferred > 0) {
          send(clientSocket, ioctlBuffer.data() + sizeof(VBOXUSB_URB_HDR),
               actualTransferred, 0);
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

    VBOXUSB_CAPTURE_REQ releaseReq = {0};
    releaseReq.vendorId = 0x0781;
    releaseReq.productId = 0x5590;
    DWORD dummy = 0;
    DeviceIoControl(hVBox, VBOXUSB_IOCTL_RELEASE_DEVICE, &releaseReq,
                    sizeof(releaseReq), NULL, 0, &dummy, NULL);

    CloseHandle(hVBox);
    closesocket(clientSocket);
  } else {
    closesocket(clientSocket);
  }
}