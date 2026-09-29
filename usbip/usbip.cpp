#if 0

#define WIN32_LEAN_AND_MEAN
#include <iostream>
#include <string>
#include <vector>
#include <windows.h>
#include <winioctl.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

#define USBIP_PORT "3240"
#define USBIP_VERSION 0x0111

// Control Codes expected by usbip_vhci.sys driver
#define IOCTL_USBIP_VHCI_ATTACH                                                \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_USBIP_VHCI_DETACH                                                \
  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)

#pragma pack(push, 1)
struct usbip_header {
    uint16_t version;
    uint16_t command;
    uint32_t status;
  };

  // Single atomic 40-byte import request (Header + 32-byte BusID)
  struct op_req_import_payload {
    usbip_header header;
    char busid[32];
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

  // IOCTL payload structure during ATTACH
  struct vhci_ioctl_attach {
    ULONG port;
    ULONG status;
    SOCKET socket; // Transferred TCP socket handle
    ULONG speed;
    ULONG devid;
  };

  // IOCTL payload structure during DETACH
  struct vhci_ioctl_detach {
    ULONG port;
  };
#pragma pack(pop)

  // Check for elevated Administrator privileges
  static bool IsRunningAsAdmin() {
    BOOL isAdmin = FALSE;
    PSID adminGroup = NULL;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                 DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0,
                                 &adminGroup)) {
      CheckTokenMembership(NULL, adminGroup, &isAdmin);
      FreeSid(adminGroup);
    }
    return isAdmin == TRUE;
  }

  class UsbIpClient {
  public:
    UsbIpClient(const std::string &hostIp) : m_hostIp(hostIp) {}

    // Utility to create a new TCP socket connection
    SOCKET CreateNewSocket() {
      WSADATA wsaData;
      if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        return INVALID_SOCKET;

      struct addrinfo hints{}, *result = nullptr;
      hints.ai_family = AF_INET;
      hints.ai_socktype = SOCK_STREAM;
      hints.ai_protocol = IPPROTO_TCP;

      if (getaddrinfo(m_hostIp.c_str(), USBIP_PORT, &hints, &result) != 0)
        return INVALID_SOCKET;

      SOCKET sock =
          socket(result->ai_family, result->ai_socktype, result->ai_protocol);
      if (sock == INVALID_SOCKET) {
        freeaddrinfo(result);
        return INVALID_SOCKET;
      }

      if (connect(sock, result->ai_addr, (int)result->ai_addrlen) ==
          SOCKET_ERROR) {
        closesocket(sock);
        freeaddrinfo(result);
        return INVALID_SOCKET;
      }

      freeaddrinfo(result);
      return sock;
    }

    // Attempt to open handle across known symbolic link aliases for VHCI
    static HANDLE OpenVhciDeviceHandle() {
      const wchar_t *driverSymbolicNames[] = {
          L"\\\\.\\USBIP_VHCI", L"\\\\.\\usbip_vhci", L"\\\\.\\UsbipVhci"};

      for (const auto *devicePath : driverSymbolicNames) {
        HANDLE hDevice = CreateFileW(devicePath, GENERIC_READ | GENERIC_WRITE,
                                     0, NULL, OPEN_EXISTING, 0, NULL);

        if (hDevice != INVALID_HANDLE_VALUE) {
          return hDevice;
        }
      }

      return INVALID_HANDLE_VALUE;
    }

    // Query shared devices on connection 1
    bool RequestDeviceList() {
      SOCKET querySock = CreateNewSocket();
      if (querySock == INVALID_SOCKET) {
        std::cerr
            << "[CLIENT ERROR] Failed to connect to server for DEVLIST query."
            << std::endl;
        return false;
      }

      usbip_header req{};
      req.version = htons(USBIP_VERSION);
      req.command = htons(0x8005); // OP_REQ_DEVLIST
      req.status = 0;

      if (send(querySock, (char *)&req, sizeof(req), 0) <= 0) {
        closesocket(querySock);
        return false;
      }

      op_rep_devlist rep{};
      if (recv(querySock, (char *)&rep, sizeof(rep), MSG_WAITALL) <= 0) {
        closesocket(querySock);
        return false;
      }

      uint32_t deviceCount = ntohl(rep.num_devices);
      std::cout << "[CLIENT] Host reports " << deviceCount
                << " shared device(s)." << std::endl;

      closesocket(querySock);
      return true;
    }

    // Import device on a fresh connection 2
    bool ImportAndMountDevice(const std::string &busId) {
      SOCKET importSock = CreateNewSocket();
      if (importSock == INVALID_SOCKET) {
        std::cerr
            << "[CLIENT ERROR] Failed to open connection for IMPORT operation."
            << std::endl;
        return false;
      }

      op_req_import_payload importReq{};
      importReq.header.version = htons(USBIP_VERSION);
      importReq.header.command = htons(0x8003); // OP_REQ_IMPORT
      importReq.header.status = 0;

      strcpy_s(importReq.busid, sizeof(importReq.busid), busId.c_str());

      // Send atomic frame
      if (send(importSock, (char *)&importReq, sizeof(importReq), 0) <= 0) {
        std::cerr << "[CLIENT ERROR] Failed to send IMPORT request frame."
                  << std::endl;
        closesocket(importSock);
        return false;
      }

      op_rep_import rep{};
      if (recv(importSock, (char *)&rep, sizeof(rep), MSG_WAITALL) <= 0) {
        std::cerr << "[CLIENT ERROR] Server disconnected before sending IMPORT "
                     "response."
                  << std::endl;
        closesocket(importSock);
        return false;
      }

      if (ntohs(rep.header.status) == 0) {
        std::cout << "\n=========================================="
                  << std::endl;
        std::cout << "[SUCCESS] Import Handshake Complete!" << std::endl;
        std::cout << "  BUS ID   : " << rep.busid << std::endl;
        std::cout << "  VID:PID  : " << std::hex << std::uppercase
                  << ntohs(rep.idVendor) << ":" << ntohs(rep.idProduct)
                  << std::dec << std::endl;
        std::cout << "==========================================\n"
                  << std::endl;

        return AttachToVhciDriver(importSock, rep.speed);
      } else {
        std::cerr
            << "[CLIENT ERROR] Server rejected import request for BUS ID: "
            << busId << std::endl;
        closesocket(importSock);
        return false;
      }
    }

    static bool DetachVirtualDevice(ULONG portNumber) {
      HANDLE hVhci = OpenVhciDeviceHandle();
      if (hVhci == INVALID_HANDLE_VALUE) {
        std::cerr << "[CLIENT ERROR] Could not open VHCI handle to detach port "
                  << portNumber << "." << std::endl;
        return false;
      }

      vhci_ioctl_detach detachParams{portNumber};
      DWORD bytesReturned = 0;
      BOOL ioctlSuccess =
          DeviceIoControl(hVhci, IOCTL_USBIP_VHCI_DETACH, &detachParams,
                          sizeof(detachParams), NULL, 0, &bytesReturned, NULL);

      CloseHandle(hVhci);

      if (ioctlSuccess) {
        std::cout << "[SUCCESS] Detached virtual USB device from port "
                  << portNumber << std::endl;
        return true;
      } else {
        std::cerr << "[CLIENT ERROR] DeviceIoControl DETACH failed (Error: "
                  << GetLastError() << ")." << std::endl;
        return false;
      }
    }

  private:
    std::string m_hostIp;

    bool AttachToVhciDriver(SOCKET importSocket, ULONG speed) {
      HANDLE hVhci = OpenVhciDeviceHandle();

      if (hVhci == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        std::cerr << "[CLIENT ERROR] Cannot open VHCI handle (Error: " << error
                  << ")." << std::endl;
        std::cerr << "               Ensure you run as Administrator!"
                  << std::endl;
        closesocket(importSocket);
        return false;
      }

      vhci_ioctl_attach attachParams{};
      attachParams.port = 0; // Auto-select port
      attachParams.status = 0;
      attachParams.socket =
          importSocket; // Hand ownership of socket to kernel driver
      attachParams.speed = speed;
      attachParams.devid = 0x00010001;

      DWORD bytesReturned = 0;
      BOOL ioctlSuccess = DeviceIoControl(
          hVhci, IOCTL_USBIP_VHCI_ATTACH, &attachParams, sizeof(attachParams),
          &attachParams, sizeof(attachParams), &bytesReturned, NULL);

      CloseHandle(hVhci);

      if (ioctlSuccess) {
        std::cout << "[SUCCESS] Virtual device attached to VHCI driver!"
                  << std::endl;
        std::cout << "[INFO] Hardware mounted in Windows Device Manager."
                  << std::endl;
        return true;
      } else {
        std::cerr << "[CLIENT ERROR] DeviceIoControl ATTACH failed (Error: "
                  << GetLastError() << ")." << std::endl;
        closesocket(importSocket);
        return false;
      }
    }
  };

  int main(int argc, char *argv[]) {
    if (!IsRunningAsAdmin()) {
      std::cerr << "[WARNING] usbip_client requires Administrator privileges "
                   "to access VHCI."
                << std::endl;
    }

    if (argc > 1 && std::string(argv[1]) == "--detach") {
      if (argc > 2) {
        ULONG port = std::stoul(argv[2]);
        UsbIpClient::DetachVirtualDevice(port);
        return 0;
      } else {
        std::cerr << "Error: --detach requires a port number argument."
                  << std::endl;
        return 1;
      }
    }

    std::string serverIp = "127.0.0.1";
    std::string busId = "1-5";

    if (argc > 1)
      serverIp = argv[1];
    if (argc > 2)
      busId = argv[2];

    std::cout << "=== USB/IP Windows Client ===" << std::endl;
    std::cout << "Target Server IP : " << serverIp << std::endl;
    std::cout << "Target BUS ID    : " << busId << "\n" << std::endl;

    UsbIpClient client(serverIp);

    if (client.RequestDeviceList()) {
      client.ImportAndMountDevice(busId);
    }

    WSACleanup();
    return 0;
  }

  #endif
