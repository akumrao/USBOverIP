#include "UsbDeviceMonitor.hpp"
#include "UsbIpServerDaemon.hpp"
#include <fstream>
#include <newdev.h>

#ifndef DBT_DEVICEREMOVALCOMPLETE
#define DBT_DEVICEREMOVALCOMPLETE 0x8004
#endif

UsbIpServerDaemon* UsbDeviceMonitor::s_pDaemonRef = nullptr;

std::string UsbDeviceMonitor::WideToString(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string str(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &str[0], size, NULL, NULL);
    return str;
}

// Dynamically creates a temporary SetupAPI INF file and updates the hardware driver to WinUSB.sys
bool UsbDeviceMonitor::AutoAssignWinUsbDriver(const std::wstring& hardwareId) {
    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    std::wstring infFilePath = std::wstring(tempPath) + L"auto_winusb.inf";

    // Generate a temporary generic WinUSB INF file targeted at the specific Hardware ID
    std::wofstream infFile(infFilePath);
    if (!infFile.is_open()) {
        std::wcerr << L"[ERROR] Failed to generate INF script for WinUSB binding." << std::endl;
        return false;
    }

    infFile << L"[Version]\n"
            << L"Signature=\"$Windows NT$\"\n"
            << L"Class=USBDevice\n"
            << L"ClassGUID={88BAE032-5A81-49f0-BC3D-A4FF138216D6}\n"
            << L"Provider=\"USBIP_AutoInstaller\"\n"
            << L"DriverVer=01/01/2026,1.0.0.0\n\n"
            << L"[Manufacturer]\n"
            << L"\"USBIP\"=DeviceList,NTamd64\n\n"
            << L"[DeviceList.NTamd64]\n"
            << L"\"USBIP Shared Hardware\"=USB_Install, " << hardwareId << L"\n\n"
            << L"[USB_Install]\n"
            << L"Include=winusb.inf\n"
            << L"Needs=WINUSB.NT\n\n"
            << L"[USB_Install.Services]\n"
            << L"Include=winusb.inf\n"
            << L"Needs=WINUSB.NT.Services\n";
    infFile.close();

    BOOL rebootRequired = FALSE;
    std::wcout << L"[AUTO-INSTALL] Attempting INF driver update to WinUSB.sys for: " << hardwareId << std::endl;

    // Call Windows Device Installer API to update physical driver to WinUSB
    BOOL status = UpdateDriverForPlugAndPlayDevicesW(
        NULL,
        hardwareId.c_str(),
        infFilePath.c_str(),
        INSTALLFLAG_FORCE | INSTALLFLAG_NONINTERACTIVE,
        &rebootRequired
    );

    DeleteFileW(infFilePath.c_str());

    if (status) {
        std::wcout << L"[AUTO-INSTALL SUCCESS] Device updated to WinUSB.sys natively!" << std::endl;
        return true;
    } else {
        DWORD err = GetLastError();
        if (err == ERROR_ACCESS_DENIED) {
            std::wcerr << L"[WARNING] Admin permissions required to automatically update driver to WinUSB.sys." << std::endl;
            std::wcerr << L"[NOTE] Please run this application as Administrator OR manually use Zadig once to set WinUSB." << std::endl;
        } else {
            std::wcerr << L"[INFO] Standard driver active or update returned code: " << err << L". (Zadig can be used as manual fallback)." << std::endl;
        }
        return false;
    }
}

