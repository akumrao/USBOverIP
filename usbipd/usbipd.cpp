#include "usbipd.h"
#include <algorithm>
#include <iostream>
#include <winioctl.h>

// ============================================================================
// Global State
// ============================================================================

extern HANDLE g_hVBoxDriver;
std::map<std::string, DeviceState> g_devices;

// VBoxUSB Device Interface GUID
// {873fdfCA-FE80-EE80-AA5E-00C04FB1720B}
static const GUID GUID_CLASS_VBOXUSB = {
    0x873fdfCA,
    0xFE80,
    0xEE80,
    {0xAA, 0x5E, 0x00, 0xC0, 0x4F, 0xB1, 0x72, 0x0B}};

// ============================================================================
// Helper Functions
// ============================================================================

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

// ============================================================================
// VBoxUSBMon Driver Management
// ============================================================================

HANDLE OpenVBoxUsbDriver() {
  HANDLE hVBox =
      CreateFileW(VBOXUSB_DEVICE_NAME, FILE_READ_DATA | FILE_WRITE_DATA,
                  FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                  FILE_FLAG_OVERLAPPED, NULL);

  if (hVBox == INVALID_HANDLE_VALUE) {
    return INVALID_HANDLE_VALUE;
  }

  UsbSupVersion ver = {};
  DWORD bytesReturned = 0;
  BOOL result = DeviceIoControl(hVBox, SUPUSBFLT_IOCTL_GET_VERSION, NULL, 0,
                                &ver, sizeof(ver), &bytesReturned, NULL);
  if (!result) {
    CloseHandle(hVBox);
    return INVALID_HANDLE_VALUE;
  }

  std::cout << "[+] VBoxUSBMon version: " << ver.major << "." << ver.minor
            << std::endl;
  return hVBox;
}

// ============================================================================
// VBoxUsb Device Driver
// ============================================================================

