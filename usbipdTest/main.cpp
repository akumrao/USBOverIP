#include "usbipd.h"


#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>
#if 0

// Core Windows header MUST be included before other Windows subsystem headers
#include <windows.h>

// Other Windows subsystem headers
#include <cfgmgr32.h>
#include <setupapi.h>
#include <winioctl.h>

// IMPORTANT: Define INITGUID before including initguid.h for DEFINE_GUID to
// work
#define INITGUID
#include <initguid.h>

#pragma comment(lib, "setupapi.lib")

// VBoxUSB Device Interface GUID
// {873fdfCA-FE80-EE80-AA5E-00C04FB1720B}
DEFINE_GUID(GUID_CLASS_VBOXUSB, 0x873fdfCA, 0xFE80, 0xEE80, 0xAA, 0x5E, 0x00,
            0xC0, 0x4F, 0xB1, 0x72, 0x0B);

// ============================================================================
// VBoxUsbMon Filter Driver IOCTLs (device: \\.\VBoxUSBMon)
// ============================================================================

#define VBOXUSBMON_DEVICE_NAME L"\\\\.\\VBoxUSBMon"

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
// Constants
// ============================================================================

constexpr uint32_t USBMON_MAJOR_VERSION = 5;
constexpr uint32_t USBMON_MINOR_VERSION = 0;
constexpr uint32_t USBDRV_MAJOR_VERSION = 5;
constexpr uint32_t USBDRV_MINOR_VERSION = 0;
constexpr uint32_t USBFILTER_MAGIC = 0x19670408;

// ============================================================================
// Structures (Pack = 4 unless noted)
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
// Helper Functions
// ============================================================================
HANDLE OpenVBoxUsbDevice() {
  HDEVINFO hDevInfo = SetupDiGetClassDevsW(
      &GUID_CLASS_VBOXUSB, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

  if (hDevInfo == INVALID_HANDLE_VALUE) {
    std::cerr << "[-] SetupDiGetClassDevsW failed. Error: " << GetLastError()
              << std::endl;
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
        std::cerr << "    Make sure:" << std::endl;
        std::cerr << "    1. VBoxUSB driver is installed" << std::endl;
        std::cerr << "    2. Device is bound: usbipd bind --busid 1-5"
                  << std::endl;
        std::cerr << "    3. A client has imported the device" << std::endl;
      } else {
        std::cerr << "[-] SetupDiEnumDeviceInterfaces failed. Error: " << err
                  << std::endl;
      }
      break;
    }

    DWORD requiredSize = 0;
    SetupDiGetDeviceInterfaceDetailW(hDevInfo, &devInterfaceData, NULL, 0,
                                     &requiredSize, NULL);

    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
      std::cerr << "[-] SetupDiGetDeviceInterfaceDetailW failed. Error: "
                << GetLastError() << std::endl;
      continue;
    }

    std::vector<BYTE> buffer(requiredSize);
    PSP_DEVICE_INTERFACE_DETAIL_DATA_W pDetailData =
        reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(buffer.data());
    pDetailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

    if (!SetupDiGetDeviceInterfaceDetailW(hDevInfo, &devInterfaceData,
                                          pDetailData, requiredSize,
                                          &requiredSize, NULL)) {
      std::cerr << "[-] SetupDiGetDeviceInterfaceDetailW failed. Error: "
                << GetLastError() << std::endl;
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

    std::cerr << "[-] Failed to open VBoxUSB device. Error: " << GetLastError()
              << std::endl;
  }

  SetupDiDestroyDeviceInfoList(hDevInfo);
  return INVALID_HANDLE_VALUE;
}
inline UsbFilter CreateUsbFilter(UsbFilterType type) {
  UsbFilter filter = {};
  filter.u32Magic = USBFILTER_MAGIC;
  filter.enmType = type;
  for (int i = 0; i < static_cast<int>(USBFILTER_IDX_END); ++i) {
    filter.aFields[i * 2] = USBFILTER_MATCH_IGNORE;
  }
  return filter;
}

inline void SetFilterMatch(UsbFilter &filter, UsbFilterIdx index,
                           UsbFilterMatch match, uint16_t value) {
  filter.aFields[static_cast<int>(index) * 2] = match;
  filter.aFields[static_cast<int>(index) * 2 + 1] = value;
}

