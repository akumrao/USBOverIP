#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbt.h>         // Must be directly after windows.h
#include <setupapi.h>
#include <cfgmgr32.h>
#include <devguid.h>
#include <initguid.h>
#include <usbiodef.h>

#include <iostream>
#include <string>
#include <algorithm>
#include <thread>
#include <iomanip>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "newdev.lib")

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "newdev.lib")



class UsbIpServerDaemon;

class UsbDeviceMonitor {
public:
    UsbDeviceMonitor() : m_pDaemon(nullptr) {}
    ~UsbDeviceMonitor() = default;

    void SetDaemonReference(UsbIpServerDaemon* pDaemon) { m_pDaemon = pDaemon; }

    static std::string WideToString(const std::wstring& wstr);
    void StartHotplugListener();
    void ListConnectedDevices();

    // Automates INF creation and driver update to WinUSB.sys for target Hardware IDs
    static bool AutoAssignWinUsbDriver(const std::wstring& hardwareId);

private:
    UsbIpServerDaemon* m_pDaemon;
    static UsbIpServerDaemon* s_pDaemonRef;

    static LRESULT CALLBACK HotplugWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    static void IdentifyHotpluggedDevice(const std::wstring& symbolicPath);
    void RunMessageLoop();
};