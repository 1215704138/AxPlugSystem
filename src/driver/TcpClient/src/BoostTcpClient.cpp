#include "../include/BoostTcpClient.h"
#include <boost/asio/placeholders.hpp>
#include <boost/system/error_code.hpp>
#include <iostream>

BoostTcpClient::BoostTcpClient()
    : io_context_(std::make_unique<boost::asio::io_context>())
    , socket_(std::make_unique<boost::asio::ip::tcp::socket>(*io_context_))
    , resolver_(std::make_unique<boost::asio::ip::tcp::resolver>(*io_context_))
    , connect_timer_(std::make_unique<boost::asio::deadline_timer>(*io_context_))
    , receive_timer_(std::make_unique<boost::asio::deadline_timer>(*io_context_))
    , connected_(false)
    , connecting_(false)
    , stopped_(false)
    , buffer_size_(DEFAULT_BUFFER_SIZE)
    , timeout_ms_(5000)
    , keep_alive_enabled_(true)
    , error_code_(0)
    , local_port_(0)
    , remote_port_(0)
{
    // Fix 3.13: Removed SetSocketOptions() — socket is not open yet
}

BoostTcpClient::~BoostTcpClient() {
    StopInternal();
}

void BoostTcpClient::Destroy() {
    delete this;
}

bool BoostTcpClient::AttachSocket(boost::asio::ip::tcp::socket&& socket) {
    if (connected_.load() || connecting_.load()) {
        return false;
    }
    try {
        stopped_.store(false);
        *socket_ = std::move(socket);
        connected_.store(true);
        SetSocketOptions();
        UpdateAddressInfo();
        return true;
    } catch (const std::exception& e) {
        UpdateError(boost::system::error_code(boost::system::errc::resource_unavailable_try_again, boost::system::system_category()));
        return false;
    }
}

void BoostTcpClient::AttachUpdateState() {
    connected_.store(true);
    SetSocketOptions();
    UpdateAddressInfo();
}

bool BoostTcpClient::Connect(const char* host, int port) {
    if (connected_.load() || connecting_.load()) {
        UpdateError(boost::system::error_code(boost::system::errc::operation_in_progress, boost::system::system_category()));
        return false;
    }

    try {
        connecting_.store(true);
        stopped_.store(false);
        
        if (socket_->is_open()) socket_->close();
        
        // Synchronous resolve
        boost::system::error_code ec;
        std::string port_str = std::to_string(port);
        auto results = resolver_->resolve(host, port_str, ec);
        if (ec) { UpdateError(ec); connecting_.store(false); return false; }
        
        // Non-blocking connect with timeout via select
        auto endpoint_it = results.begin();
        socket_->open(endpoint_it->endpoint().protocol(), ec);
        if (ec) { UpdateError(ec); connecting_.store(false); return false; }
        
        socket_->non_blocking(true);
        socket_->connect(endpoint_it->endpoint(), ec);
        
        if (ec == boost::asio::error::in_progress || ec == boost::asio::error::would_block) {
            fd_set writefds, errfds;
            FD_ZERO(&writefds);
            FD_ZERO(&errfds);
            FD_SET(socket_->native_handle(), &writefds);
            FD_SET(socket_->native_handle(), &errfds);
            timeval tv;
            tv.tv_sec = timeout_ms_ / 1000;
            tv.tv_usec = (timeout_ms_ % 1000) * 1000;
            int sel = ::select(0, nullptr, &writefds, &errfds, &tv);
            if (sel <= 0 || FD_ISSET(socket_->native_handle(), &errfds)) {
                socket_->close();
                UpdateError(boost::system::error_code(boost::system::errc::timed_out, boost::system::system_category()));
                connecting_.store(false);
                return false;
            }
            // Verify connection succeeded
            int optval = 0;
            int optlen = sizeof(optval);
            ::getsockopt(socket_->native_handle(), SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&optval), &optlen);
            if (optval != 0) {
                socket_->close();
                UpdateError(boost::system::error_code(optval, boost::system::system_category()));
                connecting_.store(false);
                return false;
            }
        } else if (ec) {
            UpdateError(ec);
            connecting_.store(false);
            return false;
        }
        
        socket_->non_blocking(false);
        connected_.store(true);
        connecting_.store(false);
        SetSocketOptions();
        ApplySocketTimeouts();
        UpdateAddressInfo();
        return true;
        
    } catch (const std::exception& e) {
        UpdateError(boost::system::error_code(boost::system::errc::resource_unavailable_try_again, boost::system::system_category()));
        connecting_.store(false);
        return false;
    }
}