// ============================================================================
// Open VBoxUSB Device via Device Interface GUID
// ============================================================================

// ============================================================================
// USB Device Binding and Attachment Test
// ============================================================================

// Check if device is bound (exists in system)
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

  for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devInfo); ++i) {
    wchar_t instanceId[MAX_DEVICE_ID_LEN];
    if (!SetupDiGetDeviceInstanceIdW(hDevInfo, &devInfo, instanceId,
                                     MAX_DEVICE_ID_LEN, nullptr))
      continue;

    std::wstring wsId(instanceId);
    if (wsId.find(vidStr) != std::wstring::npos &&
        wsId.find(pidStr) != std::wstring::npos) {
      SetupDiDestroyDeviceInfoList(hDevInfo);
      return true;
    }
  }

  SetupDiDestroyDeviceInfoList(hDevInfo);
  return false;
}

// Check if device is attached (VBoxUSB device interface exists)
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

// Attach device by capturing it via VBoxUSBMon filter
bool AttachDevice(HANDLE hVBoxMon, uint16_t vid, uint16_t pid) {
  std::cout << "[*] Attaching device via VBoxUSBMon filter..." << std::endl;

  // Create filter with all fields
  UsbFilter filter = {};
  filter.u32Magic = USBFILTER_MAGIC;
  filter.enmType = USBFILTER_CAPTURE;

  for (int i = 0; i < USBFILTER_IDX_END; ++i) {
    filter.aFields[i * 2] = USBFILTER_MATCH_IGNORE;
  }

  filter.aFields[USBFILTER_IDX_VENDOR_ID * 2] = USBFILTER_MATCH_NUM_EXACT;
  filter.aFields[USBFILTER_IDX_VENDOR_ID * 2 + 1] = vid;

  filter.aFields[USBFILTER_IDX_PRODUCT_ID * 2] = USBFILTER_MATCH_NUM_EXACT;
  filter.aFields[USBFILTER_IDX_PRODUCT_ID * 2 + 1] = pid;

  filter.aFields[USBFILTER_IDX_DEVICE_REV * 2] = USBFILTER_MATCH_NUM_EXACT;
  filter.aFields[USBFILTER_IDX_DEVICE_REV * 2 + 1] = 0x0100;

  filter.aFields[USBFILTER_IDX_DEVICE_CLASS * 2] = USBFILTER_MATCH_NUM_EXACT;
  filter.aFields[USBFILTER_IDX_DEVICE_CLASS * 2 + 1] = 0x08;

  filter.aFields[USBFILTER_IDX_DEVICE_SUB_CLASS * 2] =
      USBFILTER_MATCH_NUM_EXACT;
  filter.aFields[USBFILTER_IDX_DEVICE_SUB_CLASS * 2 + 1] = 0x06;

  filter.aFields[USBFILTER_IDX_DEVICE_PROTOCOL * 2] = USBFILTER_MATCH_NUM_EXACT;
  filter.aFields[USBFILTER_IDX_DEVICE_PROTOCOL * 2 + 1] = 0x50;

  filter.aFields[USBFILTER_IDX_PORT * 2] = USBFILTER_MATCH_NUM_EXACT;
  filter.aFields[USBFILTER_IDX_PORT * 2 + 1] = 5;

  UsbSupFltAddOut fltOut = {};
  DWORD bytesReturned = 0;
  BOOL result = DeviceIoControl(hVBoxMon, SUPUSBFLT_IOCTL_ADD_FILTER, &filter,
                                sizeof(filter), &fltOut, sizeof(fltOut),
                                &bytesReturned, NULL);
  if (!result || fltOut.rc != 0) {
    std::cerr << "[-] Failed to add filter. RC: " << fltOut.rc << std::endl;
    return false;
  }
  std::cout << "[+] Filter added. ID: " << fltOut.uId << std::endl;

  // Run filters
  DeviceIoControl(hVBoxMon, SUPUSBFLT_IOCTL_RUN_FILTERS, NULL, 0, NULL, 0,
                  &bytesReturned, NULL);
  std::cout << "[+] Filters executed" << std::endl;

  return true;
}

