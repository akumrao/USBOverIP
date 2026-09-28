#pragma once
#include <cstdint>
#include <windows.h>
#include <winioctl.h>

// VirtualBox USB Device Interface GUID: {2408B120-A3CC-11D1-A902-00A0C9223196}
DEFINE_GUID(GUID_DEVINTERFACE_VBOXUSB, 0x2408B120, 0xA3CC, 0x11D1, 0xA9, 0x02,
            0x00, 0xA0, 0xC9, 0x22, 0x31, 0x96);

#define VBOXUSB_IOCTL_BASE 0x8000
#define VBOXUSB_CTL_CODE(code)                                                 \
  CTL_CODE(FILE_DEVICE_UNKNOWN, VBOXUSB_IOCTL_BASE + (code), METHOD_BUFFERED,  \
           FILE_ANY_ACCESS)

#define VBOXUSB_IOCTL_SUBMIT_URB VBOXUSB_CTL_CODE(3)
#define VBOXUSB_IOCTL_REAP_URB VBOXUSB_CTL_CODE(4)

#pragma pack(push, 1)

struct VBOXUSB_URB_HDR {
  uint32_t handle;
  uint32_t endpoint;
  uint32_t transferFlags;
  uint32_t bufferLength;
  uint32_t status;
};

#pragma pack(pop)