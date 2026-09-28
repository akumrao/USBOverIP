#pragma once
#include <cstdint>
#include <windows.h>
#include <winioctl.h>

#define VBOXUSB_DEVICE_NAME L"\\\\.\\VBoxUSBMon"

#define VBOXUSB_IOCTL_BASE 0x8000
#define VBOXUSB_CTL_CODE(code)                                                 \
  CTL_CODE(FILE_DEVICE_UNKNOWN, VBOXUSB_IOCTL_BASE + (code), METHOD_BUFFERED,  \
           FILE_ANY_ACCESS)

#define VBOXUSB_IOCTL_CAPTURE_DEVICE VBOXUSB_CTL_CODE(1)
#define VBOXUSB_IOCTL_RELEASE_DEVICE VBOXUSB_CTL_CODE(2)
#define VBOXUSB_IOCTL_SUBMIT_URB VBOXUSB_CTL_CODE(3)
#define VBOXUSB_IOCTL_REAP_URB VBOXUSB_CTL_CODE(4)

// Standard 8-byte alignment for 64-bit Windows Kernel Drivers
struct VBOXUSB_CAPTURE_REQ {
  uint32_t vendorId;
  uint32_t productId;
  uint32_t revision;
  uint32_t busNumber;
  uint32_t deviceAddress;
  uint32_t flags;
  wchar_t devicePath[260]; // Required device instance path
};

#pragma pack(push, 1)
struct VBOXUSB_URB_HDR {
  uint32_t handle;
  uint32_t endpoint;
  uint32_t transferFlags;
  uint32_t bufferLength;
  uint32_t status;
};
#pragma pack(pop)