void UsbDeviceMonitor::IdentifyHotpluggedDevice(
    const std::wstring &symbolicPath) {
  std::cout << "\n--------------------------------------------------"
            << std::endl;
  std::cout << "[ENUM LOG] Starting device matching pipeline..." << std::endl;
  std::cout << "[ENUM LOG] Raw Symbolic Path received: "
            << WideToString(symbolicPath) << std::endl;

  HDEVINFO devInfo =
      SetupDiGetClassDevsW(&GUID_DEVCLASS_USB, NULL, NULL, DIGCF_PRESENT);
  if (devInfo == INVALID_HANDLE_VALUE) {
    std::cerr << "[ENUM LOG ERROR] SetupDiGetClassDevsW failed to query USB "
                 "device class."
              << std::endl;
    return;
  }

  SP_DEVINFO_DATA devInfoData;
  devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

  std::string vidPid = "0000:0000";
  std::string busId = "";
  std::string description = "USB Device";
  std::wstring targetHwId = L"";

  // 1. Normalize the incoming symbolic path
  std::wstring normalizedSymbolic = symbolicPath;
  std::transform(normalizedSymbolic.begin(), normalizedSymbolic.end(),
                 normalizedSymbolic.begin(), ::tolower);
  std::replace(normalizedSymbolic.begin(), normalizedSymbolic.end(), L'#',
               L'\\');

  // Strip prefix '\\?\' or '\\.\'
  if (normalizedSymbolic.rfind(L"\\\\?\\", 0) == 0 ||
      normalizedSymbolic.rfind(L"\\\\.\\", 0) == 0) {
    normalizedSymbolic = normalizedSymbolic.substr(4);
  }

  // Strip trailing interface GUID if present
  size_t bracePos = normalizedSymbolic.find(L"\\{");
  if (bracePos != std::wstring::npos) {
    normalizedSymbolic = normalizedSymbolic.substr(0, bracePos);
  }

  std::cout << "[ENUM LOG] Normalized Target Path    : "
            << WideToString(normalizedSymbolic) << std::endl;
  std::cout << "[ENUM LOG] Scanning Windows PnP USB Registry Tree..."
            << std::endl;

  DWORD matchCount = 0;
  bool matchFound = false;

  for (DWORD i = 0; SetupDiEnumDeviceInfo(devInfo, i, &devInfoData); i++) {
    matchCount++;
    wchar_t buffer[MAX_DEVICE_ID_LEN] = {0};

    if (CM_Get_Device_IDW(devInfoData.DevInst, buffer, MAX_DEVICE_ID_LEN, 0) ==
        CR_SUCCESS) {
      std::wstring instanceId(buffer);
      std::wstring normalizedInstance = instanceId;
      std::transform(normalizedInstance.begin(), normalizedInstance.end(),
                     normalizedInstance.begin(), ::tolower);

      std::cout << "  -> Index [" << i
                << "] Instance ID: " << WideToString(instanceId) << std::endl;

      // 2. Perform cross-matching
      if (normalizedSymbolic.find(normalizedInstance) != std::wstring::npos ||
          normalizedInstance.find(normalizedSymbolic) != std::wstring::npos) {

        matchFound = true;
        targetHwId = instanceId;

        std::cout << "  [? MATCH SUCCESS] Index [" << i
                  << "] matches hotplug path!" << std::endl;

        if (SetupDiGetDeviceRegistryPropertyW(
                devInfo, &devInfoData, SPDRP_DEVICEDESC, NULL, (PBYTE)buffer,
                sizeof(buffer), NULL)) {
          description = WideToString(buffer);
        }

        size_t vidPos = instanceId.find(L"VID_");
        size_t pidPos = instanceId.find(L"PID_");
        if (vidPos != std::wstring::npos && pidPos != std::wstring::npos &&
            instanceId.length() >= vidPos + 8 &&
            instanceId.length() >= pidPos + 8) {
          std::wstring wVid = instanceId.substr(vidPos + 4, 4);
          std::wstring wPid = instanceId.substr(pidPos + 4, 4);
          vidPid = WideToString(wVid) + ":" + WideToString(wPid);
          for (auto &c : vidPid)
            c = (char)tolower(c);
        }

        size_t slashIndex = instanceId.find_last_of(L"\\");
        if (slashIndex != std::wstring::npos) {
          std::wstring wBusId = instanceId.substr(slashIndex + 1, 5);
          busId = WideToString(wBusId);
        }

        std::cout << "\n=========================================="
                  << std::endl;
        std::cout << "[HOTPLUG EVENT] USB Device Identified!" << std::endl;
        std::cout << "  DEVICE NAME : " << description << std::endl;
        std::cout << "  VID:PID     : " << vidPid << std::endl;
        std::cout << "  BUS ID      : " << busId << std::endl;
        std::cout << "==========================================" << std::endl;

        // 1. AUTOMATICALLY ASSIGN WINUSB.SYS
        std::cout << "[ENUM LOG] Initiating driver assignment to WinUSB.sys..."
                  << std::endl;
        AutoAssignWinUsbDriver(targetHwId);

        // 2. AUTO UNBIND OLD & AUTO BIND NEW DEVICE
        if (s_pDaemonRef && !busId.empty()) {
          std::cout << "[AUTO-BIND] Unbinding all older USB devices..."
                    << std::endl;
          s_pDaemonRef->ExecuteUnbindAll();

          std::cout << "[AUTO-BIND] Binding newly inserted device (BUSID: "
                    << busId << ")..." << std::endl;
          s_pDaemonRef->ExecuteBind(busId);
        }

        break;
      } else {
        std::cout << "     [x No Match] Target path does not contain this "
                     "instance ID."
                  << std::endl;
      }
    }
  }

  if (!matchFound) {
    std::cout << "[ENUM LOG WARNING] Scanned " << matchCount
              << " USB devices, but no matching PnP instance was found for: "
              << WideToString(normalizedSymbolic) << std::endl;
  }

  std::cout << "--------------------------------------------------\n"
            << std::endl;
  SetupDiDestroyDeviceInfoList(devInfo);
}

