#pragma once
// Fix 3.8: Unified Winsock initialization — replaces per-DLL duplicated WinsockInitializer classes.
// Windows internally reference-counts WSAStartup/WSACleanup, so per-DLL statics are safe.
// This header centralizes the definition to eliminate code duplication.

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

namespace AxPlug {

class WinsockInit {
public:
    WinsockInit() {
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
    }
    ~WinsockInit() { WSACleanup(); }
    WinsockInit(const WinsockInit&) = delete;
    WinsockInit& operator=(const WinsockInit&) = delete;
};

// Each DLL that includes this header gets its own static instance (one per DLL is correct).
inline WinsockInit& GetWinsockInit() {
    static WinsockInit instance;
    return instance;
}

} // namespace AxPlug

#endif