#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#pragma pack(push, 1)
struct USBIP_OP_COMMON {
  uint16_t version;
  uint16_t commandCode;
  uint32_t status;
};

struct USBIP_OP_REP_IMPORT {
  USBIP_OP_COMMON common;
  struct {
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
  } dev;
};
#pragma pack(pop)

int main() {
  WSADATA wsaData;
  WSAStartup(MAKEWORD(2, 2), &wsaData);

  SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(3240);
  inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

  std::cout << "[*] Connecting to server..." << std::endl;
  if (connect(sock, (sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
    std::cerr << "[-] Failed to connect" << std::endl;
    return 1;
  }
  std::cout << "[+] Connected to server" << std::endl;

  // Send OP_REQ_IMPORT
  USBIP_OP_COMMON req = {};
  req.version = htons(0x0111);
  req.commandCode = htons(0x8003);
  send(sock, (char *)&req, sizeof(req), 0);

  // Send busid
  char busid[32] = "1-5";
  send(sock, busid, sizeof(busid), 0);

  // Receive import reply
  USBIP_OP_REP_IMPORT rep = {};
  recv(sock, (char *)&rep, sizeof(rep), 0);

  std::cout << "[+] Device imported!" << std::endl;
  std::cout << "    VID: 0x" << std::hex << ntohs(rep.dev.idVendor)
            << std::endl;
  std::cout << "    PID: 0x" << std::hex << ntohs(rep.dev.idProduct)
            << std::endl;

  std::cout << "\n[+] VBoxUSB device should now be available!" << std::endl;
  std::cout << "Press Enter to disconnect..." << std::endl;
  std::cin.get();

  closesocket(sock);
  WSACleanup();
  return 0;
}