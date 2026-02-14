#pragma once
#include "../AxPlug/IAxObject.h"
#include <vector>
#include <string>

// UDP套接字接口
class IUdpSocket : public IAxObject {
    AX_INTERFACE(IUdpSocket)
public:
    // 绑定管理
    virtual bool Bind(int port) = 0;
    virtual bool Unbind() = 0;
    virtual bool IsBound() const = 0;
    
    // 数据传输
    virtual bool Send(const uint8_t* data, size_t len) = 0;
    virtual bool SendString(const char* data) = 0;
    virtual bool SendTo(const char* host, int port, const uint8_t* data, size_t len) = 0;
    virtual bool SendStringTo(const char* host, int port, const char* data) = 0;
    
    // 数据接收
    virtual bool Receive(uint8_t* buffer, size_t maxLen, size_t& outLen) = 0;
    virtual bool ReceiveString(char* buffer, size_t maxLen, size_t& outLen) = 0;
    virtual bool ReceiveFrom(char* host, int hostLen, int& port, uint8_t* buffer, size_t maxLen, size_t& outLen) = 0;
    virtual bool ReceiveStringFrom(char* host, int hostLen, int& port, char* buffer, size_t maxLen, size_t& outLen) = 0;
    
    // 广播
    virtual bool EnableBroadcast(bool enable) = 0;
    virtual bool IsBroadcastEnabled() const = 0;
    
    // 多播
    virtual bool JoinMulticast(const char* group) = 0;
    virtual bool LeaveMulticast(const char* group) = 0;
    virtual bool GetMulticastGroups(char* groups, int maxLen, int& count) const = 0;
    
    // 地址信息
    virtual const char* GetLocalAddress() const = 0;
    virtual int GetLocalPort() const = 0;
    
    // 配置
    virtual void SetTimeout(int milliseconds) = 0;
    virtual int GetTimeout() const = 0;
    virtual void SetBufferSize(int size) = 0;
    virtual int GetBufferSize() const = 0;
    virtual void SetTTL(int ttl) = 0;
    virtual int GetTTL() const = 0;
    
    // 错误信息
    virtual const char* GetLastError() const = 0;
    virtual int GetErrorCode() const = 0;
    
};
