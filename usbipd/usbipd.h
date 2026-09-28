#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <cstdint>
#include <string>
#include <vector>

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <cfgmgr32.h>
#include <initguid.h>
#include <setupapi.h>

#ifndef CM_REGKEY_HARDWARE
#define CM_REGKEY_HARDWARE 0x00000000
#endif

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "cfgmgr32.lib")

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

#pragma pack(pop)

bool ReceiveExactBytes(SOCKET s, char *buffer, int bytesToRead);
bool AttachVBoxFilterToDevice(DEVINST devInst);
bool GetUsbDeviceNodeAndParams(uint16_t targetVid, uint16_t targetPid,
                               uint8_t &outBus, uint8_t &outAddress,
                               DEVINST &outDevInst,
                               std::wstring &outInstanceId);
HANDLE OpenVBoxDeviceFilterHandle(uint16_t vid, uint16_t pid);
std::vector<USBIP_DEVICE_DESC> ScanPhysicalUsbBus();
void ConnectionWorkerThread(SOCKET clientSocket);