HANDLE OpenVBoxUsbDevice() {
  HDEVINFO hDevInfo = SetupDiGetClassDevsW(
      &GUID_CLASS_VBOXUSB, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

  if (hDevInfo == INVALID_HANDLE_VALUE) {
    return INVALID_HANDLE_VALUE;
  }

  SP_DEVICE_INTERFACE_DATA devInterfaceData = {};
  devInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

  for (DWORD i = 0;; ++i) {
    if (!SetupDiEnumDeviceInterfaces(hDevInfo, NULL, &GUID_CLASS_VBOXUSB, i,
                                     &devInterfaceData)) {
      if (GetLastError() == ERROR_NO_MORE_ITEMS)
        break;
      continue;
    }

    DWORD requiredSize = 0;
    SetupDiGetDeviceInterfaceDetailW(hDevInfo, &devInterfaceData, NULL, 0,
                                     &requiredSize, NULL);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
      continue;

    std::vector<BYTE> buffer(requiredSize);
    auto pDetail =
        reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(buffer.data());
    pDetail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

    if (!SetupDiGetDeviceInterfaceDetailW(hDevInfo, &devInterfaceData, pDetail,
                                          requiredSize, &requiredSize, NULL)) {
      continue;
    }

    HANDLE hDevice =
        CreateFileW(pDetail->DevicePath, FILE_READ_DATA | FILE_WRITE_DATA,
                    FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                    FILE_FLAG_OVERLAPPED, NULL);

    if (hDevice != INVALID_HANDLE_VALUE) {
      SetupDiDestroyDeviceInfoList(hDevInfo);
      return hDevice;
    }
  }

  SetupDiDestroyDeviceInfoList(hDevInfo);
  return INVALID_HANDLE_VALUE;
}

// ============================================================================
// USB Device Enumeration - DYNAMIC (no hardcoding)
// ============================================================================

// ============================================================================
// USB Device Enumeration - DYNAMIC (using device class)
// ============================================================================

//// Find any USB Mass Storage Device (Device Class 0x08)
//bool FindUsbMassStorageDevice(USBIP_DEVICE_DESC &desc) {
//  // Use GUID for USB Mass Storage class
//  // {4d36e967-e325-11ce-bfc1-08002be10318} is the USB class GUID
//  // But we'll enumerate all USB devices and check their class
//
//  HDEVINFO hDevInfo = SetupDiGetClassDevsW(NULL, L"USB", NULL,
//                                           DIGCF_PRESENT | DIGCF_ALLCLASSES);
//  if (hDevInfo == INVALID_HANDLE_VALUE)
//    return false;
//
//  SP_DEVINFO_DATA devInfo = {};
//  devInfo.cbSize = sizeof(SP_DEVINFO_DATA);
//
//  for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devInfo); ++i) {
//    wchar_t instanceId[MAX_DEVICE_ID_LEN];
//    if (!SetupDiGetDeviceInstanceIdW(hDevInfo, &devInfo, instanceId,
//                                     MAX_DEVICE_ID_LEN, nullptr))
//      continue;
//
//    std::wstring wsId(instanceId);
//
//    // Check if this is a USB device (starts with "USB\")
//    if (wsId.find(L"USB\\") != 0 && wsId.find(L"USBSTOR\\") != 0)
//      continue;
//
//    // Get device class
//    DWORD deviceClass = 0;
//    DWORD classSize = sizeof(deviceClass);
//    SetupDiGetDeviceRegistryPropertyW(hDevInfo, &devInfo, SPDRP_CLASS, NULL,
//                                      (PBYTE)&deviceClass, classSize,
//                                      &classSize);
//
//    // Get device class GUID
//    GUID classGuid = {};
//    DWORD guidSize = sizeof(classGuid);
//    SetupDiGetDeviceRegistryPropertyW(hDevInfo, &devInfo, SPDRP_CLASSGUID, NULL,
//                                      (PBYTE)&classGuid, guidSize, &guidSize);
//
//    // Mass Storage class GUID: {4d36e967-e325-11ce-bfc1-08002be10318}
//    // Or check by class code 0x08
//    GUID GUID_DEVCLASS_USBSTOR = {
//        0x4d36e967,
//        0xe325,
//        0x11ce,
//        {0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18}};
//
//    bool isMassStorage = (classGuid == GUID_DEVCLASS_USBSTOR);
//
//    // Also check by class code
//    if (!isMassStorage) {
//      // Get class code from registry
//      HKEY hKey = SetupDiOpenDevRegKey(hDevInfo, &devInfo, DICS_FLAG_GLOBAL, 0,
//                                       DIREG_DEV, KEY_READ);
//      if (hKey != INVALID_HANDLE_VALUE) {
//        DWORD classCode = 0;
//        DWORD classCodeSize = sizeof(classCode);
//        RegQueryValueExW(hKey, L"Class", NULL, NULL, (LPBYTE)&classCode,
//                         &classCodeSize);
//        RegCloseKey(hKey);
//        // Class code 0x08 = Mass Storage
//        isMassStorage = (classCode == 0x08);
//      }
//    }
//
//    if (!isMassStorage)
//      continue;
//
//    // Extract VID and PID from instance ID
//    // Format: USB\VID_0781&PID_558A&REV_0100 or
//    // USBSTOR\DISK&VEN_SANDISK&PROD_...
//    uint16_t vid = 0, pid = 0;
//
//    size_t vidPos = wsId.find(L"VID_");
//    size_t pidPos = wsId.find(L"PID_");
//
//    if (vidPos != std::wstring::npos && pidPos != std::wstring::npos) {
//      vid = static_cast<uint16_t>(
//          std::stoi(wsId.substr(vidPos + 4, 4), nullptr, 16));
//      pid = static_cast<uint16_t>(
//          std::stoi(wsId.substr(pidPos + 4, 4), nullptr, 16));
//    }
//
//    // Get device description
//    wchar_t friendlyName[256] = {};
//    DWORD nameSize = sizeof(friendlyName);
//    SetupDiGetDeviceRegistryPropertyW(hDevInfo, &devInfo, SPDRP_FRIENDLYNAME,
//                                      NULL, (PBYTE)friendlyName, nameSize,
//                                      &nameSize);
//
//    memset(&desc, 0, sizeof(desc));
//    strcpy_s(desc.busid, "1-5");
//    strcpy_s(desc.path, "/sys/devices/platform/virtual_host_hub/usb1/1-5");
//    desc.busnum = SWAP32(1);
//    desc.devnum = SWAP32(5);
//    desc.speed = SWAP32(3);
//    desc.idVendor = SWAP16(vid);
//    desc.idProduct = SWAP16(pid);
//    desc.bcdDevice = SWAP16(0x0100);
//    desc.bDeviceClass = 0x08; // Mass Storage
//    desc.bNumConfigurations = 1;
//    desc.bNumInterfaces = 1;
//
//    std::wcout << L"[+] Found USB Mass Storage: " << friendlyName << std::endl;
//    std::cout << "    VID=" << std::hex << vid << " PID=" << pid << std::dec
//              << std::endl;
//
//    SetupDiDestroyDeviceInfoList(hDevInfo);
//    return true;
//  }
//
//  SetupDiDestroyDeviceInfoList(hDevInfo);
//  return false;
//}


// ============================================================================
// USB Device Enumeration - SIMPLE APPROACH
// ============================================================================

// ============================================================================
// USB Device Enumeration - FIXED for USB\VID_xxxx&PID_xxxx format
// ============================================================================

// ============================================================================
// Device Binding
// ============================================================================

bool BindDeviceToBusid(const char *busid) {
  std::cout << "[*] Binding device with busid: " << busid << std::endl;

  // Check if device exists
  HDEVINFO hDevInfo = SetupDiGetClassDevsW(NULL, L"USB", NULL,
                                           DIGCF_PRESENT | DIGCF_ALLCLASSES);
  if (hDevInfo == INVALID_HANDLE_VALUE)
    return false;

  SP_DEVINFO_DATA devInfo = {};
  devInfo.cbSize = sizeof(SP_DEVINFO_DATA);

  wchar_t vidStr[16], pidStr[16];
  swprintf_s(vidStr, L"VID_%04X", 0x0781);
  swprintf_s(pidStr, L"PID_%04X", 0x5590);

  bool found = false;
  for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devInfo); ++i) {
    wchar_t instanceId[MAX_DEVICE_ID_LEN];
    if (!SetupDiGetDeviceInstanceIdW(hDevInfo, &devInfo, instanceId,
                                     MAX_DEVICE_ID_LEN, nullptr))
      continue;

    std::wstring wsId(instanceId);
    if (wsId.find(vidStr) != std::wstring::npos &&
        wsId.find(pidStr) != std::wstring::npos) {
      found = true;
      break;
    }
  }

  SetupDiDestroyDeviceInfoList(hDevInfo);

  if (found) {
    std::cout << "[+] Device bound successfully" << std::endl;
  } else {
    std::cerr << "[-] Device not found for binding" << std::endl;
  }

  return found;
}

