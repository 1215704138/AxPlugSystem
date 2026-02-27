#include <iostream>
#include <windows.h>
#include "AxPlug/AxPlug.h"
#include "driver/ITcpServer.h"
#include "driver/ITcpClient.h"
#include "driver/IUdpSocket.h"

int main() {
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);

    std::cout << "=== Named Binding Test ===" << std::endl;
    AxPlug::Init();

    // Test 1: CreateTool<ITcpServer>() returns default (winsock) impl
    auto defaultServer = AxPlug::CreateTool<ITcpServer>();
    std::cout << "[1] CreateTool<ITcpServer>(): " << (defaultServer ? "OK" : "FAIL") << std::endl;

    // Test 2: CreateTool<ITcpServer>("boost") returns boost impl
    auto boostServer = AxPlug::CreateTool<ITcpServer>("boost");
    std::cout << "[2] CreateTool<ITcpServer>(\"boost\"): " << (boostServer ? "OK" : "FAIL") << std::endl;

    // Test 3: They should be different objects
    std::cout << "[3] Different objects: " << (defaultServer.get() != boostServer.get() ? "OK" : "FAIL") << std::endl;

    // Test 4: CreateTool<ITcpClient>() returns default
    auto defaultClient = AxPlug::CreateTool<ITcpClient>();
    std::cout << "[4] CreateTool<ITcpClient>(): " << (defaultClient ? "OK" : "FAIL") << std::endl;

    // Test 5: CreateTool<ITcpClient>("boost") returns boost impl
    auto boostClient = AxPlug::CreateTool<ITcpClient>("boost");
    std::cout << "[5] CreateTool<ITcpClient>(\"boost\"): " << (boostClient ? "OK" : "FAIL") << std::endl;

    // Test 6: CreateTool<IUdpSocket>() returns default
    auto defaultUdp = AxPlug::CreateTool<IUdpSocket>();
    std::cout << "[6] CreateTool<IUdpSocket>(): " << (defaultUdp ? "OK" : "FAIL") << std::endl;

    // Test 7: CreateTool<IUdpSocket>("boost") returns boost impl
    auto boostUdp = AxPlug::CreateTool<IUdpSocket>("boost");
    std::cout << "[7] CreateTool<IUdpSocket>(\"boost\"): " << (boostUdp ? "OK" : "FAIL") << std::endl;

    // Test 8: Invalid name returns nullptr
    auto invalid = AxPlug::CreateTool<ITcpServer>("nonexistent");
    std::cout << "[8] CreateTool<ITcpServer>(\"nonexistent\"): " << (invalid == nullptr ? "OK (null as expected)" : "FAIL") << std::endl;

    // Test 9: Boost server actually works (quick listen test)
    if (boostServer) {
        boostServer->SetMaxConnections(5);
        bool ok = boostServer->Listen(19999);
        std::cout << "[9] Boost server Listen(19999): " << (ok ? "OK" : "FAIL") << std::endl;
        if (ok) boostServer->StopListening();
    }

    // Test 10: Default server also works
    if (defaultServer) {
        defaultServer->SetMaxConnections(5);
        bool ok = defaultServer->Listen(19998);
        std::cout << "[10] Default server Listen(19998): " << (ok ? "OK" : "FAIL") << std::endl;
        if (ok) defaultServer->StopListening();
    }

    std::cout << "\n=== Named Binding Test Complete ===" << std::endl;
    return 0;
}
