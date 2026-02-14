#include "BoostTcpClientLite.h"
#include <iostream>

BoostTcpClientLite::BoostTcpClientLite()
    : io_context_(std::make_unique<boost::asio::io_context>())
    , socket_(std::make_unique<boost::asio::ip::tcp::socket>(*io_context_))
    , resolver_(std::make_unique<boost::asio::ip::tcp::resolver>(*io_context_))
    , timeout_duration_(5000)
    , connected_(false)
    , connecting_(false)
    , stopped_(false)
    , buffer_size_(DEFAULT_BUFFER_SIZE)
    , keep_alive_enabled_(true)
    , error_code_(0)
    , local_port_(0)
    , remote_port_(0)
{
    SetSocketOptions();
}

BoostTcpClientLite::~BoostTcpClientLite() {
    StopInternal();
}

void BoostTcpClientLite::Destroy() {
    delete this;
}

bool BoostTcpClientLite::Connect(const char* host, int port) {
    return ConnectWithTimeout(host, port);
}

bool BoostTcpClientLite::Disconnect() {
    if (!connected_.load()) {
        return true;
    }
    
    StopInternal();
    return true;
}

bool BoostTcpClientLite::IsConnected() const {
    return connected_.load();
}

bool BoostTcpClientLite::IsConnecting() const {
    return connecting_.load();
}

bool BoostTcpClientLite::Send(const uint8_t* data, size_t len) {
    if (!connected_.load() || !data || len == 0) {
        UpdateError(-1, "Invalid parameters or not connected");
        return false;
    }
    
    try {
        // 使用同步发送，避免复杂的异步处理
        size_t bytes_sent = boost::asio::write(*socket_, 
            boost::asio::buffer(data, len));
        
        return bytes_sent == len;
        
    } catch (const std::exception& e) {
        UpdateError(-2, std::string("Send failed: ") + e.what());
        connected_.store(false);
        return false;
    }
}

bool BoostTcpClientLite::SendString(const char* data) {
    if (!data) return false;
    return Send(reinterpret_cast<const uint8_t*>(data), strlen(data));
}

