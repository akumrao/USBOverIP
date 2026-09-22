// 1. CRITICAL: Prevent old WinSock 1 conflicts
#define WIN32_LEAN_AND_MEAN

// 2. Network headers MUST come first
#include <winsock2.h>
#include <ws2tcpip.h>

// 3. System and Hardware APIs
#include <cfgmgr32.h>
#include <devguid.h>
#include <iomanip>
#include <iostream>
#include <setupapi.h>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "ws2_32.lib")

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

std::string WideToString(const std::wstring &wstr) {
  if (wstr.empty())
    return "";

  int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(),
                                 NULL, 0, NULL, NULL);
  std::string str(size, 0);

  WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &str[0], size,
                      NULL, NULL);
  return str;
}

// Check if a specific BUSID is recorded as bound in the registry
bool IsDeviceBound(const std::string &busId) {
  HKEY hKey;
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, REG_BOUND_PATH, 0, KEY_READ, &hKey) !=
      ERROR_SUCCESS) {
    return false;
  }
  std::wstring wBusId(busId.begin(), busId.end());
  DWORD val = 0;
  DWORD size = sizeof(val);
  LONG result =
      RegQueryValueExW(hKey, wBusId.c_str(), NULL, NULL, (LPBYTE)&val, &size);
  RegCloseKey(hKey);
  return (result == ERROR_SUCCESS);
}

void ExecuteBind(const std::string &busId) {
  HKEY hKey;
  LONG result =
      RegCreateKeyExW(HKEY_LOCAL_MACHINE, REG_BOUND_PATH, 0, NULL,
                      REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL);
  if (result != ERROR_SUCCESS) {
    std::cerr
        << "Error: Failed to access registry. Please run as Administrator."
        << std::endl;
    return;
  }

  std::wstring wBusId(busId.begin(), busId.end());
  DWORD val = 1;
  result = RegSetValueExW(hKey, wBusId.c_str(), 0, REG_DWORD,
                          (const BYTE *)&val, sizeof(val));
  RegCloseKey(hKey);

  if (result == ERROR_SUCCESS) {
    std::cout << "info: bind successful for busid " << busId << std::endl;
  } else {
    std::cerr << "Error: Could not save binding configuration for " << busId
              << std::endl;
  }
}

void ExecuteUnbind(const std::string &busId) {
  HKEY hKey;
  LONG result =
      RegOpenKeyExW(HKEY_LOCAL_MACHINE, REG_BOUND_PATH, 0, KEY_WRITE, &hKey);
  if (result != ERROR_SUCCESS) {
    std::cerr << "Error: No bound devices found or run as Administrator."
              << std::endl;
    return;
  }

  std::wstring wBusId(busId.begin(), busId.end());
  result = RegDeleteValueW(hKey, wBusId.c_str());
  RegCloseKey(hKey);

  if (result == ERROR_SUCCESS) {
    std::cout << "info: unbind successful for busid " << busId << std::endl;
  } else {
    std::cerr << "Error: Busid " << busId << " was not found in the bound list."
              << std::endl;
  }
}

