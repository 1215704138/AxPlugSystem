#pragma once
#include "../../../../include/driver/ITcpServer.h"
#include "../../../../include/driver/ITcpClient.h"
#include <boost/asio.hpp>
#include <memory>
#include <vector>
#include <atomic>
#include <mutex>
#include <thread>
#include <unordered_map>

// 前向声明
class BoostTcpClient;

class BoostTcpServer : public ITcpServer {
    AX_INTERFACE(BoostTcpServer)

public:
    BoostTcpServer();
    virtual ~BoostTcpServer();

    // 监听管理
    bool Listen(int port, int backlog = 5) override;
    bool StopListening() override;
    bool IsListening() const override;
    bool IsRunning() const override;

    // 连接管理
    ITcpClient* Accept() override;
    ITcpClient* GetClient(int index) override;
    bool DisconnectClient(ITcpClient* client) override;
    bool DisconnectAllClients() override;

    // 服务器信息
    const char* GetListenAddress() const override;
    int GetListenPort() const override;
    int GetMaxConnections() const override;
    void SetMaxConnections(int max) override;
    int GetConnectedCount() const override;

    // 配置
    void SetTimeout(int milliseconds) override;
    int GetTimeout() const override;
    void SetReuseAddress(bool enable) override;
    bool IsReuseAddressEnabled() const override;

    // 错误信息
    const char* GetLastError() const override;
    int GetErrorCode() const override;

protected:
    void Destroy() override;

private:
    // Boost.Asio核心组件
    std::unique_ptr<boost::asio::io_context> io_context_;
    std::unique_ptr<boost::asio::ip::tcp::acceptor> acceptor_;
    std::unique_ptr<boost::asio::ip::tcp::socket> accept_socket_;
    
    // 线程管理
    std::vector<std::thread> worker_threads_;
    std::atomic<bool> running_;
    std::atomic<bool> stopped_;
    
    // 客户端管理
    mutable std::mutex clients_mutex_;
    std::vector<std::unique_ptr<BoostTcpClient>> clients_;
    std::unordered_map<ITcpClient*, size_t> client_index_map_;
    int max_connections_;
    
    // 配置参数
    int timeout_ms_;
    bool reuse_address_enabled_;
    int listen_port_;
    std::string listen_address_;
    
    // 错误信息
    mutable std::mutex error_mutex_;
    std::string last_error_;
    int error_code_;
    
    // 性能优化参数
    static constexpr size_t THREAD_POOL_SIZE = 4;
    static constexpr int DEFAULT_BACKLOG = 1024;

    // 内部方法
    void UpdateError(const boost::system::error_code& ec);
    void StartAccept();
    void HandleAccept(const boost::system::error_code& ec);
    void StartWorkerThreads();
    void StopWorkerThreads();
    void RemoveClient(ITcpClient* client);
    void OptimizeAcceptor();
    void SetAcceptorOptions();
    size_t GetNextClientIndex();
    
    // 高性能连接管理
    void PreAllocateClients();
    void CleanupDisconnectedClients();
    
    // 线程池管理
    void WorkerThread(size_t thread_id);
};
