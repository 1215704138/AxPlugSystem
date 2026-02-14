#pragma once
#include "driver/IUdpSocket.h"
#include "AxPlug/AxPluginExport.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

class UdpSocket : public IUdpSocket {
public:
    UdpSocket();
    ~UdpSocket();

protected:
    void Destroy() override { delete this; }

private:
    SOCKET socket_;
    bool isBound_;
    int timeout_;
    int bufferSize_;
    int ttl_;
    bool broadcastEnabled_;
    std::string localAddr_;
    int localPort_;
    std::vector<std::string> multicastGroups_;
    mutable std::string lastError_;
    mutable int errorCode_;
    
    // 私有方法
    bool createSocket();
    bool setSocketOptions();
    void setError(const std::string& error, int code = 0) const;
    std::string getAddressFromSockaddr(const sockaddr_in& addr) const;
    int getPortFromSockaddr(const sockaddr_in& addr) const;
    
public:
    // 实现 IUdpSocket 接口
    bool Bind(int port) override;
    bool Unbind() override;
    bool IsBound() const override;
    
    bool Send(const uint8_t* data, size_t len) override;
    bool SendString(const char* data) override;
    bool SendTo(const char* host, int port, const uint8_t* data, size_t len) override;
    bool SendStringTo(const char* host, int port, const char* data) override;
    
    bool Receive(uint8_t* buffer, size_t maxLen, size_t& outLen) override;
    bool ReceiveString(char* buffer, size_t maxLen, size_t& outLen) override;
    bool ReceiveFrom(char* host, int hostLen, int& port, uint8_t* buffer, size_t maxLen, size_t& outLen) override;
    bool ReceiveStringFrom(char* host, int hostLen, int& port, char* buffer, size_t maxLen, size_t& outLen) override;
    
    bool EnableBroadcast(bool enable) override;
    bool IsBroadcastEnabled() const override;
    
    bool JoinMulticast(const char* group) override;
    bool LeaveMulticast(const char* group) override;
    bool GetMulticastGroups(char* groups, int maxLen, int& count) const override;
    
    const char* GetLocalAddress() const override;
    int GetLocalPort() const override;
    
    void SetTimeout(int milliseconds) override;
    int GetTimeout() const override;
    void SetBufferSize(int size) override;
    int GetBufferSize() const override;
    void SetTTL(int ttl) override;
    int GetTTL() const override;
    
    const char* GetLastError() const override;
    int GetErrorCode() const override;
};