//void UsbDeviceMonitor::IdentifyHotpluggedDevice(const std::wstring& symbolicPath) {
//    HDEVINFO devInfo = SetupDiGetClassDevsW(&GUID_DEVCLASS_USB, NULL, NULL, DIGCF_PRESENT);
//    if (devInfo == INVALID_HANDLE_VALUE) return;
//
//    SP_DEVINFO_DATA devInfoData;
//    devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
//
//    std::string vidPid = "0000:0000";
//    std::string busId = "";
//    std::string description = "USB Device";
//    std::wstring targetHwId = L"";
//
//    std::wstring searchPath = symbolicPath;
//    std::transform(searchPath.begin(), searchPath.end(), searchPath.begin(), ::tolower);
//
//    for (DWORD i = 0; SetupDiEnumDeviceInfo(devInfo, i, &devInfoData); i++) {
//        wchar_t buffer[MAX_DEVICE_ID_LEN] = {0};
//
//        if (CM_Get_Device_IDW(devInfoData.DevInst, buffer, MAX_DEVICE_ID_LEN, 0) == CR_SUCCESS) {
//            std::wstring instanceId(buffer);
//            std::wstring matchCheck = instanceId;
//            std::transform(matchCheck.begin(), matchCheck.end(), matchCheck.begin(), ::tolower);
//
//            if (searchPath.find(matchCheck) != std::wstring::npos || matchCheck.find(searchPath) != std::wstring::npos) {
//                targetHwId = instanceId;
//
//                if (SetupDiGetDeviceRegistryPropertyW(devInfo, &devInfoData, SPDRP_DEVICEDESC, NULL, (PBYTE)buffer, sizeof(buffer), NULL)) {
//                    description = WideToString(buffer);
//                }
//
//                size_t vidPos = instanceId.find(L"VID_");
//                size_t pidPos = instanceId.find(L"PID_");
//                if (vidPos != std::wstring::npos && pidPos != std::wstring::npos &&
//                    instanceId.length() >= vidPos + 8 && instanceId.length() >= pidPos + 8) {
//                    std::wstring wVid = instanceId.substr(vidPos + 4, 4);
//                    std::wstring wPid = instanceId.substr(pidPos + 4, 4);
//                    vidPid = WideToString(wVid) + ":" + WideToString(wPid);
//                    for (auto &c : vidPid) c = (char)tolower(c);
//                }
//
//                size_t slashIndex = instanceId.find_last_of(L"\\");
//                if (slashIndex != std::wstring::npos) {
//                    std::wstring wBusId = instanceId.substr(slashIndex + 1, 5);
//                    busId = WideToString(wBusId);
//                }
//
//                std::cout << "\n==========================================" << std::endl;
//                std::cout << "[HOTPLUG EVENT] USB Device Connected!" << std::endl;
//                std::cout << "  DEVICE NAME : " << description << std::endl;
//                std::cout << "  VID:PID     : " << vidPid << std::endl;
//                std::cout << "  BUS ID      : " << busId << std::endl;
//                std::cout << "==========================================" << std::endl;
//
//                // --- 1. AUTOMATICALLY ASSIGN WINUSB.SYS ---
//                AutoAssignWinUsbDriver(targetHwId);
//
//                // --- 2. AUTO UNBIND OLD & AUTO BIND NEW DEVICE ---
//                if (s_pDaemonRef && !busId.empty()) {
//                    std::cout << "[AUTO-BIND] Unbinding all older USB devices..." << std::endl;
//                    s_pDaemonRef->ExecuteUnbindAll();
//
//                    std::cout << "[AUTO-BIND] Binding newly inserted device (BUSID: " << busId << ")..." << std::endl;
//                    s_pDaemonRef->ExecuteBind(busId);
//                }
//
//                break;
//            }
//        }
//    }
//
//    SetupDiDestroyDeviceInfoList(devInfo);
//}

