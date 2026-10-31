#pragma once
#ifdef __cplusplus

#include <cstddef>
#include <cstdint>
#include <string>

// Socket TCP mínimo y portable (Windows / Linux / macOS / Android) sin dependencias externas.
class AnchorSocket {
  public:
    AnchorSocket() = default;
    ~AnchorSocket();
    AnchorSocket(const AnchorSocket&) = delete;
    AnchorSocket& operator=(const AnchorSocket&) = delete;

    bool Connect(const std::string& host, uint16_t port, int timeoutMs, std::string& error);
    bool SendAll(const char* data, size_t len);
    // >0: bytes leídos, 0: timeout sin datos, -1: conexión cerrada o error
    int Recv(char* buf, size_t len);
    void Shutdown(); // desbloquea Recv desde otro hilo
    void Close();
    bool IsOpen() const {
        return mSock != -1;
    }

  private:
    intptr_t mSock = -1;
};

#endif
