#pragma once
#include "../AxPlug/IAxObject.h"
#include <stdint.h>

// TCP客户端接口
class ITcpClient : public IAxObject {
    AX_INTERFACE(ITcpClient)
public:
    // 连接管理
    virtual bool Connect(const char* host, int port) = 0;
    virtual bool Disconnect() = 0;
    virtual bool IsConnected() const = 0;
    virtual bool IsConnecting() const = 0;
    
    // 数据传输
    virtual bool Send(const uint8_t* data, size_t len) = 0;
    virtual bool SendString(const char* data) = 0;
    virtual bool Receive(uint8_t* buffer, size_t maxLen, size_t& outLen) = 0;
    virtual bool ReceiveString(char* buffer, size_t maxLen, size_t& outLen) = 0;
    
    // 连接信息
    virtual const char* GetLocalAddress() const = 0;
    virtual const char* GetRemoteAddress() const = 0;
    virtual int GetLocalPort() const = 0;
    virtual int GetRemotePort() const = 0;
    
    // 配置
    virtual void SetTimeout(int milliseconds) = 0;
    virtual int GetTimeout() const = 0;
    virtual void SetBufferSize(int size) = 0;
    virtual int GetBufferSize() const = 0;
    virtual void SetKeepAlive(bool enable) = 0;
    virtual bool IsKeepAliveEnabled() const = 0;
    
    // 错误信息
    virtual const char* GetLastError() const = 0;
    virtual int GetErrorCode() const = 0;

};
