#include "TcpTestClient.h"
#include "pch.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>

namespace {
    constexpr unsigned long long kReconnectCooldownMs = 3000;
}

TcpTestClient::TcpTestClient(const char* host, unsigned short port)
    : host(host), port(port), sock((uintptr_t)INVALID_SOCKET),
      connected(false), wsaOk(false), nextConnectAttemptMs(0) {
    WSADATA wsaData;
    wsaOk = (WSAStartup(MAKEWORD(2, 2), &wsaData) == 0);
    if (!wsaOk) {
        std::cout << "[tcp] WSAStartup failed" << std::endl;
    }
}

TcpTestClient::~TcpTestClient() {
    closeSocket();
    if (wsaOk) {
        WSACleanup();
    }
}

void TcpTestClient::closeSocket() {
    if ((SOCKET)sock != INVALID_SOCKET) {
        closesocket((SOCKET)sock);
        sock = (uintptr_t)INVALID_SOCKET;
    }
    connected = false;
}

bool TcpTestClient::tryConnect() {
    ULONGLONG now = GetTickCount64();
    if (now < nextConnectAttemptMs) {
        return false;
    }
    // Listener is probably just not up yet (dev harness) - don't hammer connect() every poll cycle.
    nextConnectAttemptMs = now + kReconnectCooldownMs;

    if (!wsaOk) {
        return false;
    }

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        std::cout << "[tcp] socket() failed: " << WSAGetLastError() << std::endl;
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (InetPtonA(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        std::cout << "[tcp] bad host address: " << host << std::endl;
        closesocket(s);
        return false;
    }

    if (connect(s, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(s);
        return false;
    }

    sock = (uintptr_t)s;
    connected = true;
    std::cout << "[tcp] connected to " << host << ":" << port << std::endl;
    return true;
}

void TcpTestClient::sendLine(const std::string& line) {
    if (!connected && !tryConnect()) {
        return;
    }

    std::string payload = line + "\n";
    int sent = send((SOCKET)sock, payload.c_str(), (int)payload.size(), 0);
    if (sent == SOCKET_ERROR) {
        std::cout << "[tcp] send failed (" << WSAGetLastError() << "), will reconnect" << std::endl;
        closeSocket();
    }
}
