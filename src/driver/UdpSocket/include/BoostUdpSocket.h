#pragma once
#include "../../../../include/driver/IUdpSocket.h"
#include <boost/asio.hpp>
#include <memory>
#include <array>
#include <atomic>
#include <mutex>
#include <vector>
#include <string>
#include <unordered_set>

class BoostUdpSocket : public IUdpSocket {
    AX_INTERFACE(BoostUdpSocket)

public:
    BoostUdpSocket();
    virtual ~BoostUdpSocket();

    // 绑定管理
    bool Bind(int port) override;
    bool Unbind() override;
    bool IsBound() const override;

    // 数据传输
    bool Send(const uint8_t* data, size_t len) override;
    bool SendString(const char* data) override;
    bool SendTo(const char* host, int port, const uint8_t* data, size_t len) override;
    bool SendStringTo(const char* host, int port, const char* data) override;

    // 数据接收
    bool Receive(uint8_t* buffer, size_t maxLen, size_t& outLen) override;
    bool ReceiveString(char* buffer, size_t maxLen, size_t& outLen) override;
    bool ReceiveFrom(char* host, int hostLen, int& port, uint8_t* buffer, size_t maxLen, size_t& outLen) override;
    bool ReceiveStringFrom(char* host, int hostLen, int& port, char* buffer, size_t maxLen, size_t& outLen) override;

    // 广播
    bool EnableBroadcast(bool enable) override;
    bool IsBroadcastEnabled() const override;

    // 多播
    bool JoinMulticast(const char* group) override;
    bool LeaveMulticast(const char* group) override;
    bool GetMulticastGroups(char* groups, int maxLen, int& count) const override;

    // 地址信息
    const char* GetLocalAddress() const override;
    int GetLocalPort() const override;

    // 配置
    void SetTimeout(int milliseconds) override;
    int GetTimeout() const override;
    void SetBufferSize(int size) override;
    int GetBufferSize() const override;
    void SetTTL(int ttl) override;
    int GetTTL() const override;

    // 错误信息
    const char* GetLastError() const override;
    int GetErrorCode() const override;

protected:
    void Destroy() override;

private:
    // Boost.Asio核心组件
    std::unique_ptr<boost::asio::io_context> io_context_;
    std::unique_ptr<boost::asio::ip::udp::socket> socket_;
    std::unique_ptr<boost::asio::ip::udp::resolver> resolver_;
    
    // 异步操作相关
    std::unique_ptr<boost::asio::deadline_timer> receive_timer_;
    std::unique_ptr<boost::asio::deadline_timer> send_timer_;
    
    // 状态管理
    std::atomic<bool> bound_;
    std::atomic<bool> stopped_;
    
    // 缓冲区管理
    static constexpr size_t DEFAULT_BUFFER_SIZE = 65536;
    static constexpr size_t MAX_UDP_PACKET_SIZE = 65507; // UDP最大有效载荷
    size_t buffer_size_;
    std::array<uint8_t, MAX_UDP_PACKET_SIZE> receive_buffer_;
    
    // 配置参数
    int timeout_ms_;
    bool broadcast_enabled_;
    int ttl_;
    
    // 默认远程端点（用于Send方法）
    boost::asio::ip::udp::endpoint default_remote_endpoint_;
    bool has_default_remote_;
    
    // 多播组管理
    mutable std::mutex multicast_mutex_;
    std::unordered_set<std::string> multicast_groups_;
    
    // 错误信息
    mutable std::mutex error_mutex_;
    std::string last_error_;
    int error_code_;
    
    // 地址信息缓存
    mutable std::mutex address_mutex_;
    std::string local_address_;
    int local_port_;
    
    // 接收信息缓存
    mutable std::mutex receive_mutex_;
    boost::asio::ip::udp::endpoint last_sender_endpoint_;
    std::string last_sender_address_;
    int last_sender_port_;

    // 内部方法
    void UpdateError(const boost::system::error_code& ec);
    void UpdateAddressInfo();
    void SetSocketOptions();
    void HandleReceive(const boost::system::error_code& ec, size_t bytes_transferred);
    void HandleSend(const boost::system::error_code& ec, size_t bytes_transferred);
    void HandleTimeout(const boost::system::error_code& ec);
    void StartReceive();
    void StopInternal();
    
    // 高性能优化方法
    void OptimizeSocket();
    void SetSocketBufferSizes();
    void EnablePacketInfo();
    void SetReuseAddress();
    
    // 多播相关
    boost::asio::ip::address GetMulticastAddress(const std::string& group);
    void JoinMulticastGroup(const boost::asio::ip::address& group_addr);
    void LeaveMulticastGroup(const boost::asio::ip::address& group_addr);
    
    // 端点解析
    boost::asio::ip::udp::endpoint ResolveEndpoint(const char* host, int port);
};
