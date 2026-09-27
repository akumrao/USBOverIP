#define WIN32_LEAN_AND_MEAN
#include 
#include 
#include 
#include 
#include 
#include 
#include 
#include 
#include 
#include 

#pragma comment(lib, "ws2_32.lib")

#define USBIP_PORT "3240"
#define USBIP_VERSION 0x0111

// Symbolic link or device interface path for the usbip-win2 UDE kernel driver
#define USBIP_DRIVER_SYMBOLIC_LINK L"\\\\.\\usbip_ude"

// Custom IOCTL codes matching usbip-win2 driver communication interface
#define IOCTL_USBIP_ATTACHCTL CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_USBIP_DETACHCTL CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_USBIP_PORTLIST  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)

#pragma pack(push, 1)
struct usbip_header {
    uint16_t version;
    uint16_t command;
    uint32_t status;
};

struct op_req_devlist {
    usbip_header header;
};

struct op_dev_export {
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

struct op_rep_devlist {
    usbip_header header;
    uint32_t num_devices;
};

struct op_req_import {
    usbip_header header;
    char busid[32];
};

struct op_rep_import {
    usbip_header header;
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

// Driver communication structures
struct USBIP_ATTACH_REQUEST {
    char Host[256];
    char BusId[32];
    uint32_t Port;
};

struct USBIP_DETACH_REQUEST {
    uint32_t Port;
};
#pragma pack(pop)

SOCKET ConnectToServer(const std::string& host) {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "Error: WSAStartup failed." << std::endl;
        return INVALID_SOCKET;
    }

    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    if (getaddrinfo(host.c_str(), USBIP_PORT, &hints, &res) != 0) {
        std::cerr << "Error: Failed to resolve hostname/IP: " << host << std::endl;
        WSACleanup();
        return INVALID_SOCKET;
    }

    SOCKET sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock == INVALID_SOCKET) {
        std::cerr << "Error: Failed to create socket." << std::endl;
        freeaddrinfo(res);
        WSACleanup();
        return INVALID_SOCKET;
    }

    if (connect(sock, res->ai_addr, (int)res->ai_addrlen) == SOCKET_ERROR) {
        std::cerr << "Error: Failed to connect to server " << host << ":" << USBIP_PORT << std::endl;
        closesocket(sock);
        freeaddrinfo(res);
        WSACleanup();
        return INVALID_SOCKET;
    }

    freeaddrinfo(res);
    return sock;
}

void ExecuteRemoteList(const std::string& host) {
    SOCKET sock = ConnectToServer(host);
    if (sock == INVALID_SOCKET) return;

    op_req_devlist req{};
    req.header.version = htons(USBIP_VERSION);
    req.header.command = htons(0x8005);
    req.header.status = 0;

    if (send(sock, (char*)&req, sizeof(req), 0) == SOCKET_ERROR) {
        std::cerr << "Error: Failed to send device list request." << std::endl;
        closesocket(sock);
        WSACleanup();
        return;
    }

    op_rep_devlist rep{};
    if (recv(sock, (char*)&rep, sizeof(rep), MSG_WAITALL) <= 0) {
        std::cerr << "Error: Failed to receive device list response." << std::endl;
        closesocket(sock);
        WSACleanup();
        return;
    }

    uint32_t numDevices = ntohl(rep.num_devices);
    std::cout << "Exportable USB Devices on " << host << ":" << std::endl;
    std::cout << std::left << std::setw(12) << "BUSID" << std::setw(12) << "VID:PID" << "DEVICE PATH" << std::endl;

    for (uint32_t i = 0; i < numDevices; ++i) {
        op_dev_export dev{};
        if (recv(sock, (char*)&dev, sizeof(dev), MSG_WAITALL) <= 0) break;

        char vidPidBuf[16];
        snprintf(vidPidBuf, sizeof(vidPidBuf), "%04X:%04X", ntohs(dev.idVendor), ntohs(dev.idProduct));

        std::cout << std::left << std::setw(12) << dev.busid 
                  << std::setw(12) << vidPidBuf 
                  << dev.path << std::endl;

        for (uint8_t intf = 0; intf < dev.bNumInterfaces; ++intf) {
            uint32_t intfData[2];
            recv(sock, (char*)&intfData, sizeof(intfData), MSG_WAITALL);
        }
    }

    closesocket(sock);
    WSACleanup();
}

