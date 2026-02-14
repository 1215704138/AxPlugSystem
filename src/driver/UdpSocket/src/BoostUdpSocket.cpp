#include "../include/BoostUdpSocket.h"
#include <boost/asio/placeholders.hpp>
#include <boost/system/error_code.hpp>
#include <iostream>

BoostUdpSocket::BoostUdpSocket()
    : io_context_(std::make_unique<boost::asio::io_context>())
    , socket_(std::make_unique<boost::asio::ip::udp::socket>(*io_context_))
    , resolver_(std::make_unique<boost::asio::ip::udp::resolver>(*io_context_))
    , receive_timer_(std::make_unique<boost::asio::deadline_timer>(*io_context_))
    , send_timer_(std::make_unique<boost::asio::deadline_timer>(*io_context_))
    , bound_(false)
    , stopped_(false)
    , buffer_size_(DEFAULT_BUFFER_SIZE)
    , timeout_ms_(5000)
    , broadcast_enabled_(false)
    , ttl_(1)
    , has_default_remote_(false)
    , error_code_(0)
    , local_port_(0)
    , last_sender_port_(0)
{
    SetSocketOptions();
}

BoostUdpSocket::~BoostUdpSocket() {
    StopInternal();
}

void BoostUdpSocket::Destroy() {
    delete this;
}

bool BoostUdpSocket::Bind(int port) {
    if (bound_.load()) {
        UpdateError(boost::system::error_code(boost::system::errc::operation_in_progress, 
                                            boost::system::system_category()));
        return false;
    }

    try {
        stopped_.store(false);
        
        // 创建绑定端点
        boost::asio::ip::udp::endpoint endpoint(boost::asio::ip::udp::v4(), port);
        
        // 打开socket
        socket_->open(endpoint.protocol());
        SetSocketOptions();
        
        // 绑定地址
        socket_->bind(endpoint);
        
        // 更新地址信息
        UpdateAddressInfo();
        
        // 启动异步接收
        StartReceive();
        
        bound_.store(true);
        
        return true;
        
    } catch (const std::exception& e) {
        UpdateError(boost::system::error_code(boost::system::errc::resource_unavailable_try_again, 
                                            boost::system::system_category()));
        return false;
    }
}

bool BoostUdpSocket::Unbind() {
    if (!bound_.load()) {
        return true;
    }
    
    StopInternal();
    return true;
}

bool BoostUdpSocket::IsBound() const {
    return bound_.load();
}

bool BoostUdpSocket::Send(const uint8_t* data, size_t len) {
    if (!bound_.load() || !has_default_remote_ || !data || len == 0) {
        return false;
    }
    
    return SendTo(default_remote_endpoint_.address().to_string().c_str(),
                  default_remote_endpoint_.port(), data, len);
}

bool BoostUdpSocket::SendString(const char* data) {
    if (!data) return false;
    return Send(reinterpret_cast<const uint8_t*>(data), strlen(data));
}

bool BoostUdpSocket::SendTo(const char* host, int port, const uint8_t* data, size_t len) {
    if (!bound_.load() || !data || len == 0 || len > MAX_UDP_PACKET_SIZE) {
        return false;
    }
    
    try {
        auto endpoint = ResolveEndpoint(host, port);
        
        boost::system::error_code ec;
        size_t bytes_sent = socket_->send_to(boost::asio::buffer(data, len), endpoint, 0, ec);
        
        if (ec) {
            UpdateError(ec);
            return false;
        }
        
        return bytes_sent == len;
        
    } catch (const std::exception& e) {
        UpdateError(boost::system::error_code(boost::system::errc::resource_unavailable_try_again, 
                                            boost::system::system_category()));
        return false;
    }
}

bool BoostUdpSocket::SendStringTo(const char* host, int port, const char* data) {
    if (!data) return false;
    return SendTo(host, port, reinterpret_cast<const uint8_t*>(data), strlen(data));
}

bool BoostUdpSocket::Receive(uint8_t* buffer, size_t maxLen, size_t& outLen) {
    if (!bound_.load() || !buffer || maxLen == 0) {
        outLen = 0;
        return false;
    }
    
    try {
        boost::system::error_code ec;
        boost::asio::ip::udp::endpoint sender_endpoint;
        
        size_t bytes_received = socket_->receive_from(
            boost::asio::buffer(buffer, maxLen), sender_endpoint, 0, ec);
        
        if (ec) {
            UpdateError(ec);
            outLen = 0;
            return false;
        }
        
        // 更新发送者信息
        std::lock_guard<std::mutex> lock(receive_mutex_);
        last_sender_endpoint_ = sender_endpoint;
        last_sender_address_ = sender_endpoint.address().to_string();
        last_sender_port_ = sender_endpoint.port();
        
        outLen = bytes_received;
        return true;
        
    } catch (const std::exception& e) {
        UpdateError(boost::system::error_code(boost::system::errc::resource_unavailable_try_again, 
                                            boost::system::system_category()));
        outLen = 0;
        return false;
    }
}

