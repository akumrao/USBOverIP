#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <cfgmgr32.h>
#include <setupapi.h>
#include <winioctl.h>

#define INITGUID
#include <initguid.h>

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
// VBoxUsbMon Filter Driver IOCTLs
// ============================================================================

#define VBOXUSBMON_DEVICE_NAME L"\\\\.\\VBoxUSBMon"
#define VBOXUSB_DEVICE_NAME L"\\\\.\\VBoxUSBMon"

#define SUPUSBFLT_IOCTL_GET_VERSION                                            \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x610, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define SUPUSBFLT_IOCTL_ADD_FILTER                                             \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x611, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define SUPUSBFLT_IOCTL_REMOVE_FILTER                                          \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x612, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define SUPUSBFLT_IOCTL_RUN_FILTERS                                            \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x615, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define SUPUSBFLT_IOCTL_GET_DEVICE                                             \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x617, METHOD_BUFFERED, FILE_WRITE_ACCESS)

// ============================================================================
// VBoxUsb Device Driver IOCTLs
// ============================================================================

#define SUPUSB_IOCTL_GET_DEVICE                                                \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x603, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define SUPUSB_IOCTL_SEND_URB                                                  \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x607, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define SUPUSB_IOCTL_USB_RESET                                                 \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x608, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define SUPUSB_IOCTL_USB_SELECT_INTERFACE                                      \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x609, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define SUPUSB_IOCTL_USB_SET_CONFIG                                            \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x60a, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define SUPUSB_IOCTL_USB_CLAIM_DEVICE                                          \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x60b, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define SUPUSB_IOCTL_USB_RELEASE_DEVICE                                        \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x60c, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define SUPUSB_IOCTL_IS_OPERATIONAL                                            \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x60d, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define SUPUSB_IOCTL_USB_CLEAR_ENDPOINT                                        \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x60e, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define SUPUSB_IOCTL_GET_VERSION                                               \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x60f, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define SUPUSB_IOCTL_USB_ABORT_ENDPOINT                                        \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x610, METHOD_BUFFERED, FILE_WRITE_ACCESS)

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
// VBoxUsbMon Structures (Pack = 4)
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
  uint16_t aFields[11 * 2];
  uint32_t offCurEnd;
  uint8_t achStrTab[256];
};

struct UsbSupIsoPkt {
  uint16_t cb;
  uint16_t off;
  uint32_t stat;
};

struct UsbSupUrb {
  uint32_t type;
  uint32_t ep;
  uint32_t dir;
  uint32_t flags;
  uint32_t error;
  uint64_t len;
  uint64_t buf;
  uint32_t numIsoPkts;
  uint32_t aIsoPkts[8 * 2];
};

#pragma pack(pop)

#pragma pack(push, 1)

struct UsbSupClaimDev {
  uint8_t bInterfaceNumber;
  uint8_t fClaimed;
};

struct UsbSupSetConfig {
  uint8_t bConfigurationValue;
};

struct UsbSupSelectInterface {
  uint8_t bInterfaceNumber;
  uint8_t bAlternateSetting;
};

struct UsbSupClearEndpoint {
  uint8_t bEndpoint;
};

#pragma pack(pop)

// ============================================================================
// Enums
// ============================================================================

enum UsbFilterType : uint32_t {
  USBFILTER_INVALID = 0,
  USBFILTER_FIRST = 1,
  USBFILTER_ONESHOT_IGNORE = USBFILTER_FIRST,
  USBFILTER_ONESHOT_CAPTURE,
  USBFILTER_IGNORE,
  USBFILTER_CAPTURE,
  USBFILTER_END,
};

enum UsbFilterMatch : uint16_t {
  USBFILTER_MATCH_INVALID = 0,
  USBFILTER_MATCH_IGNORE,
  USBFILTER_MATCH_PRESENT,
  USBFILTER_MATCH_NUM_FIRST,
  USBFILTER_MATCH_NUM_EXACT = USBFILTER_MATCH_NUM_FIRST,
  USBFILTER_MATCH_NUM_EXACT_NP,
  USBFILTER_MATCH_NUM_EXPRESSION,
  USBFILTER_MATCH_NUM_EXPRESSION_NP,
  USBFILTER_MATCH_NUM_LAST = USBFILTER_MATCH_NUM_EXPRESSION_NP,
  USBFILTER_MATCH_STR_FIRST,
  USBFILTER_MATCH_STR_EXACT = USBFILTER_MATCH_STR_FIRST,
  USBFILTER_MATCH_STR_EXACT_NP,
  USBFILTER_MATCH_STR_PATTERN,
  USBFILTER_MATCH_STR_PATTERN_NP,
  USBFILTER_MATCH_STR_LAST = USBFILTER_MATCH_STR_PATTERN_NP,
  USBFILTER_MATCH_END,
};

enum UsbFilterIdx : uint32_t {
  USBFILTER_IDX_VENDOR_ID = 0,
  USBFILTER_IDX_PRODUCT_ID,
  USBFILTER_IDX_DEVICE_REV,
  USBFILTER_IDX_DEVICE = USBFILTER_IDX_DEVICE_REV,
  USBFILTER_IDX_DEVICE_CLASS,
  USBFILTER_IDX_DEVICE_SUB_CLASS,
  USBFILTER_IDX_DEVICE_PROTOCOL,
  USBFILTER_IDX_BUS,
  USBFILTER_IDX_PORT,
  USBFILTER_IDX_MANUFACTURER_STR,
  USBFILTER_IDX_PRODUCT_STR,
  USBFILTER_IDX_SERIAL_NUMBER_STR,
  USBFILTER_IDX_END,
};