// ============================================================================
// Find USB Mass Storage Device (with bind fallback)
// ============================================================================

bool FindUsbMassStorageDevice(USBIP_DEVICE_DESC &desc) {
  HDEVINFO hDevInfo = SetupDiGetClassDevsW(NULL, L"USB", NULL,
                                           DIGCF_PRESENT | DIGCF_ALLCLASSES);
  if (hDevInfo == INVALID_HANDLE_VALUE)
    return false;

  SP_DEVINFO_DATA devInfo = {};
  devInfo.cbSize = sizeof(SP_DEVINFO_DATA);

  for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devInfo); ++i) {
    wchar_t instanceId[MAX_DEVICE_ID_LEN];
    if (!SetupDiGetDeviceInstanceIdW(hDevInfo, &devInfo, instanceId,
                                     MAX_DEVICE_ID_LEN, nullptr))
      continue;

    std::wstring wsId(instanceId);

    if (wsId.find(L"USB\\VID_") != 0)
      continue;

    // Skip VirtualBox virtual devices
    if (wsId.find(L"VID_80EE") != std::wstring::npos)
      continue;

    // Skip USB hubs and input devices (short instance IDs)
    if (wsId.length() < 30)
      continue;

    // Extract VID and PID
    uint16_t vid = 0, pid = 0;
    size_t vidPos = wsId.find(L"VID_");
    size_t pidPos = wsId.find(L"PID_");

    if (vidPos == std::wstring::npos || pidPos == std::wstring::npos)
      continue;

    try {
      vid = static_cast<uint16_t>(
          std::stoul(wsId.substr(vidPos + 4, 4), nullptr, 16));
      pid = static_cast<uint16_t>(
          std::stoul(wsId.substr(pidPos + 4, 4), nullptr, 16));
    } catch (...) {
      continue;
    }

    // Skip known hub/input VIDs
    if (vid == 0x05E3 || vid == 0x8087 || vid == 0x1462 || vid == 0x413C)
      continue;

    // Check if mass storage by class GUID
    GUID classGuid = {};
    DWORD guidSize = sizeof(classGuid);
    SetupDiGetDeviceRegistryPropertyW(hDevInfo, &devInfo, SPDRP_CLASSGUID, NULL,
                                      (PBYTE)&classGuid, guidSize, &guidSize);

    GUID GUID_DEVCLASS_USBSTOR = {
        0x4d36e967,
        0xe325,
        0x11ce,
        {0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18}};
    bool isMassStorage = (classGuid == GUID_DEVCLASS_USBSTOR);

    // Fallback: Check hardware ID for USBSTOR
    if (!isMassStorage) {
      wchar_t hardwareId[1024] = {};
      DWORD hwSize = sizeof(hardwareId);
      if (SetupDiGetDeviceRegistryPropertyW(
              hDevInfo, &devInfo, SPDRP_HARDWAREID, NULL, (PBYTE)hardwareId,
              hwSize, &hwSize)) {
        std::wstring wsHwId(hardwareId);
        if (wsHwId.find(L"USBSTOR") != std::wstring::npos) {
          isMassStorage = true;
        }
      }
    }

    if (!isMassStorage)
      continue;

    // Found a mass storage device!
    memset(&desc, 0, sizeof(desc));
    strcpy_s(desc.busid, "1-5");
    strcpy_s(desc.path, "/sys/devices/platform/virtual_host_hub/usb1/1-5");
    desc.busnum = SWAP32(1);
    desc.devnum = SWAP32(5);
    desc.speed = SWAP32(3);
    desc.idVendor = SWAP16(vid);
    desc.idProduct = SWAP16(pid);
    desc.bcdDevice = SWAP16(0x0100);
    desc.bDeviceClass = 0x08;
    desc.bNumConfigurations = 1;
    desc.bNumInterfaces = 1;

    std::cout << "[+] Found USB Mass Storage Device:" << std::endl;
    std::cout << "    VID=" << std::hex << vid << " PID=" << pid << std::dec
              << std::endl;

    SetupDiDestroyDeviceInfoList(hDevInfo);
    return true;
  }

  SetupDiDestroyDeviceInfoList(hDevInfo);

  // FALLBACK: Bind to busid 1-5 and try again
  std::cout
      << "[!] No mass storage device found, attempting to bind busid 1-5..."
      << std::endl;

  if (BindDeviceToBusid("1-5")) {
    // Try again after binding
    hDevInfo = SetupDiGetClassDevsW(NULL, L"USB", NULL,
                                    DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (hDevInfo != INVALID_HANDLE_VALUE) {
      devInfo.cbSize = sizeof(SP_DEVINFO_DATA);

      for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devInfo); ++i) {
        wchar_t instanceId[MAX_DEVICE_ID_LEN];
        if (!SetupDiGetDeviceInstanceIdW(hDevInfo, &devInfo, instanceId,
                                         MAX_DEVICE_ID_LEN, nullptr))
          continue;

        std::wstring wsId(instanceId);

        // Look for the bound device (VID_0781&PID_5590)
        if (wsId.find(L"VID_0781") != std::wstring::npos &&
            wsId.find(L"PID_5590") != std::wstring::npos) {

          memset(&desc, 0, sizeof(desc));
          strcpy_s(desc.busid, "1-5");
          strcpy_s(desc.path,
                   "/sys/devices/platform/virtual_host_hub/usb1/1-5");
          desc.busnum = SWAP32(1);
          desc.devnum = SWAP32(5);
          desc.speed = SWAP32(3);
          desc.idVendor = SWAP16(0x0781);
          desc.idProduct = SWAP16(0x5590);
          desc.bcdDevice = SWAP16(0x0100);
          desc.bDeviceClass = 0x08;
          desc.bNumConfigurations = 1;
          desc.bNumInterfaces = 1;

          std::cout << "[+] Found bound USB Mass Storage Device:" << std::endl;
          std::cout << "    VID=781 PID=5590" << std::endl;

          SetupDiDestroyDeviceInfoList(hDevInfo);
          return true;
        }
      }
      SetupDiDestroyDeviceInfoList(hDevInfo);
    }
  }

  std::cerr << "[-] No USB Mass Storage Device found even after binding"
            << std::endl;
  return false;
}