bool BoostUdpSocket::ReceiveString(char* buffer, size_t maxLen, size_t& outLen) {
    return Receive(reinterpret_cast<uint8_t*>(buffer), maxLen, outLen);
}

bool BoostUdpSocket::ReceiveFrom(char* host, int hostLen, int& port, uint8_t* buffer, size_t maxLen, size_t& outLen) {
    if (!host || hostLen == 0) {
        outLen = 0;
        return false;
    }
    
    bool result = Receive(buffer, maxLen, outLen);
    
    if (result) {
        std::lock_guard<std::mutex> lock(receive_mutex_);
        strncpy(host, last_sender_address_.c_str(), hostLen - 1);
        host[hostLen - 1] = '\0';
        port = last_sender_port_;
    }
    
    return result;
}

bool BoostUdpSocket::ReceiveStringFrom(char* host, int hostLen, int& port, char* buffer, size_t maxLen, size_t& outLen) {
    return ReceiveFrom(host, hostLen, port, reinterpret_cast<uint8_t*>(buffer), maxLen, outLen);
}

bool BoostUdpSocket::EnableBroadcast(bool enable) {
    broadcast_enabled_ = enable;
    
    if (socket_ && socket_->is_open()) {
        boost::asio::socket_base::broadcast option(enable);
        boost::system::error_code ec;
        socket_->set_option(option, ec);
        
        if (ec) {
            UpdateError(ec);
            return false;
        }
    }
    
    return true;
}

bool BoostUdpSocket::IsBroadcastEnabled() const {
    return broadcast_enabled_;
}

bool BoostUdpSocket::JoinMulticast(const char* group) {
    if (!group || !bound_.load()) {
        return false;
    }
    
    try {
        auto group_addr = GetMulticastAddress(group);
        JoinMulticastGroup(group_addr);
        
        std::lock_guard<std::mutex> lock(multicast_mutex_);
        multicast_groups_.insert(group);
        
        return true;
        
    } catch (const std::exception& e) {
        UpdateError(boost::system::error_code(boost::system::errc::resource_unavailable_try_again, 
                                            boost::system::system_category()));
        return false;
    }
}

bool BoostUdpSocket::LeaveMulticast(const char* group) {
    if (!group) return false;
    
    try {
        auto group_addr = GetMulticastAddress(group);
        LeaveMulticastGroup(group_addr);
        
        std::lock_guard<std::mutex> lock(multicast_mutex_);
        multicast_groups_.erase(group);
        
        return true;
        
    } catch (const std::exception& e) {
        UpdateError(boost::system::error_code(boost::system::errc::resource_unavailable_try_again, 
                                            boost::system::system_category()));
        return false;
    }
}

bool BoostUdpSocket::GetMulticastGroups(char* groups, int maxLen, int& count) const {
    std::lock_guard<std::mutex> lock(multicast_mutex_);
    
    count = 0;
    if (!groups || maxLen == 0) {
        return true;
    }
    
    std::string result;
    for (const auto& group : multicast_groups_) {
        if (!result.empty()) {
            result += ";";
        }
        result += group;
    }
    
    strncpy(groups, result.c_str(), maxLen - 1);
    groups[maxLen - 1] = '\0';
    count = static_cast<int>(multicast_groups_.size());
    
    return true;
}

const char* BoostUdpSocket::GetLocalAddress() const {
    std::lock_guard<std::mutex> lock(address_mutex_);
    return local_address_.c_str();
}

int BoostUdpSocket::GetLocalPort() const {
    std::lock_guard<std::mutex> lock(address_mutex_);
    return local_port_;
}

void BoostUdpSocket::SetTimeout(int milliseconds) {
    timeout_ms_ = milliseconds;
}

int BoostUdpSocket::GetTimeout() const {
    return timeout_ms_;
}

void BoostUdpSocket::SetBufferSize(int size) {
    buffer_size_ = std::max(1024, std::min(size, static_cast<int>(MAX_UDP_PACKET_SIZE)));
    SetSocketBufferSizes();
}

int BoostUdpSocket::GetBufferSize() const {
    return static_cast<int>(buffer_size_);
}

void BoostUdpSocket::SetTTL(int ttl) {
    ttl_ = std::max(1, std::min(255, ttl));
    
    if (socket_ && socket_->is_open()) {
        boost::asio::ip::multicast::hops option(ttl_);
        boost::system::error_code ec;
        socket_->set_option(option, ec);
        
        if (ec) {
            UpdateError(ec);
        }
    }
}

int BoostUdpSocket::GetTTL() const {
    return ttl_;
}

const char* BoostUdpSocket::GetLastError() const {
    std::lock_guard<std::mutex> lock(error_mutex_);
    return last_error_.c_str();
}

int BoostUdpSocket::GetErrorCode() const {
    std::lock_guard<std::mutex> lock(error_mutex_);
    return error_code_;
}

// 私有方法实现

void BoostUdpSocket::UpdateError(const boost::system::error_code& ec) {
    std::lock_guard<std::mutex> lock(error_mutex_);
    error_code_ = ec.value();
    last_error_ = ec.message();
}

