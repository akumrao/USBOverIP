#include "usbipd.h"
#include <algorithm>
#include <iomanip>
#include <iostream>

// ============================================================================
// Global Variables
// ============================================================================

HANDLE g_hVBoxDriver = INVALID_HANDLE_VALUE;
std::map<std::string, DeviceState> g_devices;

// VBoxUSB Device Interface GUID
// {873fdfCA-FE80-EE80-AA5E-00C04FB1720B}
static const GUID GUID_CLASS_VBOXUSB = {
    0x873fdfCA,
    0xFE80,
    0xEE80,
    {0xAA, 0x5E, 0x00, 0xC0, 0x4F, 0xB1, 0x72, 0x0B}};

// ============================================================================
// Utility Functions
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

void UsbSupUrbInit(UsbSupUrb *urb) {
  memset(urb, 0, sizeof(UsbSupUrb));
  urb->type = USBSUP_TRANSFER_TYPE_BULK;
  urb->flags = USBSUP_FLAG_NONE;
}

// ============================================================================
// VBoxUsbMon Filter Helpers
// ============================================================================

UsbFilter CreateUsbFilter(UsbFilterType type) {
  UsbFilter filter = {};
  filter.u32Magic = 0x19670408; // USBFILTER_MAGIC
  filter.enmType = type;
  for (int i = 0; i < static_cast<int>(USBFILTER_IDX_END); ++i) {
    filter.aFields[i * 2] = USBFILTER_MATCH_IGNORE;
  }
  return filter;
}

void SetFilterMatch(UsbFilter &filter, UsbFilterIdx index, UsbFilterMatch match,
                    uint16_t value) {
  filter.aFields[static_cast<int>(index) * 2] = match;
  filter.aFields[static_cast<int>(index) * 2 + 1] = value;
}

// ============================================================================
// VBoxUsbMon Driver Management
// ============================================================================

HANDLE OpenVBoxUsbDriver() {
  HANDLE hVBox =
      CreateFileW(VBOXUSB_DEVICE_NAME, FILE_READ_DATA | FILE_WRITE_DATA,
                  FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                  FILE_FLAG_OVERLAPPED, NULL);

  if (hVBox == INVALID_HANDLE_VALUE) {
    return INVALID_HANDLE_VALUE;
  }

  UsbSupVersion ver = VBoxUsbMonGetVersion(hVBox);
  if (ver.major == 0 && ver.minor == 0) {
    CloseHandle(hVBox);
    return INVALID_HANDLE_VALUE;
  }

  std::cout << "[+] VBoxUSBMon version: " << ver.major << "." << ver.minor
            << std::endl;
  return hVBox;
}

UsbSupVersion VBoxUsbMonGetVersion(HANDLE hVBoxMon) {
  UsbSupVersion ver = {};
  DWORD bytesReturned = 0;
  BOOL result = DeviceIoControl(hVBoxMon, SUPUSBFLT_IOCTL_GET_VERSION, NULL, 0,
                                &ver, sizeof(ver), &bytesReturned, NULL);
  if (!result) {
    std::cerr << "[-] GET_VERSION failed. Error: " << GetLastError()
              << std::endl;
  }
  return ver;
}

uint64_t VBoxUsbMonAddFilter(HANDLE hVBoxMon, uint16_t vid, uint16_t pid,
                             uint8_t port) {
  UsbFilter filter = CreateUsbFilter(USBFILTER_CAPTURE);

  SetFilterMatch(filter, USBFILTER_IDX_VENDOR_ID, USBFILTER_MATCH_NUM_EXACT,
                 vid);
  SetFilterMatch(filter, USBFILTER_IDX_PRODUCT_ID, USBFILTER_MATCH_NUM_EXACT,
                 pid);
  SetFilterMatch(filter, USBFILTER_IDX_DEVICE_REV, USBFILTER_MATCH_NUM_EXACT,
                 0x0100);
  SetFilterMatch(filter, USBFILTER_IDX_DEVICE_CLASS, USBFILTER_MATCH_NUM_EXACT,
                 0x08);
  SetFilterMatch(filter, USBFILTER_IDX_DEVICE_SUB_CLASS,
                 USBFILTER_MATCH_NUM_EXACT, 0x06);
  SetFilterMatch(filter, USBFILTER_IDX_DEVICE_PROTOCOL,
                 USBFILTER_MATCH_NUM_EXACT, 0x50);
  SetFilterMatch(filter, USBFILTER_IDX_PORT, USBFILTER_MATCH_NUM_EXACT, port);

  UsbSupFltAddOut fltOut = {};
  DWORD bytesReturned = 0;
  BOOL result = DeviceIoControl(hVBoxMon, SUPUSBFLT_IOCTL_ADD_FILTER, &filter,
                                sizeof(filter), &fltOut, sizeof(fltOut),
                                &bytesReturned, NULL);
  if (!result || fltOut.rc != 0) {
    std::cerr << "[-] ADD_FILTER failed. RC: " << fltOut.rc << std::endl;
    return 0;
  }

  return fltOut.uId;
}