void ExecuteDeviceListing() {
  HDEVINFO devInfo =
      SetupDiGetClassDevsW(&GUID_DEVCLASS_USB, NULL, NULL, DIGCF_PRESENT);
  if (devInfo == INVALID_HANDLE_VALUE) {
    std::cerr << "Failed to fetch Windows USB class device registry tree."
              << std::endl;
    return;
  }

  SP_DEVINFO_DATA devInfoData;
  devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

  std::cout << "Connected:" << std::endl;
  std::cout << std::left << std::setw(8) << "BUSID" << std::setw(11)
            << "VID:PID" << std::setw(30) << "DEVICE"
            << "STATE" << std::endl;

  for (DWORD i = 0; SetupDiEnumDeviceInfo(devInfo, i, &devInfoData); i++) {
    wchar_t buffer[MAX_DEVICE_ID_LEN] = {0};
    std::string description = "USB Input Device";
    std::string busId = "1-1";
    std::string vidPid = "0000:0000";

    if (SetupDiGetDeviceRegistryPropertyW(devInfo, &devInfoData,
                                          SPDRP_DEVICEDESC, NULL, (PBYTE)buffer,
                                          sizeof(buffer), NULL)) {
      description = WideToString(buffer);
    }

    if (CM_Get_Device_IDW(devInfoData.DevInst, buffer, MAX_DEVICE_ID_LEN, 0) ==
        CR_SUCCESS) {
      std::wstring wInstanceId(buffer);

      size_t vidPos = wInstanceId.find(L"VID_");
      size_t pidPos = wInstanceId.find(L"PID_");
      if (vidPos != std::wstring::npos && pidPos != std::wstring::npos &&
          wInstanceId.length() >= vidPos + 8 &&
          wInstanceId.length() >= pidPos + 8) {
        std::wstring wVid = wInstanceId.substr(vidPos + 4, 4);
        std::wstring wPid = wInstanceId.substr(pidPos + 4, 4);
        vidPid = WideToString(wVid) + ":" + WideToString(wPid);
        for (auto &c : vidPid)
          c = (char)tolower(c);
      }

      size_t slashIndex = wInstanceId.find_last_of(L"\\");
      if (slashIndex != std::wstring::npos) {
        std::wstring wBusId = wInstanceId.substr(slashIndex + 1, 5);
        busId = WideToString(wBusId);
      }
    }

    bool isShared = IsDeviceBound(busId);

    std::cout << std::left << std::setw(8) << busId << std::setw(11) << vidPid
              << std::setw(30) << description
              << (isShared ? "Shared" : "Not shared") << std::endl;
  }

  std::cout << "\nPersisted:" << std::endl;
  std::cout << std::left << std::setw(38) << "GUID"
            << "DEVICE" << std::endl;

  SetupDiDestroyDeviceInfoList(devInfo);
}

bool PopulateImportDetails(const std::string &targetBusId,
                           op_rep_import &reply) {
  reply.header.version = htons(USBIP_VERSION);
  reply.header.command = htons(0x0003);
  reply.header.status = 0;
  strcpy_s(reply.busid, targetBusId.c_str());
  strcpy_s(reply.path, "/sys/devices/virtual/usbip_win2/dev0");
  reply.speed = htonl(3);
  reply.idVendor = htons(0x045E);
  reply.idProduct = htons(0x028E);
  return true;
}

void MaintainDataTunnel(SOCKET clientSocket) {
  while (true) {
    usbip_header_basic commonPrefix;
    if (recv(clientSocket, (char *)&commonPrefix, sizeof(usbip_header_basic),
             MSG_WAITALL) <= 0)
      break;

    uint32_t typeCode = ntohl(commonPrefix.command);
    uint32_t uniqueSeq = ntohl(commonPrefix.seqnum);

    if (typeCode == 0x00000001) {
      usbip_cmd_submit incomingUrb;
      memcpy(&incomingUrb.basic, &commonPrefix, sizeof(usbip_header_basic));
      recv(clientSocket, (char *)&incomingUrb + sizeof(usbip_header_basic),
           sizeof(usbip_cmd_submit) - sizeof(usbip_header_basic), MSG_WAITALL);

      uint32_t bufferLen = ntohl(incomingUrb.transfer_buffer_length);
      uint32_t ioDirection = ntohl(incomingUrb.basic.direction);
      std::vector<char> transferPayload(bufferLen);
      if (ioDirection == 0 && bufferLen > 0)
        recv(clientSocket, transferPayload.data(), bufferLen, MSG_WAITALL);

      usbip_ret_submit feedbackFrame{};
      feedbackFrame.basic.command = htonl(0x00000003);
      feedbackFrame.basic.seqnum = htonl(uniqueSeq);
      feedbackFrame.status = htonl(0);
      feedbackFrame.actual_length = htonl(bufferLen);

      send(clientSocket, (char *)&feedbackFrame, sizeof(usbip_ret_submit), 0);
      if (ioDirection == 1 && bufferLen > 0)
        send(clientSocket, transferPayload.data(), bufferLen, 0);
    }
  }
  closesocket(clientSocket);
}

