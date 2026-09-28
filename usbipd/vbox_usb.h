#pragma once
#include <windows.h>
#include <winioctl.h>
#include <cstdint>

// VirtualBox USB Monitor Device Name
#define VBOXUSB_DEVICE_NAME L"\\\\.\\VBoxUSBMon"

// VirtualBox Internal Kernel IOCTL Definitions
#define VBOXUSB_IOCTL_BASE                  0x8000
#define VBOXUSB_CTL_CODE(code)              CTL_CODE(FILE_DEVICE_UNKNOWN, VBOXUSB_IOCTL_BASE + (code), METHOD_BUFFERED, FILE_ANY_ACCESS)

#define VBOXUSB_IOCTL_CAPTURE_DEVICE        VBOXUSB_CTL_CODE(1)
#define VBOXUSB_IOCTL_RELEASE_DEVICE        VBOXUSB_CTL_CODE(2)
#define VBOXUSB_IOCTL_SUBMIT_URB            VBOXUSB_CTL_CODE(3)
#define VBOXUSB_IOCTL_REAP_URB              VBOXUSB_CTL_CODE(4)

#pragma pack(push, 1)

// VirtualBox Device Capture Handshake Structure
struct VBOXUSB_CAPTURE_REQ {
    uint32_t vendorId;
    uint32_t productId;
    uint32_t revision;
    uint8_t  busNumber;
    uint8_t  deviceAddress;
    uint16_t reserved;
};

// VirtualBox URB Transfer Request Header
struct VBOXUSB_URB_HDR {
    uint32_t handle;            // Connection handle identifier
    uint32_t endpoint;          // Target USB Endpoint (0x00 for Control, 0x81/0x01 for Bulk)
    uint32_t transferFlags;     // Direction / Type flags
    uint32_t bufferLength;      // Size of payload buffer
    uint32_t status;            // Returned Kernel Status
};

#pragma pack(pop)