enum UsbSupTransferType : uint32_t {
  USBSUP_TRANSFER_TYPE_CTRL = 0,
  USBSUP_TRANSFER_TYPE_ISOC,
  USBSUP_TRANSFER_TYPE_BULK,
  USBSUP_TRANSFER_TYPE_INTR,
  USBSUP_TRANSFER_TYPE_MSG,
};

enum UsbSupDirection : uint32_t {
  USBSUP_DIRECTION_SETUP = 0,
  USBSUP_DIRECTION_IN,
  USBSUP_DIRECTION_OUT,
};

enum UsbSupXferFlags : uint32_t {
  USBSUP_FLAG_NONE = 0,
  USBSUP_FLAG_SHORT_OK = 1 << 0,
};

enum UsbSupError : uint32_t {
  USBSUP_XFER_OK = 0,
  USBSUP_XFER_STALL,
  USBSUP_XFER_DNR,
  USBSUP_XFER_CRC,
  USBSUP_XFER_NAC,
  USBSUP_XFER_UNDERRUN,
  USBSUP_XFER_OVERRUN,
};

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
// USB Device Info Structure
// ============================================================================

struct UsbDeviceInfo {
  std::string instanceId;
  std::string description;
  uint16_t vid;
  uint16_t pid;
  std::string busid;
  bool isMassStorage;
};

// ============================================================================
// Global Variables
// ============================================================================

extern HANDLE g_hVBoxDriver;
extern std::map<std::string, DeviceState> g_devices;

// ============================================================================
// Function Declarations - VBoxUsbMon
// ============================================================================

// VBoxUsbMon driver management
HANDLE OpenVBoxUsbDriver();
UsbSupVersion VBoxUsbMonGetVersion(HANDLE hVBoxMon);
uint64_t VBoxUsbMonAddFilter(HANDLE hVBoxMon, uint16_t vid, uint16_t pid,
                             uint8_t port);
bool VBoxUsbMonRemoveFilter(HANDLE hVBoxMon, uint64_t filterId);
bool VBoxUsbMonRunFilters(HANDLE hVBoxMon);

// VBoxUsbMon filter helpers
UsbFilter CreateUsbFilter(UsbFilterType type);
void SetFilterMatch(UsbFilter &filter, UsbFilterIdx index, UsbFilterMatch match,
                    uint16_t value);

// ============================================================================
// Function Declarations - VBoxUsb Device
// ============================================================================

// VBoxUsb device management
HANDLE OpenVBoxUsbDevice();
UsbSupVersion VBoxUsbGetVersion(HANDLE hVBoxUsb);
bool VBoxUsbClaimDevice(HANDLE hVBoxUsb);
bool VBoxUsbReleaseDevice(HANDLE hVBoxUsb);
bool VBoxUsbIsOperational(HANDLE hVBoxUsb);
bool VBoxUsbSendUrb(HANDLE hVBoxUsb, UsbSupUrb *urb, uint32_t direction,
                    char *dataBuffer, int32_t reqLen, int32_t *actualLength);

// ============================================================================
// Function Declarations - USB Device Enumeration
// ============================================================================

// USB device listing and management
std::vector<UsbDeviceInfo> ListUsbDevices();
void PrintDeviceList(const std::vector<UsbDeviceInfo> &devices);
bool FindDevice(uint16_t vid, uint16_t pid, UsbDeviceInfo &outInfo);
bool IsDeviceBound(uint16_t vid, uint16_t pid);
bool IsDeviceAttached();

// Device binding and attachment
bool BindDevice(const UsbDeviceInfo &device);
bool AttachDevice(HANDLE hVBoxMon, const UsbDeviceInfo &device);
bool DetachDevice(HANDLE hVBoxMon, uint64_t filterId);

// ============================================================================
// Function Declarations - USB/IP Protocol
// ============================================================================

// USB/IP server
bool UsbIpServerStart(uint16_t port);
void UsbIpServerStop();
void UsbIpHandleClient(SOCKET clientSocket);

// USB/IP protocol handlers
void UsbIpHandleOpReqDevList(SOCKET clientSocket);
void UsbIpHandleOpReqImport(SOCKET clientSocket, const char *busid);
void UsbIpHandleCmdSubmit(SOCKET clientSocket, HANDLE hVBoxUsb,
                          const USBIP_HEADER_BASIC *basicHeader);
void UsbIpHandleCmdUnlink(SOCKET clientSocket,
                          const USBIP_HEADER_BASIC *basicHeader);

// ============================================================================
// Function Declarations - Test Functions
// ============================================================================

// Test functions
void TestVBoxUsbMon();
bool TestVBoxUsbDevice();
bool TestListBindAttach();

// ============================================================================
// Function Declarations - Utility
// ============================================================================

// Utility functions
bool ReceiveExactBytes(SOCKET s, char *buffer, int bytesToRead);
void UsbSupUrbInit(UsbSupUrb *urb);