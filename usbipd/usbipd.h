#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <cstdint>
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

// --- USB DESCRIPTOR STRUCTURES ---

struct USB_DEVICE_DESCRIPTOR {
  uint8_t bLength;
  uint8_t bDescriptorType;
  uint16_t bcdUSB;
  uint8_t bDeviceClass;
  uint8_t bDeviceSubClass;
  uint8_t bDeviceProtocol;
  uint8_t bMaxPacketSize0;
  uint16_t idVendor;
  uint16_t idProduct;
  uint16_t bcdDevice;
  uint8_t iManufacturer;
  uint8_t iProduct;
  uint8_t iSerialNumber;
  uint8_t bNumConfigurations;
};

struct USB_CONFIGURATION_DESCRIPTOR {
  uint8_t bLength;
  uint8_t bDescriptorType;
  uint16_t wTotalLength;
  uint8_t bNumInterfaces;
  uint8_t bConfigurationValue;
  uint8_t iConfiguration;
  uint8_t bmAttributes;
  uint8_t bMaxPower;
};

struct USB_INTERFACE_DESCRIPTOR {
  uint8_t bLength;
  uint8_t bDescriptorType;
  uint8_t bInterfaceNumber;
  uint8_t bAlternateSetting;
  uint8_t bNumEndpoints;
  uint8_t bInterfaceClass;
  uint8_t bInterfaceSubClass;
  uint8_t bInterfaceProtocol;
  uint8_t iInterface;
};

struct USB_ENDPOINT_DESCRIPTOR {
  uint8_t bLength;
  uint8_t bDescriptorType;
  uint8_t bEndpointAddress;
  uint8_t bmAttributes;
  uint16_t wMaxPacketSize;
  uint8_t bInterval;
};

struct USB_FULL_CONFIG_PACKET {
  USB_CONFIGURATION_DESCRIPTOR config;
  USB_INTERFACE_DESCRIPTOR interface0;
  USB_ENDPOINT_DESCRIPTOR epIn;
  USB_ENDPOINT_DESCRIPTOR epOut;
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

bool ReceiveExactBytes(SOCKET s, char *buffer, int bytesToRead);
bool IsDeviceAuthorizedInRegistry(const std::string &hardwareId);
std::vector<USBIP_DEVICE_DESC> ScanPhysicalUsbBus();
void ConnectionWorkerThread(SOCKET clientSocket);