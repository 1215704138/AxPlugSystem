#pragma once
#include "driver/ITcpClient.h"
#include "AxPlug/AxPluginExport.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

class TcpClient : public ITcpClient {
public:
    TcpClient();
    ~TcpClient();

protected:
    void Destroy() override { delete this; }

private:
    SOCKET socket_;
    bool isConnected_;
    bool isConnecting_;
    int timeout_;
    int bufferSize_;
    bool keepAlive_;
    std::string localAddr_;
    std::string remoteAddr_;
    int localPort_;
    int remotePort_;
    mutable std::string lastError_;
    mutable int errorCode_;
    
    // 私有方法
    bool createSocket();
    bool setSocketOptions();
    void setError(const std::string& error, int code = 0) const;
    std::string getAddressFromSockaddr(const sockaddr_in& addr) const;
    int getPortFromSockaddr(const sockaddr_in& addr) const;
    
public:
    // 实现 ITcpClient 接口
    bool Connect(const char* host, int port) override;
    bool Disconnect() override;
    bool IsConnected() const override;
    bool IsConnecting() const override;
    
    bool Send(const uint8_t* data, size_t len) override;
    bool SendString(const char* data) override;
    bool Receive(uint8_t* buffer, size_t maxLen, size_t& outLen) override;
    bool ReceiveString(char* buffer, size_t maxLen, size_t& outLen) override;
    
    const char* GetLocalAddress() const override;
    const char* GetRemoteAddress() const override;
    int GetLocalPort() const override;
    int GetRemotePort() const override;
    
    void SetTimeout(int milliseconds) override;
    int GetTimeout() const override;
    void SetBufferSize(int size) override;
    int GetBufferSize() const override;
    void SetKeepAlive(bool enable) override;
    bool IsKeepAliveEnabled() const override;
    
    const char* GetLastError() const override;
    int GetErrorCode() const override;
};
