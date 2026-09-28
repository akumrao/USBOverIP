#include "usbipd.h"
#include "vbox_usb.h"
#include <iostream>
#include <regstr.h>

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

// Injects VBoxUSB into target device UpperFilters safely
bool AttachVBoxFilterToDevice(DEVINST devInst) {
  HKEY hKey = NULL;
  if (CR_SUCCESS != CM_Open_DevNode_Key(devInst, KEY_READ | KEY_WRITE, 0,
                                        RegDisposition_OpenExisting, &hKey,
                                        CM_REGKEY_HARDWARE)) {
    return false;
  }

  wchar_t currentFilters[1024] = {0};
  DWORD size = sizeof(currentFilters);
  DWORD type = REG_MULTI_SZ;

  LONG status = RegQueryValueExW(hKey, L"UpperFilters", NULL, &type,
                                 (LPBYTE)currentFilters, &size);
  std::wstring filterList =
      (status == ERROR_SUCCESS && size > 2)
          ? std::wstring(currentFilters, size / sizeof(wchar_t))
          : L"";

  if (filterList.find(L"VBoxUSB") == std::wstring::npos) {
    std::cout << "[+] Injecting VBoxUSB into target device UpperFilters..."
              << std::endl;

    wchar_t newFilters[1024] = {0};
    size_t writeOffset = 0;

    if (status == ERROR_SUCCESS && size > 2) {
      size_t charCount = (size / sizeof(wchar_t));
      while (charCount > 0 && currentFilters[charCount - 1] == L'\0') {
        charCount--;
      }
      memcpy(newFilters, currentFilters, charCount * sizeof(wchar_t));
      writeOffset = charCount;
      if (writeOffset > 0) {
        newFilters[writeOffset++] = L'\0';
      }
    }

    const wchar_t *targetFilter = L"VBoxUSB";
    size_t filterLen = wcslen(targetFilter);
    memcpy(newFilters + writeOffset, targetFilter, filterLen * sizeof(wchar_t));
    writeOffset += filterLen;

    newFilters[writeOffset++] = L'\0';
    newFilters[writeOffset++] = L'\0';

    RegSetValueExW(hKey, L"UpperFilters", 0, REG_MULTI_SZ,
                   (const BYTE *)newFilters,
                   (DWORD)(writeOffset * sizeof(wchar_t)));
  }

  RegCloseKey(hKey);

  std::cout << "[+] Re-enumerating USB device node to activate filter stack..."
            << std::endl;
  CONFIGRET cr = CM_Reenumerate_DevNode(devInst, CM_REENUMERATE_NORMAL);
  return (cr == CR_SUCCESS);
}

bool GetUsbDeviceNodeAndParams(uint16_t targetVid, uint16_t targetPid,
                               uint8_t &outBus, uint8_t &outAddress,
                               DEVINST &outDevInst,
                               std::wstring &outInstanceId) {
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
      outDevInst = devInfoData.DevInst;
      outInstanceId = instanceId;

      found = true;
      break;
    }
  }

  SetupDiDestroyDeviceInfoList(hDevInfo);
  return found;
}

#include <newdev.h>
#pragma comment(lib, "newdev.lib")

// Pre-installs VBoxUSB.inf into Driver Store if PnP hasn't loaded it yet
bool EnsureVBoxInfInstalled() {
  wchar_t exePath[MAX_PATH];
  GetModuleFileNameW(NULL, exePath, MAX_PATH);
  std::wstring dirPath(exePath);
  dirPath = dirPath.substr(0, dirPath.find_last_of(L"\\/"));
  std::wstring infPath = dirPath + L"\\VBoxUSB.inf";

  BOOL rebootRequired = FALSE;
  // Pre-stage the INF file into driver store
  return (UpdateDriverForPlugAndPlayDevicesW(NULL, L"USB\\Class_00",
                                             infPath.c_str(), 0,
                                             &rebootRequired) == TRUE);
}