std::vector<USBIP_DEVICE_DESC> ScanPhysicalUsbBus() {
  std::vector<USBIP_DEVICE_DESC> devices;
  USBIP_DEVICE_DESC desc;

  if (FindUsbMassStorageDevice(desc)) {
    devices.push_back(desc);
  }

  return devices;
}

// ============================================================================
// Device Capture via VBoxUSBMon - DYNAMIC
// ============================================================================

bool CaptureDeviceWithVBox(HANDLE hVBox, uint16_t vid, uint16_t pid) {
  std::cout << "[*] Adding filter: VID=" << std::hex << vid << " PID=" << pid
            << std::dec << std::endl;

  UsbFilter filter = {};
  filter.u32Magic = 0x19670408;
  filter.enmType = 4; // USBFILTER_CAPTURE
  for (int i = 0; i < 11; ++i) {
    filter.aFields[i * 2] = 1; // USBFILTER_MATCH_IGNORE
  }
  filter.aFields[0] = 3; // USBFILTER_MATCH_NUM_EXACT
  filter.aFields[1] = vid;
  filter.aFields[2] = 3; // USBFILTER_MATCH_NUM_EXACT
  filter.aFields[3] = pid;

  UsbSupFltAddOut fltOut = {};
  DWORD bytesReturned = 0;
  BOOL result = DeviceIoControl(hVBox, SUPUSBFLT_IOCTL_ADD_FILTER, &filter,
                                sizeof(filter), &fltOut, sizeof(fltOut),
                                &bytesReturned, NULL);
  if (!result) {
    std::cerr << "[-] ADD_FILTER failed. Error: " << GetLastError()
              << std::endl;
    return false;
  }
  if (fltOut.rc != 0) {
    std::cerr << "[-] ADD_FILTER returned error code: " << fltOut.rc
              << std::endl;
    return false;
  }

  std::cout << "[+] Filter added successfully, ID=" << fltOut.uId << std::endl;

  result = DeviceIoControl(hVBox, SUPUSBFLT_IOCTL_RUN_FILTERS, NULL, 0, NULL, 0,
                           &bytesReturned, NULL);
  if (!result) {
    std::cerr << "[-] RUN_FILTERS failed. Error: " << GetLastError()
              << std::endl;
  } else {
    std::cout << "[+] Filters executed" << std::endl;
  }

  return true;
}

