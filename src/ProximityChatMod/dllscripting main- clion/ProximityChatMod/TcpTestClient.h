#ifndef _TCP_TEST_CLIENT_H_
#define _TCP_TEST_CLIENT_H_

#include <string>
#include <cstdint>

// Plain WSASocket TCP client for the quick-tcp-tester/tcp-test-listener.js dev harness.
// Kept deliberately dumb: connects lazily, drops lines while disconnected, and
// retries on a cooldown instead of blocking the caller's poll loop.
class TcpTestClient {
public:
    TcpTestClient(const char* host, unsigned short port);
    ~TcpTestClient();

    TcpTestClient(const TcpTestClient&) = delete;
    TcpTestClient& operator=(const TcpTestClient&) = delete;

    void sendLine(const std::string& line);

private:
    bool tryConnect();
    void closeSocket();

    std::string host;
    unsigned short port;
    uintptr_t sock; // SOCKET, stored as uintptr_t so winsock2.h doesn't leak into this header
    bool connected;
    bool wsaOk;
    unsigned long long nextConnectAttemptMs;
};

#endif // _TCP_TEST_CLIENT_H_