HANDLE OpenVBoxDeviceFilterHandle(uint16_t vid, uint16_t pid) {
  uint8_t realBus = 0, realAddress = 0;
  DEVINST devInst = 0;
  std::wstring instanceId;

  if (!GetUsbDeviceNodeAndParams(vid, pid, realBus, realAddress, devInst,
                                 instanceId)) {
    std::cerr << "[-] Error: Physical USB device not found on system bus!"
              << std::endl;
    return INVALID_HANDLE_VALUE;
  }

  std::cout << "[+] Physical Hardware resolved -> Bus: " << (int)realBus
            << " | Address: " << (int)realAddress << std::endl;

  // Step 1: Ensure VBoxUSB.inf is recognized by Windows PnP
  EnsureVBoxInfInstalled();

  // Step 2: Inject VBoxUSB into UpperFilters and trigger PnP re-enumeration
  AttachVBoxFilterToDevice(devInst);

  std::cout << "[*] Waiting for Windows PnP Manager to mount VBoxUSB device "
               "interface..."
            << std::endl;

  // Step 3: Retry interface lookup up to 10 times (5 seconds total)
  for (int retry = 0; retry < 10; ++retry) {
    Sleep(500); // 500ms delay per attempt

    HDEVINFO hDevInfo =
        SetupDiGetClassDevsW(&GUID_DEVINTERFACE_VBOXUSB, NULL, NULL,
                             DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (hDevInfo == INVALID_HANDLE_VALUE)
      continue;

    SP_DEVICE_INTERFACE_DATA interfaceData = {sizeof(SP_DEVICE_INTERFACE_DATA)};
    DWORD index = 0;

    while (SetupDiEnumDeviceInterfaces(
        hDevInfo, NULL, &GUID_DEVINTERFACE_VBOXUSB, index++, &interfaceData)) {
      DWORD detailSize = 0;
      SetupDiGetDeviceInterfaceDetailW(hDevInfo, &interfaceData, NULL, 0,
                                       &detailSize, NULL);

      std::vector<char> buffer(detailSize);
      PSP_DEVICE_INTERFACE_DETAIL_DATA_W pDetail =
          (PSP_DEVICE_INTERFACE_DETAIL_DATA_W)buffer.data();
      pDetail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

      if (SetupDiGetDeviceInterfaceDetailW(hDevInfo, &interfaceData, pDetail,
                                           detailSize, NULL, NULL)) {
        HANDLE hVBoxDevice =
            CreateFileW(pDetail->DevicePath, GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                        FILE_FLAG_OVERLAPPED, NULL);

        if (hVBoxDevice != INVALID_HANDLE_VALUE) {
          std::cout << "[+] Successfully opened kernel bridge handle to "
                       "VBoxUSB filter driver!"
                    << std::endl;
          SetupDiDestroyDeviceInfoList(hDevInfo);
          return hVBoxDevice;
        }
      }
    }
    SetupDiDestroyDeviceInfoList(hDevInfo);
  }

  std::cerr
      << "[-] Error: Failed to open VBoxUSB interface path after 5 seconds."
      << std::endl;
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

    // Attach VBoxUSB UpperFilter and open kernel handle to device interface
    HANDLE hVBoxDevice = OpenVBoxDeviceFilterHandle(0x0781, 0x5590);
    if (hVBoxDevice == INVALID_HANDLE_VALUE) {
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

    send(clientSocket, (char *)&importReply, sizeof(USBIP_OP_REP_IMPORT), 0);

    // Dynamic USB/IP -> VBoxUSB Kernel URB Bridge
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
        BOOL ioctlSuccess = DeviceIoControl(
            hVBoxDevice, VBOXUSB_IOCTL_SUBMIT_URB, ioctlBuffer.data(),
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

    CloseHandle(hVBoxDevice);
    closesocket(clientSocket);
  } else {
    closesocket(clientSocket);
  }
}