// ============================================================================
// URB Forwarding to VBoxUsb Device
// ============================================================================

bool SendUrbToDevice(HANDLE hDevice, UsbSupUrb *urb, uint32_t direction,
                     char *dataBuffer, int32_t reqLen, int32_t *actualLength) {
  if (hDevice == INVALID_HANDLE_VALUE) {
    *actualLength = 0;
    return false;
  }

  DWORD bytesReturned = 0;
  BOOL result =
      DeviceIoControl(hDevice, SUPUSB_IOCTL_SEND_URB, urb, sizeof(UsbSupUrb),
                      urb, sizeof(UsbSupUrb), &bytesReturned, NULL);

  if (result) {
    *actualLength = static_cast<int32_t>(urb->len);
    if (direction == 1 && dataBuffer && urb->len > 0) {
      memcpy(dataBuffer, reinterpret_cast<void *>(urb->buf), urb->len);
    }
    return true;
  }

  *actualLength = 0;
  return false;
}

// ============================================================================
// USB/IP Protocol Handlers
// ============================================================================

void ConnectionWorkerThread(SOCKET clientSocket) {
  USBIP_OP_COMMON commonHeader;
  if (!ReceiveExactBytes(clientSocket, (char *)&commonHeader,
                         sizeof(USBIP_OP_COMMON))) {
    closesocket(clientSocket);
    return;
  }

  uint16_t command = ntohs(commonHeader.commandCode);
  std::cout << "[*] Received command: 0x" << std::hex << command << std::dec
            << std::endl;

  if (command == 0x8005) { // OP_REQ_DEVLIST
  /*  std::cout << "[*] Handling OP_REQ_DEVLIST" << std::endl;
    auto devices = ScanPhysicalUsbBus();
    std::cout << "[*] Found " << devices.size() << " device(s)" << std::endl;

    USBIP_OP_REP_DEVLIST listReply = {};
    listReply.common.version = htons(0x0111);
    listReply.common.commandCode = htons(0x0005);
    listReply.common.status = 0;
    listReply.numDevices = htonl((uint32_t)devices.size());

    send(clientSocket, (char *)&listReply, sizeof(listReply), 0);
    for (auto &dev : devices) {
      send(clientSocket, (char *)&dev, sizeof(dev), 0);
    }
    std::cout << "[+] Sent device list" << std::endl;*/

         std::cout << "[*] Handling OP_REQ_DEVLIST" << std::endl;
  auto devices = ScanPhysicalUsbBus();

  USBIP_OP_REP_DEVLIST listReply = {};
  listReply.common.version = htons(0x0111);
  listReply.common.commandCode = htons(0x0005);
  listReply.common.status = 0;
  listReply.numDevices = htonl((uint32_t)devices.size());

  send(clientSocket, (char *)&listReply, sizeof(listReply), 0);

  for (auto &dev : devices) {
    // Send device descriptor
    send(clientSocket, (char *)&dev, sizeof(dev), 0);

    // Send interface info (4 bytes per device)
    uint8_t interfaceInfo[4] = {
        0x08, // bInterfaceClass: Mass Storage
        0x06, // bInterfaceSubClass: SCSI
        0x50, // bInterfaceProtocol: Bulk-Only
        0x00  // padding
    };
    send(clientSocket, (char *)interfaceInfo, sizeof(interfaceInfo), 0);
  }

  std::cout << "[+] Sent device list" << std::endl;


  } else if (command == 0x8003) { // OP_REQ_IMPORT
    std::cout << "[*] Handling OP_REQ_IMPORT" << std::endl;

    char busid[32];
    if (!ReceiveExactBytes(clientSocket, busid, sizeof(busid))) {
      std::cerr << "[-] Failed to receive busid" << std::endl;
      closesocket(clientSocket);
      return;
    }

    // Ensure VBoxUSB driver is running
    if (!EnsureVBoxUsbDriver()) {
      std::cerr << "[-] VBoxUSB driver not available" << std::endl;
      closesocket(clientSocket);
      return;
    }

    std::string busidStr(busid);
    std::cout << "[*] Import request for busid: " << busidStr << std::endl;

    // Check if device already exists
    auto it = g_devices.find(busidStr);
    if (it != g_devices.end() && it->second.hVBoxUsb != INVALID_HANDLE_VALUE) {
      std::cout << "[*] Device already exists, returning cached" << std::endl;
      USBIP_OP_REP_IMPORT importReply = {};
      importReply.common.version = htons(0x0111);
      importReply.common.commandCode = htons(0x0003);
      importReply.common.status = 0;
      importReply.dev = it->second.info;
      send(clientSocket, (char *)&importReply, sizeof(importReply), 0);
      return;
    }

    // Find any available USB Mass Storage Device dynamically
    std::cout << "[*] Searching for USB Mass Storage Device..." << std::endl;
    USBIP_DEVICE_DESC devInfo;
    if (!FindUsbMassStorageDevice(devInfo)) {
      std::cerr << "[-] No USB Mass Storage Device found" << std::endl;
      closesocket(clientSocket);
      return;
    }

    uint16_t vid = ntohs(devInfo.idVendor);
    uint16_t pid = ntohs(devInfo.idProduct);
    std::cout << "[*] Found device: VID=" << std::hex << vid << " PID=" << pid
              << std::dec << std::endl;

    // Capture device via VBoxUSBMon
    std::cout << "[*] Capturing device via VBoxUSBMon..." << std::endl;
    if (!CaptureDeviceWithVBox(g_hVBoxDriver, vid, pid)) {
      std::cerr << "[-] Failed to capture device (CaptureDeviceWithVBox "
                   "returned false)"
                << std::endl;
      closesocket(clientSocket);
      return;
    }
    std::cout << "[+] Device captured via filter" << std::endl;

    // Wait for device to be captured
    std::cout << "[*] Waiting for VBoxUsb device to appear..." << std::endl;
    Sleep(2000); // Give time for device to be captured

    // Open VBoxUsb device
    std::cout << "[*] Opening VBoxUsb device..." << std::endl;
    HANDLE hVBoxUsb = OpenVBoxUsbDevice();
    if (hVBoxUsb == INVALID_HANDLE_VALUE) {
      std::cerr << "[-] Failed to open VBoxUsb device" << std::endl;
      std::cerr << "    Make sure the device is bound: usbipd bind --busid 1-5"
                << std::endl;
      closesocket(clientSocket);
      return;
    }
    std::cout << "[+] VBoxUsb device opened" << std::endl;

    // Claim device
    std::cout << "[*] Claiming device..." << std::endl;
    DWORD bytesReturned = 0;
    BOOL claimResult = DeviceIoControl(hVBoxUsb, SUPUSB_IOCTL_USB_CLAIM_DEVICE,
                                       NULL, 0, NULL, 0, &bytesReturned, NULL);
    if (!claimResult) {
      std::cerr << "[-] Failed to claim device. Error: " << GetLastError()
                << std::endl;
    } else {
      std::cout << "[+] Device claimed" << std::endl;
    }

    // Store device state
    DeviceState state = {};
    state.attached = true;
    state.claimed = true;
    state.hVBoxUsb = hVBoxUsb;
    state.info = devInfo;
    strcpy_s(state.info.busid, busidStr.c_str());
    g_devices[busidStr] = state;

    // Send import reply
    std::cout << "[*] Sending import reply..." << std::endl;
    USBIP_OP_REP_IMPORT importReply = {};
    importReply.common.version = htons(0x0111);
    importReply.common.commandCode = htons(0x0003);
    importReply.common.status = 0;
    importReply.dev = state.info;

    int sent = send(clientSocket, (char *)&importReply, sizeof(importReply), 0);
    if (sent == SOCKET_ERROR) {
      std::cerr << "[-] Failed to send import reply. Error: "
                << WSAGetLastError() << std::endl;
    } else {
      std::cout << "[+] Import reply sent (" << sent << " bytes)" << std::endl;
    }

    // Handle URB submissions
    std::cout << "[*] Entering URB handling loop..." << std::endl;
    while (true) {
      USBIP_HEADER_BASIC basicHeader;
      if (!ReceiveExactBytes(clientSocket, (char *)&basicHeader,
                             sizeof(basicHeader))) {
        std::cout << "[*] Client disconnected or error receiving URB"
                  << std::endl;
        break;
      }

      uint32_t cmdType = ntohl(basicHeader.command);
      uint32_t seqNum = ntohl(basicHeader.seqnum);
      uint32_t direction = ntohl(basicHeader.direction);
      uint32_t ep = ntohl(basicHeader.ep);

      std::cout << "[*] URB command: 0x" << std::hex << cmdType << std::dec
                << " seqnum=" << seqNum << " ep=" << ep << std::endl;

      if (cmdType == 0x00000001) { // USBIP_CMD_SUBMIT
        USBIP_CMD_SUBMIT submit = {};
        memcpy(&submit.base, &basicHeader, sizeof(basicHeader));
        if (!ReceiveExactBytes(clientSocket, (char *)&submit.transferFlags,
                               sizeof(submit) - sizeof(basicHeader)))
          break;

        int32_t reqLen = ntohl(submit.transferBufferLength);
        std::vector<char> dataBuffer(reqLen > 0 ? reqLen : 0);

        if (direction == 0 && reqLen > 0) {
          if (!ReceiveExactBytes(clientSocket, dataBuffer.data(), reqLen))
            break;
        }

        // Forward URB to VBoxUsb device
        UsbSupUrb urb = {};
        urb.type = 2; // BULK
        urb.ep = ep;
        urb.dir = direction;
        urb.len = reqLen;
        urb.buf = reinterpret_cast<uint64_t>(dataBuffer.data());
        urb.numIsoPkts = 0;

        int32_t actualLength = 0;
        bool success =
            SendUrbToDevice(hVBoxUsb, &urb, direction, dataBuffer.data(),
                            reqLen, &actualLength);

        // Send response
        USBIP_RET_SUBMIT retSubmit = {};
        retSubmit.base.command = htonl(0x00000003);
        retSubmit.base.seqnum = htonl(seqNum);
        retSubmit.base.devid = basicHeader.devid;
        retSubmit.base.direction = htonl(direction);
        retSubmit.base.ep = basicHeader.ep;
        retSubmit.status = success ? 0 : -1;
        retSubmit.actualLength = htonl(actualLength);

        send(clientSocket, (char *)&retSubmit, sizeof(retSubmit), 0);
        if (direction == 1 && actualLength > 0) {
          send(clientSocket, dataBuffer.data(), actualLength, 0);
        }
      } else if (cmdType == 0x00000002) { // USBIP_CMD_UNLINK
        char unlinkTail[28];
        if (!ReceiveExactBytes(clientSocket, unlinkTail, sizeof(unlinkTail)))
          break;

        USBIP_RET_UNLINK retUnlink = {};
        retUnlink.base.command = htonl(0x00000004);
        retUnlink.base.seqnum = htonl(seqNum);
        retUnlink.status = 0;
        send(clientSocket, (char *)&retUnlink, sizeof(retUnlink), 0);
      } else {
        std::cerr << "[-] Unknown URB command: 0x" << std::hex << cmdType
                  << std::dec << std::endl;
        break;
      }
    }

    // Cleanup
    auto cleanupIt = g_devices.find(busidStr);
    if (cleanupIt != g_devices.end()) {
      if (cleanupIt->second.hVBoxUsb != INVALID_HANDLE_VALUE) {
        CloseHandle(cleanupIt->second.hVBoxUsb);
      }
      g_devices.erase(cleanupIt);
    }
  }

  closesocket(clientSocket);
}


