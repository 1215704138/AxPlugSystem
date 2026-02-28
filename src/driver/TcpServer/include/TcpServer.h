#pragma once
#include "driver/ITcpServer.h"
#include "driver/ITcpClient.h"
#include "AxPlug/AxPluginExport.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <memory>

#pragma comment(lib, "ws2_32.lib")

// 前向声明
class TcpServer;

// TCP客户端实现（用于服务器返回的连接）
class TcpClientConnection : public ITcpClient {
    friend class TcpServer;
public:
    TcpClientConnection(SOCKET socket, TcpServer* server);
    ~TcpClientConnection();

protected:
    void Destroy() override { delete this; }

private:
    SOCKET socket_;
    std::mutex socketMutex_;  // Fix 7/C: protect socket_ against concurrent Disconnect/Send/Receive
    std::atomic<bool> isConnected_;  // Fix 2.4: atomic for thread safety
    int timeout_;
    int bufferSize_;
    std::string localAddr_;
    std::string remoteAddr_;
    int localPort_;
    int remotePort_;
    mutable std::mutex errorMutex_;  // Fix 2.9: protect lastError_/errorCode_
    mutable std::string lastError_;
    mutable int errorCode_;
    
    // 回调机制：指向所属服务器的指针
    class TcpServer* server_;
    
    // 私有方法
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
    
    // 内部方法
    SOCKET GetSocket() const { return socket_; }
};

// TCP服务器实现
class TcpServer : public ITcpServer {
private:
    SOCKET listenSocket_;
    mutable std::mutex listenMutex_;  // Fix D: protect listenSocket_ against concurrent Accept/StopListening
    std::atomic<bool> isListening_;   // Fix 2.4: atomic for thread safety
    std::atomic<bool> isRunning_;    // Fix 2.4: atomic for thread safety
    int maxConnections_;
    int timeout_;
    bool reuseAddress_;
    std::string listenAddr_;
    int listenPort_;
    std::vector<std::shared_ptr<TcpClientConnection>> clients_;  // Fix: Use shared_ptr to prevent dangling pointers
    mutable std::mutex errorMutex_;  // Fix 2.9: protect lastError_/errorCode_
    mutable std::string lastError_;
    mutable int errorCode_;
    
    // 线程安全保护
    mutable std::mutex clientsMutex_;
    
    // 析构标志
    std::atomic<bool> isDestroying_;  // Fix 2.4: atomic for thread safety
    
    // 私有方法
    bool createSocket();
    bool setSocketOptions();
    void setError(const std::string& error, int code = 0) const;
    void removeClient(TcpClientConnection* client);
    
    // 友元类访问权限
    friend class TcpClientConnection;
    void OnClientDisconnected(TcpClientConnection* client);
    void CleanupInvalidClients();
    
public:
    TcpServer();
    ~TcpServer();

protected:
    void Destroy() override { delete this; }
    
    // 实现 ITcpServer 接口
    bool Listen(int port, int backlog = 5) override;
    bool StopListening() override;
    bool IsListening() const override;
    bool IsRunning() const override;
    
    std::shared_ptr<ITcpClient> Accept() override;
    std::shared_ptr<ITcpClient> GetClient(int index) override;
    bool DisconnectClient(const std::shared_ptr<ITcpClient>& client) override;
    bool DisconnectAllClients() override;
    
    const char* GetListenAddress() const override;
    int GetListenPort() const override;
    int GetMaxConnections() const override;
    void SetMaxConnections(int max) override;
    int GetConnectedCount() const override;
    
    void SetTimeout(int milliseconds) override;
    int GetTimeout() const override;
    void SetReuseAddress(bool enable) override;
    bool IsReuseAddressEnabled() const override;
    
    const char* GetLastError() const override;
    int GetErrorCode() const override;
};