bool BoostTcpClient::Disconnect() {
    if (!connected_.load()) {
        return true;
    }
    
    std::lock_guard<std::mutex> lock(io_mutex_);
    StopInternal();
    return true;
}

bool BoostTcpClient::IsConnected() const {
    return connected_.load();
}

bool BoostTcpClient::IsConnecting() const {
    return connecting_.load();
}

bool BoostTcpClient::Send(const uint8_t* data, size_t len) {
    if (!connected_.load() || !data || len == 0) return false;
    
    std::lock_guard<std::mutex> lock(io_mutex_);
    try {
        boost::system::error_code ec;
        size_t total_sent = boost::asio::write(*socket_, boost::asio::buffer(data, len), ec);
        if (ec) {
            UpdateError(ec);
            connected_.store(false);
            return false;
        }
        return total_sent == len;
    } catch (const std::exception& e) {
        UpdateError(boost::system::error_code(boost::system::errc::resource_unavailable_try_again, boost::system::system_category()));
        return false;
    }
}

bool BoostTcpClient::SendString(const char* data) {
    if (!data) return false;
    return Send(reinterpret_cast<const uint8_t*>(data), strlen(data));
}

bool BoostTcpClient::Receive(uint8_t* buffer, size_t maxLen, size_t& outLen) {
    if (!connected_.load() || !buffer || maxLen == 0) { outLen = 0; return false; }
    
    std::lock_guard<std::mutex> lock(io_mutex_);
    try {
        boost::system::error_code ec;
        outLen = socket_->read_some(boost::asio::buffer(buffer, maxLen), ec);
        if (ec) {
            UpdateError(ec);
            if (ec != boost::asio::error::would_block) connected_.store(false);
            outLen = 0;
            return false;
        }
        if (outLen == 0) { connected_.store(false); }
        return true;
    } catch (const std::exception& e) {
        UpdateError(boost::system::error_code(boost::system::errc::resource_unavailable_try_again, boost::system::system_category()));
        outLen = 0;
        return false;
    }
}

bool BoostTcpClient::ReceiveString(char* buffer, size_t maxLen, size_t& outLen) {
    return Receive(reinterpret_cast<uint8_t*>(buffer), maxLen, outLen);
}

const char* BoostTcpClient::GetLocalAddress() const {
    std::lock_guard<std::mutex> lock(address_mutex_);
    return local_address_.c_str();
}

const char* BoostTcpClient::GetRemoteAddress() const {
    std::lock_guard<std::mutex> lock(address_mutex_);
    return remote_address_.c_str();
}

int BoostTcpClient::GetLocalPort() const {
    std::lock_guard<std::mutex> lock(address_mutex_);
    return local_port_;
}

int BoostTcpClient::GetRemotePort() const {
    std::lock_guard<std::mutex> lock(address_mutex_);
    return remote_port_;
}

void BoostTcpClient::SetTimeout(int milliseconds) {
    timeout_ms_ = milliseconds;
    ApplySocketTimeouts();
}

int BoostTcpClient::GetTimeout() const {
    return timeout_ms_;
}

void BoostTcpClient::SetBufferSize(int size) {
    buffer_size_ = std::max(1024, std::min(size, 65536));
    SetSocketBufferSizes();
}

int BoostTcpClient::GetBufferSize() const {
    return static_cast<int>(buffer_size_);
}

void BoostTcpClient::SetKeepAlive(bool enable) {
    keep_alive_enabled_ = enable;
    if (socket_ && socket_->is_open()) {
        boost::asio::socket_base::keep_alive option(enable);
        socket_->set_option(option);
    }
}

bool BoostTcpClient::IsKeepAliveEnabled() const {
    return keep_alive_enabled_;
}

const char* BoostTcpClient::GetLastError() const {
    std::lock_guard<std::mutex> lock(error_mutex_);
    return last_error_.c_str();
}

int BoostTcpClient::GetErrorCode() const {
    std::lock_guard<std::mutex> lock(error_mutex_);
    return error_code_;
}

// 私有方法实现

void BoostTcpClient::UpdateError(const boost::system::error_code& ec) {
    std::lock_guard<std::mutex> lock(error_mutex_);
    error_code_ = ec.value();
    last_error_ = ec.message();
}

