#pragma once
#include "../AxPlug/IAxObject.h"
#include "ITcpClient.h"
#include <stdint.h>

// TCP服务器接口
class ITcpServer : public IAxObject {
    AX_INTERFACE(ITcpServer)
public:
    // 监听管理
    virtual bool Listen(int port, int backlog = 5) = 0;
    virtual bool StopListening() = 0;
    virtual bool IsListening() const = 0;
    virtual bool IsRunning() const = 0;
    
    // 连接管理
    virtual ITcpClient* Accept() = 0;  // 返回新的客户端连接
    virtual ITcpClient* GetClient(int index) = 0;  // 通过索引获取客户端
    virtual bool DisconnectClient(ITcpClient* client) = 0;
    virtual bool DisconnectAllClients() = 0;
    
    // 服务器信息
    virtual const char* GetListenAddress() const = 0;
    virtual int GetListenPort() const = 0;
    virtual int GetMaxConnections() const = 0;
    virtual void SetMaxConnections(int max) = 0;
    virtual int GetConnectedCount() const = 0;
    
    // 配置
    virtual void SetTimeout(int milliseconds) = 0;
    virtual int GetTimeout() const = 0;
    virtual void SetReuseAddress(bool enable) = 0;
    virtual bool IsReuseAddressEnabled() const = 0;
    
    // 错误信息
    virtual const char* GetLastError() const = 0;
    virtual int GetErrorCode() const = 0;

};