bool BoostTcpClientLite::Receive(uint8_t* buffer, size_t maxLen, size_t& outLen) {
    if (!connected_.load() || !buffer || maxLen == 0) {
        outLen = 0;
        UpdateError(-1, "Invalid parameters or not connected");
        return false;
    }
    
    try {
        // 使用同步接收，设置超时
        socket_->non_blocking(true);
        
        auto start_time = std::chrono::steady_clock::now();
        
        while (true) {
            try {
                size_t bytes_received = socket_->receive(
                    boost::asio::buffer(buffer, maxLen));
                
                outLen = bytes_received;
                return true;
                
            } catch (const boost::asio::would_block&) {
                // 检查超时
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time);
                
                if (elapsed >= timeout_duration_) {
                    UpdateError(-3, "Receive timeout");
                    outLen = 0;
                    return false;
                }
                
                // 短暂休眠避免CPU占用过高
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        
    } catch (const std::exception& e) {
        UpdateError(-2, std::string("Receive failed: ") + e.what());
        outLen = 0;
        return false;
    }
}

bool BoostTcpClientLite::ReceiveString(char* buffer, size_t maxLen, size_t& outLen) {
    return Receive(reinterpret_cast<uint8_t*>(buffer), maxLen, outLen);
}

const char* BoostTcpClientLite::GetLocalAddress() const {
    std::lock_guard<std::mutex> lock(address_mutex_);
    return local_address_.c_str();
}

const char* BoostTcpClientLite::GetRemoteAddress() const {
    std::lock_guard<std::mutex> lock(address_mutex_);
    return remote_address_.c_str();
}

int BoostTcpClientLite::GetLocalPort() const {
    std::lock_guard<std::mutex> lock(address_mutex_);
    return local_port_;
}

int BoostTcpClientLite::GetRemotePort() const {
    std::lock_guard<std::mutex> lock(address_mutex_);
    return remote_port_;
}

void BoostTcpClientLite::SetTimeout(int milliseconds) {
    timeout_duration_ = std::chrono::milliseconds(milliseconds);
}

int BoostTcpClientLite::GetTimeout() const {
    return static_cast<int>(timeout_duration_.count());
}

void BoostTcpClientLite::SetBufferSize(int size) {
    buffer_size_ = std::max(1024, std::min(size, 65536));
    SetSocketBufferSizes();
}

int BoostTcpClientLite::GetBufferSize() const {
    return static_cast<int>(buffer_size_);
}

void BoostTcpClientLite::SetKeepAlive(bool enable) {
    keep_alive_enabled_ = enable;
    if (socket_ && socket_->is_open()) {
        boost::asio::socket_base::keep_alive option(enable);
        socket_->set_option(option);
    }
}

bool BoostTcpClientLite::IsKeepAliveEnabled() const {
    return keep_alive_enabled_;
}

const char* BoostTcpClientLite::GetLastError() const {
    std::lock_guard<std::mutex> lock(error_mutex_);
    return last_error_.c_str();
}

int BoostTcpClientLite::GetErrorCode() const {
    std::lock_guard<std::mutex> lock(error_mutex_);
    return error_code_;
}

// 私有方法实现

void BoostTcpClientLite::UpdateError(int code, const std::string& message) {
    std::lock_guard<std::mutex> lock(error_mutex_);
    error_code_ = code;
    last_error_ = message;
}

void BoostTcpClientLite::UpdateAddressInfo() {
    if (!socket_ || !socket_->is_open()) {
        return;
    }
    
    try {
        std::lock_guard<std::mutex> lock(address_mutex_);
        
        // 获取本地地址
        auto local_endpoint = socket_->local_endpoint();
        local_address_ = local_endpoint.address().to_string();
        local_port_ = local_endpoint.port();
        
        // 获取远程地址
        auto remote_endpoint = socket_->remote_endpoint();
        remote_address_ = remote_endpoint.address().to_string();
        remote_port_ = remote_endpoint.port();
        
    } catch (const std::exception& e) {
        // 忽略地址获取错误
    }
}

void BoostTcpClientLite::SetSocketOptions() {
    if (!socket_) return;
    
    try {
        // 设置KeepAlive
        SetKeepAlive(keep_alive_enabled_);
        
        // 优化socket
        OptimizeSocket();
        
    } catch (const std::exception& e) {
        // 忽略设置选项错误
    }
}

void BoostTcpClientLite::StartReceive() {
    // 轻量级版本不使用异步接收
}

void BoostTcpClientLite::StopInternal() {
    stopped_.store(true);
    connected_.store(false);
    connecting_.store(false);
    
    if (socket_ && socket_->is_open()) {
        try {
            socket_->close();
        } catch (...) {
            // 忽略关闭错误
        }
    }
    
    if (io_context_) {
        try {
            io_context_->stop();
        } catch (...) {
            // 忽略停止错误
        }
    }
}

void BoostTcpClientLite::OptimizeSocket() {
    if (!socket_ || !socket_->is_open()) return;
    
    try {
        // 禁用Nagle算法
        EnableNoDelay();
        
        // 设置缓冲区
        SetSocketBufferSizes();
        
    } catch (const std::exception& e) {
        // 忽略优化错误
    }
}

void BoostTcpClientLite::EnableNoDelay() {
    boost::asio::ip::tcp::no_delay option(true);
    socket_->set_option(option);
}

void BoostTcpClientLite::SetSocketBufferSizes() {
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

bool BoostTcpClientLite::ConnectWithTimeout(const char* host, int port) {
    if (connected_.load() || connecting_.load()) {
        UpdateError(-1, "Already connected or connecting");
        return false;
    }

    try {
        connecting_.store(true);
        stopped_.store(false);
        
        // 重置socket
        if (socket_->is_open()) {
            socket_->close();
        }
        
        // 解析地址
        std::string port_str = std::to_string(port);
        auto results = resolver_->resolve(host, port_str);
        
        // 使用同步连接，但设置超时
        socket_->non_blocking(true);
        
        auto start_time = std::chrono::steady_clock::now();
        
        for (const auto& endpoint : results) {
            boost::system::error_code ec;
            socket_->connect(endpoint, ec);
            
            if (!ec) {
                // 连接成功
                connected_.store(true);
                UpdateAddressInfo();
                SetSocketOptions();
                connecting_.store(false);
                return true;
            }
            
            if (ec != boost::asio::error::in_progress && 
                ec != boost::asio::error::would_block) {
                // 连接失败，尝试下一个地址
                continue;
            }
            
            // 等待连接完成
            while (true) {
                // 检查socket状态
                if (socket_->is_open()) {
                    boost::asio::socket_base::error socket_error;
                    socket_->get_option(socket_error, ec);
                    
                    if (!ec && socket_error.value() == 0) {
                        // 连接成功
                        connected_.store(true);
                        UpdateAddressInfo();
                        SetSocketOptions();
                        connecting_.store(false);
                        return true;
                    }
                }
                
                // 检查超时
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time);
                
                if (elapsed >= timeout_duration_) {
                    UpdateError(-3, "Connection timeout");
                    connecting_.store(false);
                    return false;
                }
                
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        
        UpdateError(-4, "Could not connect to any endpoint");
        connecting_.store(false);
        return false;
        
    } catch (const std::exception& e) {
        UpdateError(-2, std::string("Connection exception: ") + e.what());
        connecting_.store(false);
        return false;
    }
}
