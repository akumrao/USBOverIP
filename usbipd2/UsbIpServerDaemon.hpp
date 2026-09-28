#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <winusb.h>
#include <usb.h>

#include <iostream>
#include <string>
#include <vector>
#include <set>
#include <mutex>
#include <thread>

#include "UsbDeviceMonitor.hpp"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winusb.lib")

#define USBIP_PORT "3240"
#define USBIP_VERSION 0x0111
#define REG_BOUND_PATH L"SOFTWARE\\USBOverIP\\BoundDevices"

#pragma pack(push, 1)
struct usbip_header {
    uint16_t version;
    uint16_t command;
    uint32_t status;
};

struct op_rep_devlist {
    usbip_header header;
    uint32_t num_devices;
};

struct op_rep_import {
    usbip_header header;
    char path[100];
    char busid[32];
    uint32_t busnum;
    uint32_t devnum;
    uint32_t speed;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t bDeviceClass;
    uint8_t bDeviceSubClass;
    uint8_t bDeviceProtocol;
    uint8_t bConfigurationValue;
    uint8_t bNumConfigurations;
    uint8_t bNumInterfaces;
};

struct usbip_header_basic {
    uint32_t command;
    uint32_t seqnum;
    uint32_t devid;
    uint32_t direction;
    uint32_t ep;
};

struct usbip_cmd_submit {
    usbip_header_basic basic;
    uint32_t transfer_flags;
    uint32_t transfer_buffer_length;
    uint32_t start_frame;
    uint32_t number_of_packets;
    uint32_t interval;
    BYTE setup[8];
};

struct usbip_ret_submit {
    usbip_header_basic basic;
    uint32_t status;
    uint32_t actual_length;
    uint32_t start_frame;
    uint32_t number_of_packets;
    uint32_t error_count;
};
#pragma pack(pop)

class UsbIpServerDaemon {
public:
    UsbIpServerDaemon() = default;
    ~UsbIpServerDaemon() = default;

    void ExecuteBind(const std::string& busId);
    void ExecuteUnbind(const std::string& busId);
    void ExecuteUnbindAll();
    void ListDevices();
    void StartDaemon();

private:
    UsbDeviceMonitor m_monitor;
    std::mutex m_stateMutex;
    std::set<std::string> m_attachedDevices;

    void SetDeviceAttached(const std::string& busId, bool attached);
    bool IsDeviceAttached(const std::string& busId);
    bool IsDeviceBound(const std::string& busId);

    std::wstring GetDevicePathFromBusId(const std::string& targetBusId);
    bool OpenPhysicalUsbDevice(const std::string& busId, HANDLE& outDeviceHandle, WINUSB_INTERFACE_HANDLE& outWinUsbHandle);

    bool PopulateImportDetails(const std::string& targetBusId, op_rep_import& reply);
    void MaintainDataTunnel(SOCKET clientSocket, const std::string& targetBusId);
    void ExecuteProtocolEngine(SOCKET clientSocket);
};