void BoostTcpClient::UpdateAddressInfo() {
    if (!socket_ || !socket_->is_open()) {
        return;
    }
    
    try {
        std::lock_guard<std::mutex> lock(address_mutex_);
        
        // 获取本地地址
        boost::system::error_code ec;
        auto local_endpoint = socket_->local_endpoint(ec);
        if (!ec) {
            local_address_ = local_endpoint.address().to_string();
            local_port_ = local_endpoint.port();
        }
        
        // 获取远程地址
        auto remote_endpoint = socket_->remote_endpoint(ec);
        if (!ec) {
            remote_address_ = remote_endpoint.address().to_string();
            remote_port_ = remote_endpoint.port();
        }
        
    } catch (const std::exception& e) {
        // 忽略地址获取错误
    }
}

void BoostTcpClient::SetSocketOptions() {
    if (!socket_) return;
    
    try {
        // 设置KeepAlive
        SetKeepAlive(keep_alive_enabled_);
        
        // 优化socket选项
        OptimizeSocket();
        
    } catch (const std::exception& e) {
        // 忽略设置选项错误
    }
}

void BoostTcpClient::HandleConnect(const boost::system::error_code& ec) {
    connecting_.store(false);
    
    if (ec) {
        UpdateError(ec);
        connected_.store(false);
    } else {
        connected_.store(true);
        SetSocketOptions();
    }
}

void BoostTcpClient::HandleReceive(const boost::system::error_code& ec, size_t bytes_transferred) {
    if (ec) {
        if (ec != boost::asio::error::operation_aborted) {
            UpdateError(ec);
            connected_.store(false);
        }
        return;
    }
    
    // 数据处理可以在这里添加回调机制
    StartReceive();
}

void BoostTcpClient::HandleSend(const boost::system::error_code& ec, size_t bytes_transferred) {
    if (ec) {
        UpdateError(ec);
        connected_.store(false);
    }
}

void BoostTcpClient::HandleTimeout(const boost::system::error_code& ec) {
    if (ec != boost::asio::error::operation_aborted) {
        // 超时处理
        connecting_.store(false);
        if (socket_ && socket_->is_open()) {
            socket_->close();
        }
        UpdateError(boost::system::error_code(boost::system::errc::timed_out, 
                                            boost::system::system_category()));
    }
}

void BoostTcpClient::StartReceive() {
    if (!connected_.load() || stopped_.load()) {
        return;
    }
    
    socket_->async_receive(boost::asio::buffer(receive_buffer_),
        [this](const boost::system::error_code& ec, size_t bytes_transferred) {
            HandleReceive(ec, bytes_transferred);
        });
}

void BoostTcpClient::StopInternal() {
    stopped_.store(true);
    connected_.store(false);
    connecting_.store(false);
    
    if (connect_timer_) connect_timer_->cancel();
    if (receive_timer_) receive_timer_->cancel();
    
    if (socket_ && socket_->is_open()) {
        boost::system::error_code ec;
        socket_->close(ec);
    }
    
    if (io_context_) {
        io_context_->stop();
    }
}

void BoostTcpClient::OptimizeSocket() {
    if (!socket_ || !socket_->is_open()) return;
    
    try {
        // 禁用Nagle算法以减少延迟
        EnableNoDelay();
        
        // 启用快速ACK
        EnableQuickAck();
        
        // 设置缓冲区大小
        SetSocketBufferSizes();
        
    } catch (const std::exception& e) {
        // 忽略优化错误
    }
}

void BoostTcpClient::EnableNoDelay() {
    boost::asio::ip::tcp::no_delay option(true);
    socket_->set_option(option);
}

void BoostTcpClient::EnableQuickAck() {
#ifdef TCP_QUICKACK
    int flag = 1;
    setsockopt(socket_->native_handle(), IPPROTO_TCP, TCP_QUICKACK, &flag, sizeof(flag));
#endif
}

void BoostTcpClient::SetSocketBufferSizes() {
    if (!socket_ || !socket_->is_open()) return;
    
    try {
        boost::asio::socket_base::send_buffer_size send_option(buffer_size_);
        socket_->set_option(send_option);
        boost::asio::socket_base::receive_buffer_size recv_option(buffer_size_);
        socket_->set_option(recv_option);
    } catch (const std::exception& e) {
        // 忽略缓冲区设置错误
    }
}

void BoostTcpClient::ApplySocketTimeouts() {
    if (!socket_ || !socket_->is_open()) return;
    try {
        DWORD timeout_val = static_cast<DWORD>(timeout_ms_);
        ::setsockopt(socket_->native_handle(), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout_val), sizeof(timeout_val));
        ::setsockopt(socket_->native_handle(), SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout_val), sizeof(timeout_val));
    } catch (const std::exception& e) {
        // 忽略超时设置错误
    }
}
