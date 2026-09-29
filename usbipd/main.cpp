#include "usbipd.h"
#include <iostream>
#include <thread>

HANDLE g_hVBoxDriver = INVALID_HANDLE_VALUE;

int main() {
  std::cout << "========================================" << std::endl;
  std::cout << "  usbipd-cpp v1.0" << std::endl;
  std::cout << "========================================" << std::endl;

  g_hVBoxDriver = OpenVBoxUsbDriver();
  if (g_hVBoxDriver == INVALID_HANDLE_VALUE) {
    std::cerr << "[-] Failed to open VBoxUSBMon" << std::endl;
    return 1;
  }
  std::cout << "[+] VBoxUSBMon opened" << std::endl;


  //DebugListAllUsbDevices();

  //std::vector<USBIP_DEVICE_DESC> tes = ScanPhysicalUsbBus();




  WSADATA wsaData;
  if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
    std::cerr << "[-] Winsock init failed" << std::endl;
    return 1;
  }

  SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (listenSocket == INVALID_SOCKET) {
    std::cerr << "[-] Socket creation failed" << std::endl;
    return 1;
  }

  int optval = 1;
  setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, (char *)&optval,
             sizeof(optval));

  sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(USBIP_PORT);

  if (bind(listenSocket, (sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
    std::cerr << "[-] Bind failed. Error: " << WSAGetLastError() << std::endl;
    return 1;
  }

  listen(listenSocket, SOMAXCONN);
  std::cout << "[+] Server listening on port " << USBIP_PORT << std::endl;

  while (true) {
    SOCKET clientSocket = accept(listenSocket, nullptr, nullptr);
    if (clientSocket != INVALID_SOCKET) {
      std::cout << "[+] Client connected" << std::endl;
      std::thread(ConnectionWorkerThread, clientSocket).detach();
    }
  }

  closesocket(listenSocket);
  WSACleanup();
  return 0;
}