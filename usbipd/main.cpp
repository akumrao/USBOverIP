#include "usbipd.h"
#include <iostream>
#include <thread>

int main() {
  std::cout
      << "====================================================================="
      << std::endl;
  std::cout << "     PRODUCTION-GRADE MULTI-THREADED USBIPD SERVER RUNTIME "
               "ENGINE     "
            << std::endl;
  std::cout
      << "====================================================================="
      << std::endl;

  WSADATA wsaData;
  if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
    std::cerr << "[-] Critical Error: Winsock initialization failure."
              << std::endl;
    return 1;
  }

  struct addrinfo networkHints, *addressResolveResult = nullptr;
  ZeroMemory(&networkHints, sizeof(networkHints));
  networkHints.ai_family = AF_INET;
  networkHints.ai_socktype = SOCK_STREAM;
  networkHints.ai_protocol = IPPROTO_TCP;
  networkHints.ai_flags = AI_PASSIVE;

  getaddrinfo(nullptr, USBIP_PORT, &networkHints, &addressResolveResult);
  SOCKET masterListenSocket =
      socket(addressResolveResult->ai_family, addressResolveResult->ai_socktype,
             addressResolveResult->ai_protocol);

  if (masterListenSocket == INVALID_SOCKET) {
    std::cerr << "[-] Error assigning socket descriptor." << std::endl;
    WSACleanup();
    return 1;
  }

  char optval = 1;
  setsockopt(masterListenSocket, SOL_SOCKET, SO_REUSEADDR, &optval,
             sizeof(optval));

  if (bind(masterListenSocket, addressResolveResult->ai_addr,
           (int)addressResolveResult->ai_addrlen) == SOCKET_ERROR) {
    std::cerr
        << "[-] Bind conflict on Port 3240. Ensure you run as Administrator."
        << std::endl;
    closesocket(masterListenSocket);
    freeaddrinfo(addressResolveResult);
    WSACleanup();
    return 1;
  }

  freeaddrinfo(addressResolveResult);
  listen(masterListenSocket, SOMAXCONN);

  std::cout << "[+] Server bound to Port " << USBIP_PORT << std::endl;
  std::cout << "[+] Awaiting connection handshakes...\n" << std::endl;

  while (true) {
    SOCKET connectionAcceptHandle =
        accept(masterListenSocket, nullptr, nullptr);
    if (connectionAcceptHandle != INVALID_SOCKET) {
      std::cout << "\n[+] Connection accepted! Dispatching worker thread..."
                << std::endl;
      std::thread processingWorker(ConnectionWorkerThread,
                                   connectionAcceptHandle);
      processingWorker.detach();
    }
  }

  closesocket(masterListenSocket);
  WSACleanup();
  return 0;
}