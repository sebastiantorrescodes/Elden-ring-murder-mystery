#ifndef _MM_TCP_CLIENT_H_
#define _MM_TCP_CLIENT_H_

#include <string>
#include <vector>
#include <cstdint>

// Plain WSASocket TCP client that connects out to the local Electron app's command
// listener, mirroring TcpTestClient.h/.cpp in SeamlessCoopExtension (connects lazily,
// retries on a cooldown, never blocks the caller's poll loop). Unlike TcpTestClient,
// this direction carries inbound commands rather than only outbound telemetry, so
// poll() drains the socket non-blockingly and hands back whatever complete
// newline-delimited lines have arrived since the last call.
class MMTcpClient {
public:
    MMTcpClient(const char* host, unsigned short port);
    ~MMTcpClient();

    MMTcpClient(const MMTcpClient&) = delete;
    MMTcpClient& operator=(const MMTcpClient&) = delete;

    // Safe to call every tick from the main poll loop: attempts a (cooldown-gated)
    // reconnect if not currently connected, then appends any complete lines received
    // since the last call to outLines.
    void poll(std::vector<std::string>& outLines);

private:
    bool tryConnect();
    void closeSocket();

    std::string host;
    unsigned short port;
    uintptr_t sock; // SOCKET, stored as uintptr_t so winsock2.h doesn't leak into this header
    bool connected;
    bool wsaOk;
    unsigned long long nextConnectAttemptMs;
    std::string recvBuffer;
};

#endif // _MM_TCP_CLIENT_H_
