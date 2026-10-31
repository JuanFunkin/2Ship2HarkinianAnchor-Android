#include "AnchorSocket.h"

#include <cerrno>
#include <cstring>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define ANCHOR_INVALID_SOCK INVALID_SOCKET
#define ANCHOR_CLOSE(s) closesocket(s)
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
typedef int sock_t;
#define ANCHOR_INVALID_SOCK (-1)
#define ANCHOR_CLOSE(s) close(s)
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace {
void EnsureNetworkInit() {
    static bool done = false;
    if (done) {
        return;
    }
    done = true;
#ifdef _WIN32
    WSADATA data;
    WSAStartup(MAKEWORD(2, 2), &data);
#else
    signal(SIGPIPE, SIG_IGN);
#endif
}

void SetNonBlocking(sock_t s, bool on) {
#ifdef _WIN32
    u_long mode = on ? 1 : 0;
    ioctlsocket(s, FIONBIO, &mode);
#else
    int flags = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, on ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK));
#endif
}

void SetRecvTimeout(sock_t s, int ms) {
#ifdef _WIN32
    DWORD t = (DWORD)ms;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&t, sizeof(t));
#else
    struct timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
}
} // namespace

AnchorSocket::~AnchorSocket() {
    Close();
}

bool AnchorSocket::Connect(const std::string& host, uint16_t port, int timeoutMs, std::string& error) {
    EnsureNetworkInit();
    Close();

    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* result = nullptr;
    const std::string portStr = std::to_string(port);
    int rc = getaddrinfo(host.c_str(), portStr.c_str(), &hints, &result);
    if (rc != 0 || result == nullptr) {
        error = "No se pudo resolver el servidor";
        return false;
    }

    sock_t connected = ANCHOR_INVALID_SOCK;
    for (struct addrinfo* ai = result; ai != nullptr && connected == ANCHOR_INVALID_SOCK; ai = ai->ai_next) {
        sock_t s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (s == ANCHOR_INVALID_SOCK) {
            continue;
        }

        SetNonBlocking(s, true);
        int cr = connect(s, ai->ai_addr, (int)ai->ai_addrlen);
        bool ok = (cr == 0);
        if (!ok) {
#ifdef _WIN32
            bool inProgress = (WSAGetLastError() == WSAEWOULDBLOCK);
#else
            bool inProgress = (errno == EINPROGRESS);
#endif
            if (inProgress) {
                fd_set wset;
                FD_ZERO(&wset);
                FD_SET(s, &wset);
                struct timeval tv;
                tv.tv_sec = timeoutMs / 1000;
                tv.tv_usec = (timeoutMs % 1000) * 1000;
                if (select((int)s + 1, nullptr, &wset, nullptr, &tv) > 0) {
                    int soErr = 0;
#ifdef _WIN32
                    int len = sizeof(soErr);
#else
                    socklen_t len = sizeof(soErr);
#endif
                    getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&soErr, &len);
                    ok = (soErr == 0);
                }
            }
        }

        if (ok) {
            SetNonBlocking(s, false);
            connected = s;
        } else {
            ANCHOR_CLOSE(s);
        }
    }
    freeaddrinfo(result);

    if (connected == ANCHOR_INVALID_SOCK) {
        error = "No se pudo conectar (servidor apagado o puerto bloqueado)";
        return false;
    }

    int one = 1;
    setsockopt(connected, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof(one));
#ifdef SO_NOSIGPIPE
    setsockopt(connected, SOL_SOCKET, SO_NOSIGPIPE, (const char*)&one, sizeof(one));
#endif
    SetRecvTimeout(connected, 500);
    mSock = (intptr_t)connected;
    return true;
}

bool AnchorSocket::SendAll(const char* data, size_t len) {
    if (mSock == -1) {
        return false;
    }
    size_t sent = 0;
    while (sent < len) {
        int n = (int)send((sock_t)mSock, data + sent, (int)(len - sent), MSG_NOSIGNAL);
        if (n <= 0) {
#ifndef _WIN32
            if (n < 0 && errno == EINTR) {
                continue;
            }
#endif
            return false;
        }
        sent += (size_t)n;
    }
    return true;
}

int AnchorSocket::Recv(char* buf, size_t len) {
    if (mSock == -1) {
        return -1;
    }
    int n = (int)recv((sock_t)mSock, buf, (int)len, 0);
    if (n > 0) {
        return n;
    }
    if (n == 0) {
        return -1;
    }
#ifdef _WIN32
    int err = WSAGetLastError();
    if (err == WSAETIMEDOUT || err == WSAEWOULDBLOCK) {
        return 0;
    }
#else
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
        return 0;
    }
#endif
    return -1;
}

void AnchorSocket::Shutdown() {
    if (mSock != -1) {
#ifdef _WIN32
        shutdown((sock_t)mSock, SD_BOTH);
#else
        shutdown((sock_t)mSock, SHUT_RDWR);
#endif
    }
}

void AnchorSocket::Close() {
    if (mSock != -1) {
        ANCHOR_CLOSE((sock_t)mSock);
        mSock = -1;
    }
}