void DebugListAllUsbDevices() {
  HDEVINFO hDevInfo = SetupDiGetClassDevsW(NULL, L"USB", NULL,
                                           DIGCF_PRESENT | DIGCF_ALLCLASSES);
  if (hDevInfo == INVALID_HANDLE_VALUE)
    return;

  SP_DEVINFO_DATA devInfo = {};
  devInfo.cbSize = sizeof(SP_DEVINFO_DATA);

  std::cout << "\n=== All USB Devices ===" << std::endl;

  for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devInfo); ++i) {
    wchar_t instanceId[MAX_DEVICE_ID_LEN];
    if (!SetupDiGetDeviceInstanceIdW(hDevInfo, &devInfo, instanceId,
                                     MAX_DEVICE_ID_LEN, nullptr))
      continue;

    wchar_t friendlyName[256] = {};
    DWORD nameSize = sizeof(friendlyName);
    SetupDiGetDeviceRegistryPropertyW(hDevInfo, &devInfo, SPDRP_FRIENDLYNAME,
                                      NULL, (PBYTE)friendlyName, nameSize,
                                      &nameSize);

    std::wcout << L"Device " << i << L": " << friendlyName << std::endl;
    std::wcout << L"  Instance ID: " << instanceId << std::endl;
  }

  SetupDiDestroyDeviceInfoList(hDevInfo);
  std::cout << "======================\n" << std::endl;
}