LRESULT CALLBACK UsbDeviceMonitor::HotplugWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_DEVICECHANGE) {
        if (wParam == DBT_DEVICEARRIVAL) {
            PDEV_BROADCAST_HDR pHdr = (PDEV_BROADCAST_HDR)lParam;
            if (pHdr && pHdr->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
                PDEV_BROADCAST_DEVICEINTERFACE_W pDevInf = (PDEV_BROADCAST_DEVICEINTERFACE_W)pHdr;
                IdentifyHotpluggedDevice(pDevInf->dbcc_name);
            }
        } else if (wParam == DBT_DEVICEREMOVALCOMPLETE) {
            std::cout << "\n[HOTPLUG EVENT] USB Device Unplugged / Removed." << std::endl;
        }
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

void UsbDeviceMonitor::RunMessageLoop() {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = HotplugWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"USBIP_Hotplug_Class";

    RegisterClassW(&wc);

    HWND hWnd = CreateWindowExW(0, wc.lpszClassName, L"USBIP_Hotplug_Window", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, wc.hInstance, NULL);

    DEV_BROADCAST_DEVICEINTERFACE_W notificationFilter = {};
    notificationFilter.dbcc_size = sizeof(DEV_BROADCAST_DEVICEINTERFACE_W);
    notificationFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    notificationFilter.dbcc_classguid = GUID_DEVINTERFACE_USB_DEVICE;

    HDEVNOTIFY hDevNotify = RegisterDeviceNotificationW(
        hWnd,
        &notificationFilter,
        DEVICE_NOTIFY_WINDOW_HANDLE
    );

    std::cout << "[INFO] USB Hotplug Event Listener active." << std::endl;

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    UnregisterDeviceNotification(hDevNotify);
    DestroyWindow(hWnd);
}

void UsbDeviceMonitor::StartHotplugListener() {
    s_pDaemonRef = m_pDaemon;
    std::thread([this]() { this->RunMessageLoop(); }).detach();
}

void UsbDeviceMonitor::ListConnectedDevices() {
    HDEVINFO devInfo = SetupDiGetClassDevsW(&GUID_DEVCLASS_USB, NULL, NULL, DIGCF_PRESENT);
    if (devInfo == INVALID_HANDLE_VALUE) {
        std::cerr << "Failed to fetch Windows USB class device registry tree." << std::endl;
        return;
    }

    SP_DEVINFO_DATA devInfoData;
    devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

    std::cout << "Connected:" << std::endl;
    std::cout << std::left << std::setw(8) << "BUSID" << std::setw(11)
              << "VID:PID" << std::setw(30) << "DEVICE"
              << "STATE" << std::endl;

    for (DWORD i = 0; SetupDiEnumDeviceInfo(devInfo, i, &devInfoData); i++) {
        wchar_t buffer[MAX_DEVICE_ID_LEN] = {0};
        std::string description = "USB Input Device";
        std::string busId = "1-1";
        std::string vidPid = "0000:0000";

        if (SetupDiGetDeviceRegistryPropertyW(devInfo, &devInfoData, SPDRP_DEVICEDESC, NULL, (PBYTE)buffer, sizeof(buffer), NULL)) {
            description = WideToString(buffer);
        }

        if (CM_Get_Device_IDW(devInfoData.DevInst, buffer, MAX_DEVICE_ID_LEN, 0) == CR_SUCCESS) {
            std::wstring wInstanceId(buffer);

            size_t vidPos = wInstanceId.find(L"VID_");
            size_t pidPos = wInstanceId.find(L"PID_");
            if (vidPos != std::wstring::npos && pidPos != std::wstring::npos &&
                wInstanceId.length() >= vidPos + 8 && wInstanceId.length() >= pidPos + 8) {
                std::wstring wVid = wInstanceId.substr(vidPos + 4, 4);
                std::wstring wPid = wInstanceId.substr(pidPos + 4, 4);
                vidPid = WideToString(wVid) + ":" + WideToString(wPid);
                for (auto &c : vidPid) c = (char)tolower(c);
            }

            size_t slashIndex = wInstanceId.find_last_of(L"\\");
            if (slashIndex != std::wstring::npos) {
                std::wstring wBusId = wInstanceId.substr(slashIndex + 1, 5);
                busId = WideToString(wBusId);
            }
        }

        std::cout << std::left << std::setw(8) << busId << std::setw(11) << vidPid
                  << std::setw(30) << description << "Not shared" << std::endl;
    }

    SetupDiDestroyDeviceInfoList(devInfo);
}