void ExecuteProtocolEngine(SOCKET clientSocket) {
  usbip_header clientIntention;
  if (recv(clientSocket, (char *)&clientIntention, sizeof(usbip_header),
           MSG_WAITALL) <= 0) {
    closesocket(clientSocket);
    return;
  }
  uint16_t parsedCmd = ntohs(clientIntention.command);
  if (parsedCmd == 0x8005) {
    op_rep_devlist emptyCatalog{};
    emptyCatalog.header.version = htons(USBIP_VERSION);
    emptyCatalog.header.command = htons(0x0005);
    emptyCatalog.num_devices = htonl(0);
    send(clientSocket, (char *)&emptyCatalog, sizeof(emptyCatalog), 0);
    closesocket(clientSocket);
  } else if (parsedCmd == 0x8003) {
    char requestedId[32] = {0};
    recv(clientSocket, requestedId, 32, MSG_WAITALL);
    op_rep_import mappingResponse{};
    if (PopulateImportDetails(requestedId, mappingResponse)) {
      send(clientSocket, (char *)&mappingResponse, sizeof(op_rep_import), 0);
      MaintainDataTunnel(clientSocket);
    } else {
      closesocket(clientSocket);
    }
  }
}

void StartServerDaemon() {
  WSADATA networkData;
  WSAStartup(MAKEWORD(2, 2), &networkData);
  struct addrinfo requirements{}, *resolution = NULL;
  requirements.ai_family = AF_INET;
  requirements.ai_socktype = SOCK_STREAM;
  requirements.ai_protocol = IPPROTO_TCP;
  requirements.ai_flags = AI_PASSIVE;
  getaddrinfo(NULL, USBIP_PORT, &requirements, &resolution);
  SOCKET hostingSocket = socket(resolution->ai_family, resolution->ai_socktype,
                                resolution->ai_protocol);
  bind(hostingSocket, resolution->ai_addr, (int)resolution->ai_addrlen);
  listen(hostingSocket, SOMAXCONN);
  freeaddrinfo(resolution);

  std::cout << "[ONLINE] USBIP Server Engine listening on port " << USBIP_PORT
            << "..." << std::endl;
  while (true) {
    SOCKET acceptedClient = accept(hostingSocket, NULL, NULL);
    if (acceptedClient != INVALID_SOCKET) {
      std::thread(ExecuteProtocolEngine, acceptedClient).detach();
    }
  }
  closesocket(hostingSocket);
  WSACleanup();
}

int main(int argc, char *argv[]) {
  if (argc > 1) {
    std::string argument = argv[1];
    if (argument == "--list" || argument == "-l") {
      ExecuteDeviceListing();
      return 0;
    } else if (argument == "--bind" || argument == "-b") {
      if (argc > 2) {
        ExecuteBind(argv[2]);
        return 0;
      } else {
        std::cerr
            << "Error: --bind requires a BUSID argument (e.g., --bind 1-5)"
            << std::endl;
        return 1;
      }
    } else if (argument == "--unbind" || argument == "-u") {
      if (argc > 2) {
        ExecuteUnbind(argv[2]);
        return 0;
      } else {
        std::cerr
            << "Error: --unbind requires a BUSID argument (e.g., --unbind 1-5)"
            << std::endl;
        return 1;
      }
    } else {
      std::cout << "Unknown option: " << argument << "\n\n"
                << "Usage:\n"
                << "  usbip_server.exe --list\n"
                << "  usbip_server.exe --bind <BUSID>\n"
                << "  usbip_server.exe --unbind <BUSID>\n"
                << std::endl;
      return 1;
    }
  }

  StartServerDaemon();
  return 0;
}