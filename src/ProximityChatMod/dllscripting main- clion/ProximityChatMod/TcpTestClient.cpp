#include "TcpTestClient.h"
#include "pch.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#include <iostream>

namespace {
    constexpr unsigned long long kReconnectCooldownMs = 3000;

    // Windows' default SO_KEEPALIVE idle time is 2 HOURS before the first probe -
    // effectively useless for noticing a dead peer (Electron app closed uncleanly,
    // machine slept/resumed, etc.) any faster than the next failed send() would
    // anyway. Overriding via WSAIoctl so a genuinely dead connection gets detected
    // and reconnected within a few seconds instead of sitting "connected" but silently
    // dropping data for a long stretch.
    void enableKeepAlive(SOCKET s) {
        BOOL enable = TRUE;
        setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, (const char*)&enable, sizeof(enable));

        tcp_keepalive settings{};
        settings.onoff = 1;
        settings.keepalivetime = 5000;     // ms idle before the first probe
        settings.keepaliveinterval = 1000; // ms between probes
        DWORD bytesReturned = 0;
        WSAIoctl(s, SIO_KEEPALIVE_VALS, &settings, sizeof(settings), nullptr, 0, &bytesReturned, nullptr, nullptr);
    }
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
        // Previously silent - with no log line here, a run of failed reconnects was
        // indistinguishable from the thing just working, from the DLL console alone.
        std::cout << "[tcp] connect() failed: " << WSAGetLastError()
                   << ", will retry in " << kReconnectCooldownMs << "ms" << std::endl;
        closesocket(s);
        return false;
    }

    enableKeepAlive(s);

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