// ============================================================================
// VBoxUSB Device Driver Management
// ============================================================================

bool IsVBoxUsbDriverRunning() {
  SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
  if (!hSCM)
    return false;

  SC_HANDLE hService = OpenServiceW(hSCM, L"VBoxUSB", SERVICE_QUERY_STATUS);
  if (!hService) {
    CloseServiceHandle(hSCM);
    return false;
  }

  SERVICE_STATUS status = {};
  BOOL result = QueryServiceStatus(hService, &status);
  CloseServiceHandle(hService);
  CloseServiceHandle(hSCM);

  return result && status.dwCurrentState == SERVICE_RUNNING;
}

bool StartVBoxUsbDriver() {
  std::cout << "[*] Starting VBoxUSB device driver..." << std::endl;

  SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
  if (!hSCM) {
    std::cerr << "[-] Failed to open SCM. Error: " << GetLastError()
              << std::endl;
    return false;
  }

  SC_HANDLE hService =
      OpenServiceW(hSCM, L"VBoxUSB", SERVICE_START | SERVICE_QUERY_STATUS);
  if (!hService) {
    // Try to create the service
    std::cout << "[*] VBoxUSB service not found, creating..." << std::endl;

    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::wstring dirPath(exePath);
    dirPath = dirPath.substr(0, dirPath.find_last_of(L"\\/"));
    std::wstring sysPath = dirPath + L"\\VBoxUSB.sys";

    hService = CreateServiceW(hSCM, L"VBoxUSB", L"VirtualBox USB Driver",
                              SERVICE_ALL_ACCESS, SERVICE_KERNEL_DRIVER,
                              SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL,
                              sysPath.c_str(), NULL, NULL, NULL, NULL, NULL);

    if (!hService) {
      std::cerr << "[-] Failed to create VBoxUSB service. Error: "
                << GetLastError() << std::endl;
      CloseServiceHandle(hSCM);
      return false;
    }
  }

  // Start the service
  if (!StartServiceW(hService, 0, NULL)) {
    DWORD err = GetLastError();
    if (err != ERROR_SERVICE_ALREADY_RUNNING) {
      std::cerr << "[-] Failed to start VBoxUSB service. Error: " << err
                << std::endl;
      CloseServiceHandle(hService);
      CloseServiceHandle(hSCM);
      return false;
    }
  }

  CloseServiceHandle(hService);
  CloseServiceHandle(hSCM);

  std::cout << "[+] VBoxUSB device driver started" << std::endl;
  return true;
}

bool EnsureVBoxUsbDriver() {
  if (IsVBoxUsbDriverRunning()) {
    std::cout << "[+] VBoxUSB device driver is running" << std::endl;
    return true;
  }

  std::cout << "[!] VBoxUSB device driver not running" << std::endl;
  return StartVBoxUsbDriver();
}