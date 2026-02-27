#pragma once
#include "../../../../include/driver/ITcpClient.h"
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <memory>
#include <array>
#include <atomic>
#include <mutex>
#include <string>

class BoostTcpClient : public ITcpClient {
public:
    BoostTcpClient();
    virtual ~BoostTcpClient();

    // Accept an externally provided socket (used by BoostTcpServer::HandleAccept)
    bool AttachSocket(boost::asio::ip::tcp::socket&& socket);

    // 连接管理
    bool Connect(const char* host, int port) override;
    bool Disconnect() override;
    bool IsConnected() const override;
    bool IsConnecting() const override;

    // 数据传输
    bool Send(const uint8_t* data, size_t len) override;
    bool SendString(const char* data) override;
    bool Receive(uint8_t* buffer, size_t maxLen, size_t& outLen) override;
    bool ReceiveString(char* buffer, size_t maxLen, size_t& outLen) override;

    // 连接信息
    const char* GetLocalAddress() const override;
    const char* GetRemoteAddress() const override;
    int GetLocalPort() const override;
    int GetRemotePort() const override;

    // 配置
    void SetTimeout(int milliseconds) override;
    int GetTimeout() const override;
    void SetBufferSize(int size) override;
    int GetBufferSize() const override;
    void SetKeepAlive(bool enable) override;
    bool IsKeepAliveEnabled() const override;

    // 错误信息
    const char* GetLastError() const override;
    int GetErrorCode() const override;

protected:
    void Destroy() override;

private:
    // Boost.Asio核心组件
    std::unique_ptr<boost::asio::io_context> io_context_;
    std::unique_ptr<boost::asio::ip::tcp::socket> socket_;
    std::unique_ptr<boost::asio::ip::tcp::resolver> resolver_;
    
    // 异步操作相关
    std::unique_ptr<boost::asio::deadline_timer> connect_timer_;
    std::unique_ptr<boost::asio::deadline_timer> receive_timer_;
    
    // 状态管理
    std::atomic<bool> connected_;
    std::atomic<bool> connecting_;
    std::atomic<bool> stopped_;
    
    // 缓冲区管理
    static constexpr size_t DEFAULT_BUFFER_SIZE = 8192;
    size_t buffer_size_;
    std::array<uint8_t, 65536> receive_buffer_;
    
    // 配置参数
    int timeout_ms_;
    bool keep_alive_enabled_;
    
    // 错误信息
    mutable std::mutex error_mutex_;
    std::string last_error_;
    int error_code_;
    
    // 地址信息缓存
    mutable std::mutex address_mutex_;
    std::string local_address_;
    std::string remote_address_;
    int local_port_;
    int remote_port_;

    // 内部方法
    void UpdateError(const boost::system::error_code& ec);
    void UpdateAddressInfo();
    void SetSocketOptions();
    void HandleConnect(const boost::system::error_code& ec);
    void HandleReceive(const boost::system::error_code& ec, size_t bytes_transferred);
    void HandleSend(const boost::system::error_code& ec, size_t bytes_transferred);
    void HandleTimeout(const boost::system::error_code& ec);
    void StartReceive();
    void StopInternal();
    void AttachUpdateState();
    
    // 高性能优化方法
    void OptimizeSocket();
    void EnableNoDelay();
    void EnableQuickAck();
    void SetSocketBufferSizes();
};
