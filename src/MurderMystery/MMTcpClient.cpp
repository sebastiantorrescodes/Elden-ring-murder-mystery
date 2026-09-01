#include "pch.h"
#include "MMTcpClient.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>

namespace {
    constexpr unsigned long long kReconnectCooldownMs = 3000;

    // Same reasoning as TcpTestClient.cpp's enableKeepAlive: Windows' default
    // SO_KEEPALIVE idle time (2 hours) is too slow to notice the local Electron app
    // vanishing without a clean close (crash, sleep/resume, force-quit).
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

// Defined in dllmain.cpp - reused here instead of duplicating a second file logger,
// same way TcpTestClient.cpp calls straight through to whatever the ambient output
// mechanism is (std::cout there, since that project has a console attached).
namespace MM {
    void Log(const std::string& text);
}

MMTcpClient::MMTcpClient(const char* host, unsigned short port)
    : host(host), port(port), sock((uintptr_t)INVALID_SOCKET),
      connected(false), wsaOk(false), nextConnectAttemptMs(0) {
    WSADATA wsaData;
    wsaOk = (WSAStartup(MAKEWORD(2, 2), &wsaData) == 0);
    if (!wsaOk) {
        MM::Log("[mm-tcp] WSAStartup failed");
    }
}

MMTcpClient::~MMTcpClient() {
    closeSocket();
    if (wsaOk) {
        WSACleanup();
    }
}

void MMTcpClient::closeSocket() {
    if ((SOCKET)sock != INVALID_SOCKET) {
        closesocket((SOCKET)sock);
        sock = (uintptr_t)INVALID_SOCKET;
    }
    connected = false;
    recvBuffer.clear();
}

bool MMTcpClient::tryConnect() {
    ULONGLONG now = GetTickCount64();
    if (now < nextConnectAttemptMs) {
        return false;
    }
    // Electron's command listener is probably just not up yet - don't hammer connect() every poll cycle.
    nextConnectAttemptMs = now + kReconnectCooldownMs;

    if (!wsaOk) {
        return false;
    }

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        MM::Log("[mm-tcp] socket() failed: " + std::to_string(WSAGetLastError()));
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (InetPtonA(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        MM::Log("[mm-tcp] bad host address: " + host);
        closesocket(s);
        return false;
    }

    if (connect(s, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        MM::Log("[mm-tcp] connect() failed: " + std::to_string(WSAGetLastError()) +
                ", will retry in " + std::to_string(kReconnectCooldownMs) + "ms");
        closesocket(s);
        return false;
    }

    // Reads must never block the game's main loop (writes stay blocking elsewhere in
    // this codebase since small local sends never fill the buffer in practice, but
    // there's no equivalent "just don't call recv if you don't want to wait" option).
    u_long nonBlocking = 1;
    ioctlsocket(s, FIONBIO, &nonBlocking);

    enableKeepAlive(s);

    sock = (uintptr_t)s;
    connected = true;
    recvBuffer.clear();
    MM::Log("[mm-tcp] connected to " + host + ":" + std::to_string(port));
    return true;
}

// gives the dll a path back to electron
bool MMTcpClient::sendLine(const std::string& line) {
    if (!connected && !tryConnect()) {
        return false;
    }

    const std::string payload = line + "\n";
    const int sent = send(
        (SOCKET)sock,
        payload.data(),
        static_cast<int>(payload.size()),
        0);

    if (sent != static_cast<int>(payload.size())) {
        const int err = WSAGetLastError();

        MM::Log(
            "[mm-tcp] send failed (" +
            std::to_string(err) +
            "), will reconnect");

        closeSocket();
        return false;
    }

    return true;
}

void MMTcpClient::poll(std::vector<std::string>& outLines) {
    if (!connected && !tryConnect()) {
        return;
    }

    char chunk[512];
    for (;;) {
        int received = recv((SOCKET)sock, chunk, sizeof(chunk), 0);

        if (received > 0) {
            recvBuffer.append(chunk, received);
            continue;
        }

        if (received == 0) {
            // Peer closed the connection cleanly.
            MM::Log("[mm-tcp] disconnected (peer closed)");
            closeSocket();
            return;
        }

        const int err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK) {
            MM::Log("[mm-tcp] recv failed (" + std::to_string(err) + "), will reconnect");
            closeSocket();
        }
        break; // no more data available right now
    }

    // Split on newlines in case multiple commands arrived in one chunk, or one
    // command arrived split across two chunks (both happen with TCP) - same
    // buffering approach as main.ts's own DLL-reading side of this protocol.
    size_t newlinePos;
    while ((newlinePos = recvBuffer.find('\n')) != std::string::npos) {
        std::string line = recvBuffer.substr(0, newlinePos);
        recvBuffer.erase(0, newlinePos + 1);

        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (!line.empty()) {
            outLines.push_back(line);
        }
    }
}