void ExecuteAttach(const std::string& host, const std::string& busId) {
    HANDLE hDriver = CreateFileW(
        USBIP_DRIVER_SYMBOLIC_LINK,
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (hDriver == INVALID_HANDLE_VALUE) {
        std::cerr << "Error: Failed to open kernel driver handle. Ensure usbip-win2 drivers are installed and running." << std::endl;
        return;
    }

    USBIP_ATTACH_REQUEST attachReq{};
    strcpy_s(attachReq.Host, host.c_str());
    strcpy_s(attachReq.BusId, busId.c_str());
    attachReq.Port = 0;

    DWORD bytesReturned = 0;
    BOOL success = DeviceIoControl(
        hDriver,
        IOCTL_USBIP_ATTACHCTL,
        &attachReq,
        sizeof(attachReq),
        &attachReq,
        sizeof(attachReq),
        &bytesReturned,
        nullptr
    );

    if (!success) {
        std::cerr << "Error: IOCTL attach request failed with code " << GetLastError() << std::endl;
    } else {
        std::cout << "info: Successfully attached remote device " << busId << " from " << host 
                  << " to virtual port " << attachReq.Port << std::endl;
    }

    CloseHandle(hDriver);
}

void ExecuteDetach(uint32_t portNum) {
    HANDLE hDriver = CreateFileW(
        USBIP_DRIVER_SYMBOLIC_LINK,
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (hDriver == INVALID_HANDLE_VALUE) {
        std::cerr << "Error: Failed to open kernel driver handle." << std::endl;
        return;
    }

    USBIP_DETACH_REQUEST detachReq{};
    detachReq.Port = portNum;

    DWORD bytesReturned = 0;
    BOOL success = DeviceIoControl(
        hDriver,
        IOCTL_USBIP_DETACHCTL,
        &detachReq,
        sizeof(detachReq),
        nullptr,
        0,
        &bytesReturned,
        nullptr
    );

    if (!success) {
        std::cerr << "Error: IOCTL detach request failed for port " << portNum << " (Error: " << GetLastError() << ")" << std::endl;
    } else {
        std::cout << "info: Successfully detached virtual port " << portNum << std::endl;
    }

    CloseHandle(hDriver);
}

void PrintUsage() {
    std::cout << "USBIP Command-Line Client (Full Driver Integration)\n\n"
              << "Usage:\n"
              << "  usbip_client.exe list -r \n"
              << "  usbip_client.exe attach -r  -b \n"
              << "  usbip_client.exe detach -p \n"
              << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        PrintUsage();
        return 1;
    }

    std::string command = argv[1];

    if (command == "list" || command == "-l") {
        std::string serverIP;
        for (int i = 2; i < argc; ++i) {
            if ((std::string(argv[i]) == "-r" || std::string(argv[i]) == "--remote") && i + 1 < argc) {
                serverIP = argv[i + 1];
            }
        }
        if (serverIP.empty()) {
            std::cerr << "Error: 'list' requires a remote server flag (-r )." << std::endl;
            return 1;
        }
        ExecuteRemoteList(serverIP);
    } 
    else if (command == "attach" || command == "-a") {
        std::string serverIP, busId;
        for (int i = 2; i < argc; ++i) {
            std::string arg = argv[i];
            if ((arg == "-r" || arg == "--remote") && i + 1 < argc) {
                serverIP = argv[i + 1];
            } else if ((arg == "-b" || arg == "--busid") && i + 1 < argc) {
                busId = argv[i + 1];
            }
        }
        if (serverIP.empty() || busId.empty()) {
            std::cerr << "Error: 'attach' requires both remote server (-r) and busid (-b)." << std::endl;
            return 1;
        }
        ExecuteAttach(serverIP, busId);
    } 
    else if (command == "detach" || command == "-d") {
        uint32_t portNum = 0;
        for (int i = 2; i < argc; ++i) {
            std::string arg = argv[i];
            if ((arg == "-p" || arg == "--port") && i + 1 < argc) {
                portNum = std::stoul(argv[i + 1]);
            }
        }
        if (portNum == 0) {
            std::cerr << "Error: 'detach' requires a port specification (-p )." << std::endl;
            return 1;
        }
        ExecuteDetach(portNum);
    } 
    else {
        PrintUsage();
        return 1;
    }

    return 0;
}