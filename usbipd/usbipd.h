#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <cfgmgr32.h>
#include <setupapi.h>

#ifndef CM_REGKEY_HARDWARE
#define CM_REGKEY_HARDWARE 0x00000000
#endif

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "cfgmgr32.lib")

#define MAX_DEVICE_ID_LEN 200
#define USBIP_PORT 3240

#define SWAP16(x) htons(x)
#define SWAP32(x) htonl(x)

// ============================================================================
// USB/IP Protocol Structures
// ============================================================================

#pragma pack(push, 1)

struct USBIP_OP_COMMON {
  uint16_t version;
  uint16_t commandCode;
  uint32_t status;
};

struct USBIP_OP_REP_DEVLIST {
  USBIP_OP_COMMON common;
  uint32_t numDevices;
};

struct USBIP_DEVICE_DESC {
  char path[256];
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

struct USBIP_OP_REP_IMPORT {
  USBIP_OP_COMMON common;
  USBIP_DEVICE_DESC dev;
};

struct USBIP_HEADER_BASIC {
  uint32_t command;
  uint32_t seqnum;
  uint32_t devid;
  uint32_t direction;
  uint32_t ep;
};

struct USBIP_CMD_SUBMIT {
  USBIP_HEADER_BASIC base;
  uint32_t transferFlags;
  int32_t transferBufferLength;
  uint32_t startFrame;
  uint32_t numberOfPackets;
  uint32_t interval;
  uint8_t setup[8];
};

struct USBIP_RET_SUBMIT {
  USBIP_HEADER_BASIC base;
  int32_t status;
  int32_t actualLength;
  uint32_t startFrame;
  uint32_t numberOfPackets;
  uint32_t errorCount;
  uint8_t padding[8];
};

struct USBIP_CMD_UNLINK {
  USBIP_HEADER_BASIC base;
  uint32_t unlinkSeqnum;
  uint8_t padding[24];
};

struct USBIP_RET_UNLINK {
  USBIP_HEADER_BASIC base;
  int32_t status;
  uint8_t padding[24];
};

#pragma pack(pop)

// ============================================================================
// VBoxUsbMon IOCTLs
// ============================================================================

#define VBOXUSB_DEVICE_NAME L"\\\\.\\VBoxUSBMon"

#define SUPUSBFLT_IOCTL_GET_VERSION                                            \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x610, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define SUPUSBFLT_IOCTL_ADD_FILTER                                             \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x611, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define SUPUSBFLT_IOCTL_REMOVE_FILTER                                          \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x612, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define SUPUSBFLT_IOCTL_RUN_FILTERS                                            \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x615, METHOD_BUFFERED, FILE_WRITE_ACCESS)

// ============================================================================
// VBoxUsb Device IOCTLs
// ============================================================================

#define SUPUSB_IOCTL_GET_VERSION                                               \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x60f, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define SUPUSB_IOCTL_USB_CLAIM_DEVICE                                          \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x60b, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define SUPUSB_IOCTL_USB_RELEASE_DEVICE                                        \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x60c, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define SUPUSB_IOCTL_SEND_URB                                                  \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x607, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define SUPUSB_IOCTL_IS_OPERATIONAL                                            \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x60d, METHOD_BUFFERED, FILE_WRITE_ACCESS)

// ============================================================================
// VBoxUsbMon Structures
// ============================================================================

#pragma pack(push, 4)

struct UsbSupVersion {
  uint32_t major;
  uint32_t minor;
};

struct UsbSupFltAddOut {
  uint64_t uId;
  int32_t rc;
};

struct UsbFilter {
  uint32_t u32Magic;
  uint32_t enmType;
  uint16_t aFields[22];
  uint32_t offCurEnd;
  uint8_t achStrTab[256];
};

// VBoxUsb URB structure
struct UsbSupUrb {
  uint32_t type;
  uint32_t ep;
  uint32_t dir;
  uint32_t flags;
  uint32_t error;
  uint64_t len;
  uint64_t buf;
  uint32_t numIsoPkts;
  uint32_t aIsoPkts[16];
};

#pragma pack(pop)

// ============================================================================
// Device State
// ============================================================================

struct DeviceState {
  bool attached;
  bool claimed;
  HANDLE hVBoxUsb;
  uint64_t filterId;
  USBIP_DEVICE_DESC info;
};

// ============================================================================
// Function Declarations
// ============================================================================

bool ReceiveExactBytes(SOCKET s, char *buffer, int bytesToRead);
HANDLE OpenVBoxUsbDriver();
HANDLE OpenVBoxUsbDevice();
bool CaptureDeviceWithVBox(HANDLE hVBox, uint16_t vid, uint16_t pid);
bool ReleaseDeviceWithVBox(HANDLE hVBox, uint16_t vid, uint16_t pid);
std::vector<USBIP_DEVICE_DESC> ScanPhysicalUsbBus();
void ConnectionWorkerThread(SOCKET clientSocket);
bool SendUrbToDevice(HANDLE hDevice, UsbSupUrb *urb, uint32_t direction,
                     char *dataBuffer, int32_t reqLen, int32_t *actualLength);

extern HANDLE g_hVBoxDriver;
extern std::map<std::string, DeviceState> g_devices;


void DebugListAllUsbDevices();













bool IsVBoxUsbDriverRunning();

bool StartVBoxUsbDriver();


bool EnsureVBoxUsbDriver();