bool VBoxUsbMonRemoveFilter(HANDLE hVBoxMon, uint64_t filterId) {
  int32_t rc = -1;
  DWORD bytesReturned = 0;
  BOOL result =
      DeviceIoControl(hVBoxMon, SUPUSBFLT_IOCTL_REMOVE_FILTER, &filterId,
                      sizeof(filterId), &rc, sizeof(rc), &bytesReturned, NULL);
  return result && rc == 0;
}

bool VBoxUsbMonRunFilters(HANDLE hVBoxMon) {
  DWORD bytesReturned = 0;
  return DeviceIoControl(hVBoxMon, SUPUSBFLT_IOCTL_RUN_FILTERS, NULL, 0, NULL,
                         0, &bytesReturned, NULL) != 0;
}

// ============================================================================
// VBoxUsb Device Management
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
      DWORD err = GetLastError();
      if (err == ERROR_NO_MORE_ITEMS) {
        std::cerr << "[-] No VBoxUSB device interface found" << std::endl;
      }
      break;
    }

    DWORD requiredSize = 0;
    SetupDiGetDeviceInterfaceDetailW(hDevInfo, &devInterfaceData, NULL, 0,
                                     &requiredSize, NULL);

    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
      continue;

    std::vector<BYTE> buffer(requiredSize);
    PSP_DEVICE_INTERFACE_DETAIL_DATA_W pDetailData =
        reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(buffer.data());
    pDetailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

    if (!SetupDiGetDeviceInterfaceDetailW(hDevInfo, &devInterfaceData,
                                          pDetailData, requiredSize,
                                          &requiredSize, NULL)) {
      continue;
    }

    std::wcout << L"[+] Found VBoxUSB device: " << pDetailData->DevicePath
               << std::endl;

    HANDLE hDevice =
        CreateFileW(pDetailData->DevicePath, FILE_READ_DATA | FILE_WRITE_DATA,
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

UsbSupVersion VBoxUsbGetVersion(HANDLE hVBoxUsb) {
  UsbSupVersion ver = {};
  DWORD bytesReturned = 0;
  DeviceIoControl(hVBoxUsb, SUPUSB_IOCTL_GET_VERSION, NULL, 0, &ver,
                  sizeof(ver), &bytesReturned, NULL);
  return ver;
}

bool VBoxUsbClaimDevice(HANDLE hVBoxUsb) {
  DWORD bytesReturned = 0;
  return DeviceIoControl(hVBoxUsb, SUPUSB_IOCTL_USB_CLAIM_DEVICE, NULL, 0, NULL,
                         0, &bytesReturned, NULL) != 0;
}

bool VBoxUsbReleaseDevice(HANDLE hVBoxUsb) {
  DWORD bytesReturned = 0;
  return DeviceIoControl(hVBoxUsb, SUPUSB_IOCTL_USB_RELEASE_DEVICE, NULL, 0,
                         NULL, 0, &bytesReturned, NULL) != 0;
}

bool VBoxUsbIsOperational(HANDLE hVBoxUsb) {
  uint32_t operational = 0;
  DWORD bytesReturned = 0;
  DeviceIoControl(hVBoxUsb, SUPUSB_IOCTL_IS_OPERATIONAL, NULL, 0, &operational,
                  sizeof(operational), &bytesReturned, NULL);
  return operational != 0;
}

bool VBoxUsbSendUrb(HANDLE hVBoxUsb, UsbSupUrb *urb, uint32_t direction,
                    char *dataBuffer, int32_t reqLen, int32_t *actualLength) {
  if (hVBoxUsb == INVALID_HANDLE_VALUE) {
    *actualLength = 0;
    return false;
  }

  DWORD bytesReturned = 0;
  BOOL result =
      DeviceIoControl(hVBoxUsb, SUPUSB_IOCTL_SEND_URB, urb, sizeof(UsbSupUrb),
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
// USB Device Enumeration
// ============================================================================
// Helper function to parse 4 hex characters from wide string
// Add this GUID definition
DEFINE_GUID(GUID_DEVCLASS_USB, 0x36fc9e60, 0xc465, 0x11cf, 0x80, 0x56, 0x44,
            0x45, 0x53, 0x54, 0x00, 0x00);

// Registry check for authorized devices
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

//std::vector<UsbDeviceInfo> ListUsbDevices() {
//  std::vector<UsbDeviceInfo> devices;
//
//  // Use GUID_DEVCLASS_USB to only enumerate USB devices
//  HDEVINFO hDevInfo =
//      SetupDiGetClassDevsW(&GUID_DEVCLASS_USB, nullptr, nullptr, DIGCF_PRESENT);
//  if (hDevInfo == INVALID_HANDLE_VALUE)
//    return devices;
//
//  SP_DEVINFO_DATA devInfoData = {};
//  devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
//
//  for (DWORD index = 0; SetupDiEnumDeviceInfo(hDevInfo, index, &devInfoData);
//       ++index) {
//    UsbDeviceInfo info = {};
//
//    wchar_t instanceId[MAX_DEVICE_ID_LEN];
//    if (!SetupDiGetDeviceInstanceIdW(hDevInfo, &devInfoData, instanceId,
//                                     MAX_DEVICE_ID_LEN, nullptr))
//      continue;
//
//    std::wstring wsInstanceId(instanceId);
//
//    // Only process USB devices with VID/PID
//    if (wsInstanceId.find(L"USB\\VID_") != 0)
//      continue;
//
//    // Extract VID and PID using std::stoul (works correctly)
//    size_t vidPos = wsInstanceId.find(L"VID_");
//    size_t pidPos = wsInstanceId.find(L"PID_");
//    if (vidPos == std::wstring::npos || pidPos == std::wstring::npos)
//      continue;
//
//    std::wstring vidStr = wsInstanceId.substr(vidPos + 4, 4);
//    std::wstring pidStr = wsInstanceId.substr(pidPos + 4, 4);
//
//    try {
//      info.vid = (uint16_t)std::stoul(vidStr, nullptr, 16);
//      info.pid = (uint16_t)std::stoul(pidStr, nullptr, 16);
//    } catch (...) {
//      continue;
//    }
//
//    // Get friendly name
//    wchar_t friendlyName[256] = {};
//    DWORD nameSize = sizeof(friendlyName);
//    if (SetupDiGetDeviceRegistryPropertyW(
//            hDevInfo, &devInfoData, SPDRP_FRIENDLYNAME, NULL,
//            (PBYTE)friendlyName, nameSize, &nameSize)) {
//      char ansiName[256] = {};
//      WideCharToMultiByte(CP_ACP, 0, friendlyName, -1, ansiName, 256, NULL,
//                          NULL);
//      info.description = ansiName;
//    }
//
//    // Check if authorized for sharing
//    std::string rawHardwareStr(wsInstanceId.begin(), wsInstanceId.end());
//    bool isAuthorized = IsDeviceAuthorizedInRegistry(rawHardwareStr);
//
//    // Check if mass storage by class GUID
//    GUID classGuid = {};
//    DWORD guidSize = sizeof(classGuid);
//    SetupDiGetDeviceRegistryPropertyW(hDevInfo, &devInfoData, SPDRP_CLASSGUID,
//                                      NULL, (PBYTE)&classGuid, guidSize,
//                                      &guidSize);
//
//    GUID GUID_DEVCLASS_USBSTOR = {
//        0x4d36e967,
//        0xe325,
//        0x11ce,
//        {0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18}};
//    info.isMassStorage = (classGuid == GUID_DEVCLASS_USBSTOR);
//
//    // Fallback: Check hardware ID for USBSTOR
//    if (!info.isMassStorage) {
//      wchar_t hardwareId[1024] = {};
//      DWORD hwSize = sizeof(hardwareId);
//      if (SetupDiGetDeviceRegistryPropertyW(
//              hDevInfo, &devInfoData, SPDRP_HARDWAREID, NULL, (PBYTE)hardwareId,
//              hwSize, &hwSize)) {
//        std::wstring wsHwId(hardwareId);
//        if (wsHwId.find(L"USBSTOR") != std::wstring::npos) {
//          info.isMassStorage = true;
//        }
//      }
//    }
//
//    // Generate busid
//    size_t lastBackslash = wsInstanceId.find_last_of(L'\\');
//    if (lastBackslash != std::wstring::npos) {
//      std::wstring serial = wsInstanceId.substr(lastBackslash + 1);
//      std::string busid;
//      for (wchar_t c : serial) {
//        if (isalnum(c))
//          busid += static_cast<char>(c);
//      }
//      if (busid.length() > 8)
//        busid = busid.substr(0, 8);
//      info.busid = busid;
//    }
//
//    std::cout << "[+] Found Device -> VID: " << std::hex << info.vid
//              << " PID: " << info.pid;
//    if (isAuthorized) {
//      std::cout << " [AUTHORIZED FOR SHARE]";
//    } else {
//      std::cout << " [LOCAL ONLY]";
//    }
//    if (info.isMassStorage) {
//      std::cout << " [MASS STORAGE]";
//    }
//    std::cout << std::dec << std::endl;
//
//    devices.push_back(info);
//  }
//
//  SetupDiDestroyDeviceInfoList(hDevInfo);
//  return devices;
//}

// Helper function to check if device is mass storage
bool IsMassStorageDevice(HDEVINFO hDevInfo, SP_DEVINFO_DATA &devInfoData) {
  // Method 1: Check device class code from registry (most reliable)
  HKEY hKey = SetupDiOpenDevRegKey(hDevInfo, &devInfoData, DICS_FLAG_GLOBAL, 0,
                                   DIREG_DEV, KEY_READ);
  if (hKey != INVALID_HANDLE_VALUE) {
    wchar_t classStr[256] = {};
    DWORD classSize = sizeof(classStr);
    if (RegQueryValueExW(hKey, L"Class", NULL, NULL, (LPBYTE)classStr,
                         &classSize) == ERROR_SUCCESS) {
      // Mass Storage class name is "USB" or check ClassGUID
      if (wcscmp(classStr, L"USB") == 0) {
        // Additional check: look for mass storage subclass
        wchar_t classGuidStr[256] = {};
        DWORD guidSize = sizeof(classGuidStr);
        if (RegQueryValueExW(hKey, L"ClassGUID", NULL, NULL,
                             (LPBYTE)classGuidStr,
                             &guidSize) == ERROR_SUCCESS) {
          // Mass Storage GUID: {4d36e967-e325-11ce-bfc1-08002be10318}
          if (wcscmp(classGuidStr, L"{4d36e967-e325-11ce-bfc1-08002be10318}") ==
              0) {
            RegCloseKey(hKey);
            return true;
          }
        }
      }
    }
    RegCloseKey(hKey);
  }

  // Method 2: Check hardware ID for USBSTOR
  wchar_t hardwareId[1024] = {};
  DWORD hwSize = sizeof(hardwareId);
  if (SetupDiGetDeviceRegistryPropertyW(hDevInfo, &devInfoData,
                                        SPDRP_HARDWAREID, NULL,
                                        (PBYTE)hardwareId, hwSize, &hwSize)) {
    std::wstring wsHwId(hardwareId);
    if (wsHwId.find(L"USBSTOR") != std::wstring::npos) {
      return true;
    }
    // Also check for mass storage class in hardware ID
    if (wsHwId.find(L"USB\\Class_08") != std::wstring::npos) {
      return true;
    }
  }

  // Method 3: Check compatible IDs
  wchar_t compatibleIds[1024] = {};
  DWORD compatSize = sizeof(compatibleIds);
  if (SetupDiGetDeviceRegistryPropertyW(
          hDevInfo, &devInfoData, SPDRP_COMPATIBLEIDS, NULL,
          (PBYTE)compatibleIds, compatSize, &compatSize)) {
    std::wstring wsCompat(compatibleIds);
    if (wsCompat.find(L"USB\\Class_08") != std::wstring::npos) {
      return true;
    }
  }

  return false;
}

std::vector<UsbDeviceInfo> ListUsbDevices() {
  std::vector<UsbDeviceInfo> devices;

  HDEVINFO hDevInfo =
      SetupDiGetClassDevsW(&GUID_DEVCLASS_USB, nullptr, nullptr, DIGCF_PRESENT);
  if (hDevInfo == INVALID_HANDLE_VALUE)
    return devices;

  SP_DEVINFO_DATA devInfoData = {};
  devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

  for (DWORD index = 0; SetupDiEnumDeviceInfo(hDevInfo, index, &devInfoData);
       ++index) {
    UsbDeviceInfo info = {};

    wchar_t instanceId[MAX_DEVICE_ID_LEN];
    if (!SetupDiGetDeviceInstanceIdW(hDevInfo, &devInfoData, instanceId,
                                     MAX_DEVICE_ID_LEN, nullptr))
      continue;

    std::wstring wsInstanceId(instanceId);

    if (wsInstanceId.find(L"USB\\VID_") != 0)
      continue;

    size_t vidPos = wsInstanceId.find(L"VID_");
    size_t pidPos = wsInstanceId.find(L"PID_");
    if (vidPos == std::wstring::npos || pidPos == std::wstring::npos)
      continue;

    std::wstring vidStr = wsInstanceId.substr(vidPos + 4, 4);
    std::wstring pidStr = wsInstanceId.substr(pidPos + 4, 4);

    try {
      info.vid = (uint16_t)std::stoul(vidStr, nullptr, 16);
      info.pid = (uint16_t)std::stoul(pidStr, nullptr, 16);
    } catch (...) {
      continue;
    }

    // Get friendly name
    wchar_t friendlyName[256] = {};
    DWORD nameSize = sizeof(friendlyName);
    if (SetupDiGetDeviceRegistryPropertyW(
            hDevInfo, &devInfoData, SPDRP_FRIENDLYNAME, NULL,
            (PBYTE)friendlyName, nameSize, &nameSize)) {
      char ansiName[256] = {};
      WideCharToMultiByte(CP_ACP, 0, friendlyName, -1, ansiName, 256, NULL,
                          NULL);
      info.description = ansiName;
    }

    // Check if authorized for sharing
    std::string rawHardwareStr(wsInstanceId.begin(), wsInstanceId.end());
    bool isAuthorized = IsDeviceAuthorizedInRegistry(rawHardwareStr);

    // FIXED: Use the new mass storage detection function
    info.isMassStorage = IsMassStorageDevice(hDevInfo, devInfoData);

    // Generate busid
    size_t lastBackslash = wsInstanceId.find_last_of(L'\\');
    if (lastBackslash != std::wstring::npos) {
      std::wstring serial = wsInstanceId.substr(lastBackslash + 1);
      std::string busid;
      for (wchar_t c : serial) {
        if (isalnum(c))
          busid += static_cast<char>(c);
      }
      if (busid.length() > 8)
        busid = busid.substr(0, 8);
      info.busid = busid;
    }

    std::cout << "[+] Found Device -> VID: " << std::hex << info.vid
              << " PID: " << info.pid;
    if (isAuthorized) {
      std::cout << " [AUTHORIZED FOR SHARE]";
    } else {
      std::cout << " [LOCAL ONLY]";
    }
    if (info.isMassStorage) {
      std::cout << " [MASS STORAGE]";
    }
    std::cout << std::dec << std::endl;

    devices.push_back(info);
  }

  SetupDiDestroyDeviceInfoList(hDevInfo);
  return devices;
}
void PrintDeviceList(const std::vector<UsbDeviceInfo> &devices) {
  std::cout << "\n========================================" << std::endl;
  std::cout << "  USB Device List" << std::endl;
  std::cout << "========================================" << std::endl;

  if (devices.empty()) {
    std::cout << "No USB devices found" << std::endl;
    return;
  }

  std::cout << std::left << std::setw(5) << "Index" << std::setw(10) << "VID"
            << std::setw(10) << "PID" << std::setw(15) << "BusID"
            << std::setw(12) << "Type"
            << "Description" << std::endl;
  std::cout << std::string(80, '-') << std::endl;

  for (size_t i = 0; i < devices.size(); ++i) {
    const auto &dev = devices[i];
    std::cout << std::left << std::setw(5) << i << std::setw(10)
              << ("0x" + std::to_string(dev.vid)) << std::setw(10)
              << ("0x" + std::to_string(dev.pid)) << std::setw(15) << dev.busid
              << std::setw(12) << (dev.isMassStorage ? "Mass Storage" : "Other")
              << dev.description << std::endl;
  }
}

bool FindDevice(uint16_t vid, uint16_t pid, UsbDeviceInfo &outInfo) {
  auto devices = ListUsbDevices();
  for (const auto &dev : devices) {
    if (dev.vid == vid && dev.pid == pid) {
      outInfo = dev;
      return true;
    }
  }
  return false;
}

bool IsDeviceBound(uint16_t vid, uint16_t pid) {
  HDEVINFO hDevInfo = SetupDiGetClassDevsW(NULL, L"USB", NULL,
                                           DIGCF_PRESENT | DIGCF_ALLCLASSES);
  if (hDevInfo == INVALID_HANDLE_VALUE)
    return false;

  SP_DEVINFO_DATA devInfo = {};
  devInfo.cbSize = sizeof(SP_DEVINFO_DATA);

  wchar_t vidStr[16], pidStr[16];
  swprintf_s(vidStr, L"VID_%04X", vid);
  swprintf_s(pidStr, L"PID_%04X", pid);

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
  return found;
}

bool IsDeviceAttached() {
  HDEVINFO hDevInfo = SetupDiGetClassDevsW(
      &GUID_CLASS_VBOXUSB, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

  if (hDevInfo == INVALID_HANDLE_VALUE)
    return false;

  SP_DEVICE_INTERFACE_DATA devInterfaceData = {};
  devInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

  bool found = false;
  for (DWORD i = 0;; ++i) {
    if (!SetupDiEnumDeviceInterfaces(hDevInfo, NULL, &GUID_CLASS_VBOXUSB, i,
                                     &devInterfaceData)) {
      break;
    }
    found = true;
    break;
  }

  SetupDiDestroyDeviceInfoList(hDevInfo);
  return found;
}

// ============================================================================
// Device Binding and Attachment
// ============================================================================

bool BindDevice(const UsbDeviceInfo &device) {
  std::cout << "[*] Binding device: " << device.description << std::endl;

  if (!IsDeviceBound(device.vid, device.pid)) {
    std::cerr << "[-] Device not found for binding" << std::endl;
    return false;
  }

  std::cout << "[+] Device bound successfully" << std::endl;
  return true;
}

bool AttachDevice(HANDLE hVBoxMon, const UsbDeviceInfo &device) {
  std::cout << "[*] Attaching device: " << device.description << std::endl;

  uint64_t filterId = VBoxUsbMonAddFilter(hVBoxMon, device.vid, device.pid, 5);
  if (filterId == 0) {
    return false;
  }

  std::cout << "[+] Filter added. ID: " << filterId << std::endl;

  if (!VBoxUsbMonRunFilters(hVBoxMon)) {
    std::cerr << "[-] Failed to run filters" << std::endl;
    return false;
  }

  std::cout << "[+] Filters executed" << std::endl;
  return true;
}

bool DetachDevice(HANDLE hVBoxMon, uint64_t filterId) {
  std::cout << "[*] Detaching device..." << std::endl;

  if (!VBoxUsbMonRemoveFilter(hVBoxMon, filterId)) {
    std::cerr << "[-] Failed to remove filter" << std::endl;
    return false;
  }

  std::cout << "[+] Device detached" << std::endl;
  return true;
}

// ============================================================================
// USB/IP Protocol Handlers
// ============================================================================

void UsbIpHandleOpReqDevList(SOCKET clientSocket) {
  auto devices = ListUsbDevices();

  USBIP_OP_REP_DEVLIST listReply = {};
  listReply.common.version = htons(0x0111);
  listReply.common.commandCode = htons(0x0005);
  listReply.common.status = 0;
  listReply.numDevices = htonl((uint32_t)devices.size());

  send(clientSocket, (char *)&listReply, sizeof(listReply), 0);

  for (auto &dev : devices) {
    send(clientSocket, (char *)&dev, sizeof(dev), 0);
    uint8_t interfaceInfo[4] = {0x08, 0x06, 0x50, 0x00};
    send(clientSocket, (char *)interfaceInfo, sizeof(interfaceInfo), 0);
  }
}

void UsbIpHandleOpReqImport(SOCKET clientSocket, const char *busid) {
  std::string busidStr(busid);

  // Find device
  UsbDeviceInfo devInfo;
  if (!FindDevice(0x0781, 0x558a, devInfo)) {
    std::cerr << "[-] Device not found" << std::endl;
    closesocket(clientSocket);
    return;
  }

  // Attach device
  HANDLE hVBoxMon =
      CreateFileW(VBOXUSB_DEVICE_NAME, FILE_READ_DATA | FILE_WRITE_DATA,
                  FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                  FILE_FLAG_OVERLAPPED, NULL);

  if (hVBoxMon == INVALID_HANDLE_VALUE) {
    closesocket(clientSocket);
    return;
  }

  uint64_t filterId =
      VBoxUsbMonAddFilter(hVBoxMon, devInfo.vid, devInfo.pid, 5);
  if (filterId == 0) {
    CloseHandle(hVBoxMon);
    closesocket(clientSocket);
    return;
  }

  VBoxUsbMonRunFilters(hVBoxMon);

  // Open VBoxUsb device
  HANDLE hVBoxUsb = OpenVBoxUsbDevice();
  if (hVBoxUsb == INVALID_HANDLE_VALUE) {
    std::cerr << "[-] VBoxUSB device not available" << std::endl;
    CloseHandle(hVBoxMon);
    closesocket(clientSocket);
    return;
  }

  VBoxUsbClaimDevice(hVBoxUsb);

  // Store device state
  DeviceState state = {};
  state.attached = true;
  state.claimed = true;
  state.hVBoxUsb = hVBoxUsb;
  state.filterId = filterId;
  strcpy_s(state.info.busid, busidStr.c_str());
  state.info.idVendor = SWAP16(devInfo.vid);
  state.info.idProduct = SWAP16(devInfo.pid);
  g_devices[busidStr] = state;

  // Send import reply
  USBIP_OP_REP_IMPORT importReply = {};
  importReply.common.version = htons(0x0111);
  importReply.common.commandCode = htons(0x0003);
  importReply.common.status = 0;
  importReply.dev = state.info;
  send(clientSocket, (char *)&importReply, sizeof(importReply), 0);

  std::cout << "[+] Device imported: " << busidStr << std::endl;

  // Handle URB submissions
  while (true) {
    USBIP_HEADER_BASIC basicHeader;
    if (!ReceiveExactBytes(clientSocket, (char *)&basicHeader,
                           sizeof(basicHeader)))
      break;

    uint32_t cmdType = ntohl(basicHeader.command);
    uint32_t seqNum = ntohl(basicHeader.seqnum);
    uint32_t direction = ntohl(basicHeader.direction);
    uint32_t ep = ntohl(basicHeader.ep);

    if (cmdType == 0x00000001) { // USBIP_CMD_SUBMIT
      UsbIpHandleCmdSubmit(clientSocket, hVBoxUsb, &basicHeader);
    } else if (cmdType == 0x00000002) { // USBIP_CMD_UNLINK
      UsbIpHandleCmdUnlink(clientSocket, &basicHeader);
    } else {
      break;
    }
  }

  // Cleanup
  auto it = g_devices.find(busidStr);
  if (it != g_devices.end()) {
    if (it->second.hVBoxUsb != INVALID_HANDLE_VALUE) {
      CloseHandle(it->second.hVBoxUsb);
    }
    g_devices.erase(it);
  }

  CloseHandle(hVBoxMon);
}

void UsbIpHandleCmdSubmit(SOCKET clientSocket, HANDLE hVBoxUsb,
                          const USBIP_HEADER_BASIC *basicHeader) {
  USBIP_CMD_SUBMIT submit = {};
  memcpy(&submit.base, basicHeader, sizeof(USBIP_HEADER_BASIC));

  if (!ReceiveExactBytes(clientSocket, (char *)&submit.transferFlags,
                         sizeof(submit) - sizeof(USBIP_HEADER_BASIC)))
    return;

  int32_t reqLen = ntohl(submit.transferBufferLength);
  std::vector<char> dataBuffer(reqLen > 0 ? reqLen : 0);

  uint32_t direction = ntohl(basicHeader->direction);
  uint32_t ep = ntohl(basicHeader->ep);
  uint32_t seqNum = ntohl(basicHeader->seqnum);

  if (direction == 0 && reqLen > 0) {
    if (!ReceiveExactBytes(clientSocket, dataBuffer.data(), reqLen))
      return;
  }

  // Forward URB to VBoxUsb device
  UsbSupUrb urb = {};
  UsbSupUrbInit(&urb);
  urb.ep = ep;
  urb.dir = direction;
  urb.len = reqLen;
  urb.buf = reinterpret_cast<uint64_t>(dataBuffer.data());

  int32_t actualLength = 0;
  bool success = VBoxUsbSendUrb(hVBoxUsb, &urb, direction, dataBuffer.data(),
                                reqLen, &actualLength);

  // Send response
  USBIP_RET_SUBMIT retSubmit = {};
  retSubmit.base.command = htonl(0x00000003);
  retSubmit.base.seqnum = htonl(seqNum);
  retSubmit.base.devid = basicHeader->devid;
  retSubmit.base.direction = htonl(direction);
  retSubmit.base.ep = basicHeader->ep;
  retSubmit.status = success ? 0 : -1;
  retSubmit.actualLength = htonl(actualLength);

  send(clientSocket, (char *)&retSubmit, sizeof(retSubmit), 0);
  if (direction == 1 && actualLength > 0) {
    send(clientSocket, dataBuffer.data(), actualLength, 0);
  }
}

void UsbIpHandleCmdUnlink(SOCKET clientSocket,
                          const USBIP_HEADER_BASIC *basicHeader) {
  char unlinkTail[28];
  if (!ReceiveExactBytes(clientSocket, unlinkTail, sizeof(unlinkTail)))
    return;

  USBIP_RET_UNLINK retUnlink = {};
  retUnlink.base.command = htonl(0x00000004);
  retUnlink.base.seqnum = htonl(basicHeader->seqnum);
  retUnlink.status = 0;
  send(clientSocket, (char *)&retUnlink, sizeof(retUnlink), 0);
}

void UsbIpHandleClient(SOCKET clientSocket) {
  USBIP_OP_COMMON commonHeader;
  if (!ReceiveExactBytes(clientSocket, (char *)&commonHeader,
                         sizeof(USBIP_OP_COMMON))) {
    closesocket(clientSocket);
    return;
  }

  uint16_t command = ntohs(commonHeader.commandCode);

  if (command == 0x8005) { // OP_REQ_DEVLIST
    UsbIpHandleOpReqDevList(clientSocket);
  } else if (command == 0x8003) { // OP_REQ_IMPORT
    char busid[32];
    if (!ReceiveExactBytes(clientSocket, busid, sizeof(busid))) {
      closesocket(clientSocket);
      return;
    }
    UsbIpHandleOpReqImport(clientSocket, busid);
  }

  closesocket(clientSocket);
}

// ============================================================================
// Test Functions
// ============================================================================

void TestVBoxUsbMon() {
  std::cout << "\n========================================" << std::endl;
  std::cout << "  VBoxUsbMon Filter Driver Tests" << std::endl;
  std::cout << "========================================" << std::endl;

  HANDLE hVBoxMon =
      CreateFileW(VBOXUSBMON_DEVICE_NAME, FILE_READ_DATA | FILE_WRITE_DATA,
                  FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                  FILE_FLAG_OVERLAPPED, NULL);

  if (hVBoxMon == INVALID_HANDLE_VALUE) {
    std::cerr << "[-] Failed to open VBoxUSBMon. Error: " << GetLastError()
              << std::endl;
    return;
  }
  std::cout << "[+] VBoxUSBMon handle opened successfully" << std::endl;

  // TEST 1: GET_VERSION
  std::cout << "\n--- Test 1: SUPUSBFLT_IOCTL_GET_VERSION ---" << std::endl;
  {
    UsbSupVersion ver = VBoxUsbMonGetVersion(hVBoxMon);
    if (ver.major != 0 || ver.minor != 0) {
      std::cout << "[+] Version: " << ver.major << "." << ver.minor
                << std::endl;
    }
  }

  // TEST 2: ADD_FILTER
  std::cout << "\n--- Test 2: SUPUSBFLT_IOCTL_ADD_FILTER ---" << std::endl;
  uint64_t filterId = 0;
  {
    filterId = VBoxUsbMonAddFilter(hVBoxMon, 0x0781, 0x558a, 5);
    if (filterId != 0) {
      std::cout << "[+] Filter added. ID: " << filterId << std::endl;
    }
  }

  // TEST 3: RUN_FILTERS
  std::cout << "\n--- Test 3: SUPUSBFLT_IOCTL_RUN_FILTERS ---" << std::endl;
  {
    if (VBoxUsbMonRunFilters(hVBoxMon)) {
      std::cout << "[+] RUN_FILTERS succeeded" << std::endl;
    }
  }

  // TEST 4: REMOVE_FILTER
  std::cout << "\n--- Test 4: SUPUSBFLT_IOCTL_REMOVE_FILTER ---" << std::endl;
  if (filterId != 0) {
    if (VBoxUsbMonRemoveFilter(hVBoxMon, filterId)) {
      std::cout << "[+] Filter removed" << std::endl;
    }
  }

  CloseHandle(hVBoxMon);
  std::cout << "\n[+] VBoxUsbMon handle closed" << std::endl;
}

bool TestVBoxUsbDevice() {
  std::cout << "\n========================================" << std::endl;
  std::cout << "  VBoxUsb Device Driver Tests" << std::endl;
  std::cout << "========================================" << std::endl;

  HANDLE hVBoxUsb = OpenVBoxUsbDevice();
  if (hVBoxUsb == INVALID_HANDLE_VALUE) {
    std::cerr << "[-] VBoxUsb device not available - skipping device tests"
              << std::endl;
    return false;
  }
  std::cout << "[+] VBoxUsb device handle opened successfully" << std::endl;

  // TEST 5: GET_VERSION
  std::cout << "\n--- Test 5: SUPUSB_IOCTL_GET_VERSION ---" << std::endl;
  {
    UsbSupVersion ver = VBoxUsbGetVersion(hVBoxUsb);
    std::cout << "[+] Version: " << ver.major << "." << ver.minor << std::endl;
  }

  // TEST 6: USB_CLAIM_DEVICE
  std::cout << "\n--- Test 6: SUPUSB_IOCTL_USB_CLAIM_DEVICE ---" << std::endl;
  {
    if (VBoxUsbClaimDevice(hVBoxUsb)) {
      std::cout << "[+] Device claimed" << std::endl;
    }
  }

  // TEST 7: IS_OPERATIONAL
  std::cout << "\n--- Test 7: SUPUSB_IOCTL_IS_OPERATIONAL ---" << std::endl;
  {
    if (VBoxUsbIsOperational(hVBoxUsb)) {
      std::cout << "[+] Is operational: YES" << std::endl;
    }
  }

  // TEST 8: SEND_URB
  std::cout << "\n--- Test 8: SUPUSB_IOCTL_SEND_URB ---" << std::endl;
  {
    uint8_t buffer[64] = {};
    UsbSupUrb urb = {};
    UsbSupUrbInit(&urb);
    urb.ep = 0x81;
    urb.dir = USBSUP_DIRECTION_IN;
    urb.len = sizeof(buffer);
    urb.buf = reinterpret_cast<uint64_t>(buffer);

    int32_t actualLength = 0;
    if (VBoxUsbSendUrb(hVBoxUsb, &urb, USBSUP_DIRECTION_IN, (char *)buffer,
                       sizeof(buffer), &actualLength)) {
      std::cout << "[+] URB sent. Length: " << actualLength << std::endl;
    }
  }

  // TEST 9: USB_RELEASE_DEVICE
  std::cout << "\n--- Test 9: SUPUSB_IOCTL_USB_RELEASE_DEVICE ---" << std::endl;
  {
    if (VBoxUsbReleaseDevice(hVBoxUsb)) {
      std::cout << "[+] USB_RELEASE_DEVICE succeeded" << std::endl;
    }
  }

  CloseHandle(hVBoxUsb);
  std::cout << "\n[+] VBoxUsb device handle closed" << std::endl;
  return true;
}

bool TestListBindAttach() {
  std::cout << "\n========================================" << std::endl;
  std::cout << "  USB Device List, Bind, Attach Test" << std::endl;
  std::cout << "========================================" << std::endl;

  // Step 1: List all USB devices
  std::cout << "\n--- Step 1: List USB devices ---" << std::endl;
  auto devices = ListUsbDevices();
  PrintDeviceList(devices);

  if (devices.empty()) {
    std::cerr << "[-] No USB devices found" << std::endl;
    return false;
  }

  // Step 2: Find a mass storage device
  std::cout << "\n--- Step 2: Find mass storage device ---" << std::endl;
  UsbDeviceInfo targetDevice;
  bool found = false;
  for (const auto &dev : devices) {
    if (dev.isMassStorage) {
      targetDevice = dev;
      found = true;
      break;
    }
  }

  if (!found) {
    std::cerr << "[-] No mass storage device found" << std::endl;
    return false;
  }

  std::cout << "[+] Found mass storage device: " << targetDevice.description
            << std::endl;

  // Step 3: Bind device
  std::cout << "\n--- Step 3: Bind device ---" << std::endl;
  if (!BindDevice(targetDevice)) {
    return false;
  }

  // Step 4: Attach device
  std::cout << "\n--- Step 4: Attach device ---" << std::endl;
  HANDLE hVBoxMon =
      CreateFileW(VBOXUSBMON_DEVICE_NAME, FILE_READ_DATA | FILE_WRITE_DATA,
                  FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                  FILE_FLAG_OVERLAPPED, NULL);

  if (hVBoxMon == INVALID_HANDLE_VALUE) {
    std::cerr << "[-] Failed to open VBoxUSBMon" << std::endl;
    return false;
  }

  uint64_t filterId = 0;
  if (!AttachDevice(hVBoxMon, targetDevice)) {
    CloseHandle(hVBoxMon);
    return false;
  }

  // Step 5: Wait for VBoxUSB device to appear
  std::cout << "\n--- Step 5: Wait for VBoxUSB device ---" << std::endl;
  std::cout << "[*] Waiting for VBoxUSB device to appear..." << std::endl;

  bool attached = false;
  for (int i = 0; i < 20; ++i) {
    Sleep(500);
    if (IsDeviceAttached()) {
      attached = true;
      break;
    }
    std::cout << "." << std::flush;
  }
  std::cout << std::endl;

  if (attached) {
    std::cout << "[+] Device attached successfully!" << std::endl;
  } else {
    std::cerr << "[-] VBoxUSB device did not appear" << std::endl;
  }

  CloseHandle(hVBoxMon);
  return attached;
}

// ============================================================================
// Main
// ============================================================================

//int wmain(int argc, wchar_t *argv[]) {
//  std::cout << "========================================" << std::endl;
//  std::cout << "  VBoxUSB IOCTL Test Suite" << std::endl;
//  std::cout << "========================================" << std::endl;
//
//  // Test 1: VBoxUsbMon Filter Driver Tests
//  TestVBoxUsbMon();
//
//  // Test 2: List, Bind, Attach USB Device
//  if (TestListBindAttach()) {
//    // Test 3: VBoxUsb Device Driver Tests (only if attachment succeeded)
//    TestVBoxUsbDevice();
//  } else {
//    std::cout << "\n[!] Skipping VBoxUsb device tests (attachment failed)"
//              << std::endl;
//  }
//
//  std::cout << "\n========================================" << std::endl;
//  std::cout << "  All tests completed" << std::endl;
//  std::cout << "========================================" << std::endl;
//
//  return 0;
//}