void BoostUdpSocket::UpdateAddressInfo() {
    if (!socket_ || !socket_->is_open()) {
        return;
    }
    
    try {
        std::lock_guard<std::mutex> lock(address_mutex_);
        
        auto local_endpoint = socket_->local_endpoint();
        local_address_ = local_endpoint.address().to_string();
        local_port_ = local_endpoint.port();
        
    } catch (const std::exception& e) {
        // 忽略地址获取错误
    }
}

void BoostUdpSocket::SetSocketOptions() {
    if (!socket_) return;
    
    try {
        // 设置广播
        EnableBroadcast(broadcast_enabled_);
        
        // 设置TTL
        SetTTL(ttl_);
        
        // 优化socket
        OptimizeSocket();
        
    } catch (const std::exception& e) {
        // 忽略设置选项错误
    }
}

void BoostUdpSocket::HandleReceive(const boost::system::error_code& ec, size_t bytes_transferred) {
    if (ec) {
        if (ec != boost::asio::error::operation_aborted) {
            UpdateError(ec);
        }
        return;
    }
    
    // 更新发送者信息
    std::lock_guard<std::mutex> lock(receive_mutex_);
    last_sender_endpoint_ = boost::asio::ip::udp::endpoint();
    last_sender_address_ = last_sender_endpoint_.address().to_string();
    last_sender_port_ = last_sender_endpoint_.port();
    
    // 继续接收
    StartReceive();
}

void BoostUdpSocket::HandleSend(const boost::system::error_code& ec, size_t bytes_transferred) {
    if (ec) {
        UpdateError(ec);
    }
}

void BoostUdpSocket::HandleTimeout(const boost::system::error_code& ec) {
    if (ec != boost::asio::error::operation_aborted) {
        // 超时处理
        UpdateError(boost::system::error_code(boost::system::errc::timed_out, 
                                            boost::system::system_category()));
    }
}

void BoostUdpSocket::StartReceive() {
    if (!bound_.load() || stopped_.load()) {
        return;
    }
    
    socket_->async_receive_from(boost::asio::buffer(receive_buffer_), last_sender_endpoint_,
        [this](const boost::system::error_code& ec, size_t bytes_transferred) {
            HandleReceive(ec, bytes_transferred);
        });
}

void BoostUdpSocket::StopInternal() {
    stopped_.store(true);
    bound_.store(false);
    
    if (receive_timer_) receive_timer_->cancel();
    if (send_timer_) send_timer_->cancel();
    
    // 离开所有多播组
    std::lock_guard<std::mutex> lock(multicast_mutex_);
    for (const auto& group : multicast_groups_) {
        try {
            auto group_addr = GetMulticastAddress(group);
            LeaveMulticastGroup(group_addr);
        } catch (...) {
            // 忽略错误
        }
    }
    multicast_groups_.clear();
    
    if (socket_ && socket_->is_open()) {
        boost::system::error_code ec;
        socket_->close(ec);
    }
    
    if (io_context_) {
        io_context_->stop();
    }
}

void BoostUdpSocket::OptimizeSocket() {
    if (!socket_ || !socket_->is_open()) return;
    
    try {
        // 设置缓冲区大小
        SetSocketBufferSizes();
        
        // 启用地址重用
        SetReuseAddress();
        
        // 启用包信息
        EnablePacketInfo();
        
    } catch (const std::exception& e) {
        // 忽略优化错误
    }
}

void BoostUdpSocket::SetSocketBufferSizes() {
    if (!socket_ || !socket_->is_open()) return;
    
    try {
        // 设置发送缓冲区
        boost::asio::socket_base::send_buffer_size send_option(buffer_size_);
        socket_->set_option(send_option);
        
        // 设置接收缓冲区
        boost::asio::socket_base::receive_buffer_size recv_option(buffer_size_);
        socket_->set_option(recv_option);
        
    } catch (const std::exception& e) {
        // 忽略缓冲区设置错误
    }
}

void BoostUdpSocket::EnablePacketInfo() {
#ifdef IP_PKTINFO
    int flag = 1;
    setsockopt(socket_->native_handle(), IPPROTO_IP, IP_PKTINFO, reinterpret_cast<const char*>(&flag), sizeof(flag));
#endif
}

void BoostUdpSocket::SetReuseAddress() {
    boost::asio::socket_base::reuse_address option(true);
    socket_->set_option(option);
}

boost::asio::ip::address BoostUdpSocket::GetMulticastAddress(const std::string& group) {
    return boost::asio::ip::make_address(group);
}

void BoostUdpSocket::JoinMulticastGroup(const boost::asio::ip::address& group_addr) {
    boost::asio::ip::multicast::join_group option(group_addr);
    socket_->set_option(option);
}

void BoostUdpSocket::LeaveMulticastGroup(const boost::asio::ip::address& group_addr) {
    boost::asio::ip::multicast::leave_group option(group_addr);
    socket_->set_option(option);
}

boost::asio::ip::udp::endpoint BoostUdpSocket::ResolveEndpoint(const char* host, int port) {
    std::string port_str = std::to_string(port);
    
    auto results = resolver_->resolve(host, port_str);
    return boost::asio::ip::udp::endpoint(*results.begin());
}
