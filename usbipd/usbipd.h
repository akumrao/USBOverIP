#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <initguid.h>
#include <string>
#include <vector>
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

typedef VOID *HDEVINFO;
#define DIGCF_PRESENT 0x00000002
#define MAX_DEVICE_ID_LEN 200
#define USBIP_PORT "3240"
#define DEFAULT_BUFLEN 1024

#define SWAP16(x) htons(x)
#define SWAP32(x) htonl(x)

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

struct USBIP_OP_REQ_IMPORT {
  USBIP_OP_COMMON common;
  char busid[32];
};

struct USBIP_OP_REP_IMPORT {
  USBIP_OP_COMMON common;
  USBIP_DEVICE_DESC dev;
};

struct SP_DEVINFO_DATA {
  DWORD cbSize;
  GUID ClassGuid;
  DWORD DevInst;
  ULONG_PTR Reserved;
};
#pragma pack(pop)

extern "C" {
__declspec(dllimport) HDEVINFO __stdcall SetupDiGetClassDevsW(
    const GUID *ClassGuid, PCWSTR Enumerator, HWND hwndParent, DWORD Flags);
__declspec(dllimport)
BOOL __stdcall SetupDiEnumDeviceInfo(HDEVINFO DeviceInfoSet, DWORD MemberIndex,
                                     SP_DEVINFO_DATA *DeviceInfoData);
__declspec(dllimport) BOOL __stdcall SetupDiGetDeviceInstanceIdW(
    HDEVINFO DeviceInfoSet, SP_DEVINFO_DATA *DeviceInfoData,
    PWSTR DeviceInstanceId, DWORD DeviceInstanceIdSize, PDWORD RequiredSize);
__declspec(dllimport)
BOOL __stdcall SetupDiDestroyDeviceInfoList(HDEVINFO DeviceInfoSet);
}

// Global Core Implementation APIs
bool IsDeviceAuthorizedInRegistry(const std::string &hardwareId);
std::vector<USBIP_DEVICE_DESC> ScanPhysicalUsbBus();
void ConnectionWorkerThread(SOCKET clientSocket);