// Detach device by removing filter
bool DetachDevice(HANDLE hVBoxMon, uint64_t filterId) {
  std::cout << "[*] Detaching device..." << std::endl;

  int32_t rc = -1;
  DWORD bytesReturned = 0;
  BOOL result =
      DeviceIoControl(hVBoxMon, SUPUSBFLT_IOCTL_REMOVE_FILTER, &filterId,
                      sizeof(filterId), &rc, sizeof(rc), &bytesReturned, NULL);
  if (!result || rc != 0) {
    std::cerr << "[-] Failed to remove filter. RC: " << rc << std::endl;
    return false;
  }

  std::cout << "[+] Device detached" << std::endl;
  return true;
}

// ============================================================================
// Main Test: Verify and Attach USB Device
// ============================================================================

bool TestUsbDeviceAttachment(uint16_t vid, uint16_t pid) {
  std::cout << "\n========================================" << std::endl;
  std::cout << "  USB Device Attachment Test" << std::endl;
  std::cout << "========================================" << std::endl;

  // Step 1: Check if device is bound
  std::cout << "\n--- Step 1: Check if device is bound ---" << std::endl;
  if (!IsDeviceBound(vid, pid)) {
    std::cerr << "[-] Device not bound (not connected)" << std::endl;
    std::cerr << "    Please connect the USB device" << std::endl;
    return false;
  }
  std::cout << "[+] Device is bound (connected)" << std::endl;

  // Step 2: Check if device is attached
  std::cout << "\n--- Step 2: Check if device is attached ---" << std::endl;
  if (IsDeviceAttached()) {
    std::cout << "[+] Device is already attached" << std::endl;
    return true;
  }
  std::cout << "[*] Device not attached, will attach now..." << std::endl;

  // Step 3: Open VBoxUSBMon
  std::cout << "\n--- Step 3: Attach device via VBoxUSBMon ---" << std::endl;
  HANDLE hVBoxMon =
      CreateFileW(VBOXUSBMON_DEVICE_NAME, FILE_READ_DATA | FILE_WRITE_DATA,
                  FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                  FILE_FLAG_OVERLAPPED, NULL);

  if (hVBoxMon == INVALID_HANDLE_VALUE) {
    std::cerr << "[-] Failed to open VBoxUSBMon" << std::endl;
    return false;
  }

  // Step 4: Attach device
  if (!AttachDevice(hVBoxMon, vid, pid)) {
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

  CloseHandle(hVBoxMon);

  if (!attached) {
    std::cerr << "[-] VBoxUSB device did not appear after attachment"
              << std::endl;
    std::cerr << "    This may be because:" << std::endl;
    std::cerr << "    1. VBoxUSB device driver is not creating the interface"
              << std::endl;
    std::cerr
        << "    2. A client connection is required to trigger device creation"
        << std::endl;
    return false;
  }

  std::cout << "[+] Device attached successfully!" << std::endl;
  return true;
}



bool CaptureDeviceAndWaitForVBoxUsb(HANDLE hVBoxMon, uint16_t vid,
                                    uint16_t pid) {
  std::cout << "[*] Capturing device via VBoxUSBMon..." << std::endl;

  // Create filter with ALL fields (matching C# code)
  UsbFilter filter = {};
  filter.u32Magic = USBFILTER_MAGIC;
  filter.enmType = USBFILTER_CAPTURE;

  // Initialize all fields to IGNORE
  for (int i = 0; i < USBFILTER_IDX_END; ++i) {
    filter.aFields[i * 2] = USBFILTER_MATCH_IGNORE;
  }

  // Set filter fields (matching C# code exactly)
  filter.aFields[USBFILTER_IDX_VENDOR_ID * 2] = USBFILTER_MATCH_NUM_EXACT;
  filter.aFields[USBFILTER_IDX_VENDOR_ID * 2 + 1] = vid;

  filter.aFields[USBFILTER_IDX_PRODUCT_ID * 2] = USBFILTER_MATCH_NUM_EXACT;
  filter.aFields[USBFILTER_IDX_PRODUCT_ID * 2 + 1] = pid;

  filter.aFields[USBFILTER_IDX_DEVICE_REV * 2] = USBFILTER_MATCH_NUM_EXACT;
  filter.aFields[USBFILTER_IDX_DEVICE_REV * 2 + 1] = 0x0100; // bcdDevice

  filter.aFields[USBFILTER_IDX_DEVICE_CLASS * 2] = USBFILTER_MATCH_NUM_EXACT;
  filter.aFields[USBFILTER_IDX_DEVICE_CLASS * 2 + 1] = 0x08; // Mass Storage

  filter.aFields[USBFILTER_IDX_DEVICE_SUB_CLASS * 2] =
      USBFILTER_MATCH_NUM_EXACT;
  filter.aFields[USBFILTER_IDX_DEVICE_SUB_CLASS * 2 + 1] = 0x06; // SCSI

  filter.aFields[USBFILTER_IDX_DEVICE_PROTOCOL * 2] = USBFILTER_MATCH_NUM_EXACT;
  filter.aFields[USBFILTER_IDX_DEVICE_PROTOCOL * 2 + 1] = 0x50; // Bulk-Only

  filter.aFields[USBFILTER_IDX_PORT * 2] = USBFILTER_MATCH_NUM_EXACT;
  filter.aFields[USBFILTER_IDX_PORT * 2 + 1] = 5;

  UsbSupFltAddOut fltOut = {};
  DWORD bytesReturned = 0;
  BOOL result = DeviceIoControl(hVBoxMon, SUPUSBFLT_IOCTL_ADD_FILTER, &filter,
                                sizeof(filter), &fltOut, sizeof(fltOut),
                                &bytesReturned, NULL);
  if (!result || fltOut.rc != 0) {
    std::cerr << "[-] Failed to add filter. RC: " << fltOut.rc << std::endl;
    return false;
  }
  std::cout << "[+] Filter added. ID: " << fltOut.uId << std::endl;

  // Run filters
  DeviceIoControl(hVBoxMon, SUPUSBFLT_IOCTL_RUN_FILTERS, NULL, 0, NULL, 0,
                  &bytesReturned, NULL);
  std::cout << "[+] Filters executed" << std::endl;

  // Wait for VBoxUSB device to appear
  std::cout << "[*] Waiting for VBoxUSB device to appear..." << std::endl;
  for (int i = 0; i < 20; ++i) {
    Sleep(500);
    HANDLE hVBoxUsb = OpenVBoxUsbDevice();
    if (hVBoxUsb != INVALID_HANDLE_VALUE) {
      std::cout << "[+] VBoxUSB device appeared!" << std::endl;
      CloseHandle(hVBoxUsb);
      return true;
    }
    std::cout << "." << std::flush;
  }
  std::cout << std::endl;

  std::cerr << "[-] VBoxUSB device did not appear" << std::endl;
  return false;
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

  DWORD bytesReturned = 0;
  BOOL result;

  // TEST 1: GET_VERSION
  std::cout << "\n--- Test 1: SUPUSBFLT_IOCTL_GET_VERSION ---" << std::endl;
  {
    UsbSupVersion ver = {};
    result = DeviceIoControl(hVBoxMon, SUPUSBFLT_IOCTL_GET_VERSION, NULL, 0,
                             &ver, sizeof(ver), &bytesReturned, NULL);
    if (result) {
      std::cout << "[+] Version: " << ver.major << "." << ver.minor
                << std::endl;
    } else {
      std::cerr << "[-] GET_VERSION failed. Error: " << GetLastError()
                << std::endl;
    }
  }

  // TEST 2: ADD_FILTER
  std::cout << "\n--- Test 2: SUPUSBFLT_IOCTL_ADD_FILTER ---" << std::endl;
  uint64_t filterId = 0;
  {
    UsbFilter filter = CreateUsbFilter(USBFILTER_CAPTURE);
    SetFilterMatch(filter, USBFILTER_IDX_VENDOR_ID, USBFILTER_MATCH_NUM_EXACT,
                   0x0781);
    SetFilterMatch(filter, USBFILTER_IDX_PRODUCT_ID, USBFILTER_MATCH_NUM_EXACT,
                   0x558a);
    SetFilterMatch(filter, USBFILTER_IDX_PORT, USBFILTER_MATCH_NUM_EXACT, 5);

    UsbSupFltAddOut fltOut = {};
    result = DeviceIoControl(hVBoxMon, SUPUSBFLT_IOCTL_ADD_FILTER, &filter,
                             sizeof(filter), &fltOut, sizeof(fltOut),
                             &bytesReturned, NULL);
    if (result) {
      std::cout << "[+] Filter added. ID: " << fltOut.uId
                << ", RC: " << fltOut.rc << std::endl;
      filterId = fltOut.uId;
    } else {
      std::cerr << "[-] ADD_FILTER failed. Error: " << GetLastError()
                << std::endl;
    }
  }

  // TEST 3: RUN_FILTERS
  std::cout << "\n--- Test 3: SUPUSBFLT_IOCTL_RUN_FILTERS ---" << std::endl;
  {
    result = DeviceIoControl(hVBoxMon, SUPUSBFLT_IOCTL_RUN_FILTERS, NULL, 0,
                             NULL, 0, &bytesReturned, NULL);
    if (result) {
      std::cout << "[+] RUN_FILTERS succeeded" << std::endl;
    } else {
      std::cerr << "[-] RUN_FILTERS failed. Error: " << GetLastError()
                << std::endl;
    }
  }

  // TEST 4: REMOVE_FILTER
  std::cout << "\n--- Test 4: SUPUSBFLT_IOCTL_REMOVE_FILTER ---" << std::endl;
  if (filterId != 0) {
    int32_t rc = -1;
    result = DeviceIoControl(hVBoxMon, SUPUSBFLT_IOCTL_REMOVE_FILTER, &filterId,
                             sizeof(filterId), &rc, sizeof(rc), &bytesReturned,
                             NULL);
    if (result) {
      std::cout << "[+] Filter removed. RC: " << rc << std::endl;
    } else {
      std::cerr << "[-] REMOVE_FILTER failed. Error: " << GetLastError()
                << std::endl;
    }
  }

  CloseHandle(hVBoxMon);
  std::cout << "\n[+] VBoxUsbMon handle closed" << std::endl;
}

bool TestVBoxUsbDevice() {
  std::cout << "\n========================================" << std::endl;
  std::cout << "  VBoxUsb Device Driver Tests" << std::endl;
  std::cout << "========================================" << std::endl;

  // First, capture the device via VBoxUSBMon
  HANDLE hVBoxMon =
      CreateFileW(VBOXUSBMON_DEVICE_NAME, FILE_READ_DATA | FILE_WRITE_DATA,
                  FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                  FILE_FLAG_OVERLAPPED, NULL);

  if (hVBoxMon == INVALID_HANDLE_VALUE) {
    std::cerr << "[-] Failed to open VBoxUSBMon" << std::endl;
    return false;
  }

  // Capture device and wait for VBoxUSB device
  if (!CaptureDeviceAndWaitForVBoxUsb(hVBoxMon, 0x0781, 0x558a)) {
    std::cerr << "[-] VBoxUSB device not available after capture" << std::endl;
    CloseHandle(hVBoxMon);
    return false;
  }

  CloseHandle(hVBoxMon);

  // Now test VBoxUSB device
  HANDLE hVBoxUsb = OpenVBoxUsbDevice();
  if (hVBoxUsb == INVALID_HANDLE_VALUE) {
    std::cerr << "[-] VBoxUsb device not available - skipping device tests"
              << std::endl;
    return false;
  }
  std::cout << "[+] VBoxUsb device handle opened successfully" << std::endl;

  DWORD bytesReturned = 0;
  BOOL result;

  // TEST 5: GET_VERSION
  std::cout << "\n--- Test 5: SUPUSB_IOCTL_GET_VERSION ---" << std::endl;
  {
    UsbSupVersion ver = {};
    result = DeviceIoControl(hVBoxUsb, SUPUSB_IOCTL_GET_VERSION, NULL, 0, &ver,
                             sizeof(ver), &bytesReturned, NULL);
    if (result) {
      std::cout << "[+] Version: " << ver.major << "." << ver.minor
                << std::endl;
    } else {
      std::cerr << "[-] GET_VERSION failed. Error: " << GetLastError()
                << std::endl;
    }
  }

  // TEST 6: USB_CLAIM_DEVICE
  std::cout << "\n--- Test 6: SUPUSB_IOCTL_USB_CLAIM_DEVICE ---" << std::endl;
  {
    UsbSupClaimDev claimDev = {};
    result = DeviceIoControl(hVBoxUsb, SUPUSB_IOCTL_USB_CLAIM_DEVICE, &claimDev,
                             sizeof(claimDev), &claimDev, sizeof(claimDev),
                             &bytesReturned, NULL);
    if (result) {
      std::cout << "[+] Claim result: "
                << (claimDev.fClaimed ? "CLAIMED" : "NOT CLAIMED") << std::endl;
    } else {
      std::cerr << "[-] USB_CLAIM_DEVICE failed. Error: " << GetLastError()
                << std::endl;
    }
  }

  // TEST 7: IS_OPERATIONAL
  std::cout << "\n--- Test 7: SUPUSB_IOCTL_IS_OPERATIONAL ---" << std::endl;
  {
    uint32_t operational = 0;
    result = DeviceIoControl(hVBoxUsb, SUPUSB_IOCTL_IS_OPERATIONAL, NULL, 0,
                             &operational, sizeof(operational), &bytesReturned,
                             NULL);
    if (result) {
      std::cout << "[+] Is operational: " << (operational ? "YES" : "NO")
                << std::endl;
    } else {
      std::cerr << "[-] IS_OPERATIONAL failed. Error: " << GetLastError()
                << std::endl;
    }
  }

  // TEST 8: SEND_URB (Bulk IN)
  std::cout << "\n--- Test 8: SUPUSB_IOCTL_SEND_URB (Bulk IN) ---" << std::endl;
  {
    uint8_t buffer[64] = {};
    UsbSupUrb urb = {};
    urb.type = USBSUP_TRANSFER_TYPE_BULK;
    urb.ep = 0x81;
    urb.dir = USBSUP_DIRECTION_IN;
    urb.flags = USBSUP_FLAG_NONE;
    urb.len = sizeof(buffer);
    urb.buf = reinterpret_cast<uint64_t>(buffer);
    urb.numIsoPkts = 0;

    result = DeviceIoControl(hVBoxUsb, SUPUSB_IOCTL_SEND_URB, &urb, sizeof(urb),
                             &urb, sizeof(urb), &bytesReturned, NULL);
    if (result) {
      std::cout << "[+] URB sent. Error: " << urb.error
                << ", Length: " << urb.len << std::endl;
    } else {
      std::cerr << "[-] SEND_URB failed. Error: " << GetLastError()
                << std::endl;
    }
  }

  // TEST 9: USB_RELEASE_DEVICE
  std::cout << "\n--- Test 9: SUPUSB_IOCTL_USB_RELEASE_DEVICE ---" << std::endl;
  {
    result = DeviceIoControl(hVBoxUsb, SUPUSB_IOCTL_USB_RELEASE_DEVICE, NULL, 0,
                             NULL, 0, &bytesReturned, NULL);
    if (result) {
      std::cout << "[+] USB_RELEASE_DEVICE succeeded" << std::endl;
    } else {
      std::cerr << "[-] USB_RELEASE_DEVICE failed. Error: " << GetLastError()
                << std::endl;
    }
  }

  CloseHandle(hVBoxUsb);
  std::cout << "\n[+] VBoxUsb device handle closed" << std::endl;
  return true;
}
#endif

// ============================================================================
// Main
// ============================================================================

// ============================================================================
// Updated Main
// ============================================================================

int wmain(int argc, wchar_t *argv[]) {
  std::cout << "========================================" << std::endl;
  std::cout << "  VBoxUSB IOCTL Test Suite" << std::endl;
  std::cout << "========================================" << std::endl;

  // Test 1: VBoxUsbMon Filter Driver Tests
  TestVBoxUsbMon();

  // Test 2: List, Bind, Attach USB Device
  if (TestListBindAttach()) {
    // Test 3: VBoxUsb Device Driver Tests (only if attachment succeeded)
    TestVBoxUsbDevice();
  } else {
    std::cout << "\n[!] Skipping VBoxUsb device tests (attachment failed)"
              << std::endl;
  }

  std::cout << "\n========================================" << std::endl;
  std::cout << "  All tests completed" << std::endl;
  std::cout << "========================================" << std::endl;

  return 0;
}