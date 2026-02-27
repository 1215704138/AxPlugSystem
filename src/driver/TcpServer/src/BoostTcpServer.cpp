#include "../include/BoostTcpServer.h"
#include "include/BoostTcpClient.h"
#include <boost/bind.hpp>
#include <iostream>

BoostTcpServer::BoostTcpServer()
    : io_context_(std::make_unique<boost::asio::io_context>())
    , acceptor_(std::make_unique<boost::asio::ip::tcp::acceptor>(*io_context_))
    , accept_socket_(std::make_unique<boost::asio::ip::tcp::socket>(*io_context_))
    , running_(false)
    , stopped_(false)
    , max_connections_(1000)
    , timeout_ms_(30000)
    , reuse_address_enabled_(true)
    , listen_port_(0)
    , error_code_(0)
{
    // Fix 3.11: Removed SetAcceptorOptions() — acceptor is not open yet
}

BoostTcpServer::~BoostTcpServer() {
    StopListening();
}

void BoostTcpServer::Destroy() {
    delete this;
}

bool BoostTcpServer::Listen(int port, int backlog) {
    if (running_.load()) {
        UpdateError(boost::system::error_code(boost::system::errc::operation_in_progress, 
                                            boost::system::system_category()));
        return false;
    }

    try {
        stopped_.store(false);
        listen_port_ = port;
        
        // 创建监听端点
        boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::tcp::v4(), port);
        
        // 打开acceptor
        acceptor_->open(endpoint.protocol());
        SetAcceptorOptions();
        
        // 绑定地址
        acceptor_->bind(endpoint);
        
        // 开始监听
        acceptor_->listen(std::max(backlog, DEFAULT_BACKLOG));
        
        // 更新监听地址信息
        listen_address_ = acceptor_->local_endpoint().address().to_string();
        
        // Fix 2.12/A.11: Set running_ BEFORE starting threads and accept,
        // so IsListening()/IsRunning() returns true once connections begin.
        running_.store(true);
        
        StartWorkerThreads();
        StartAccept();
        
        return true;
        
    } catch (const std::exception& e) {
        UpdateError(boost::system::error_code(boost::system::errc::resource_unavailable_try_again, 
                                            boost::system::system_category()));
        return false;
    }
}

bool BoostTcpServer::StopListening() {
    if (!running_.load()) {
        return true;
    }
    
    stopped_.store(true);
    running_.store(false);
    
    // 停止acceptor
    if (acceptor_ && acceptor_->is_open()) {
        boost::system::error_code ec;
        acceptor_->close(ec);
    }
    
    // 断开所有客户端
    DisconnectAllClients();
    
    // 停止工作线程
    StopWorkerThreads();
    
    // 停止io_context
    if (io_context_) {
        io_context_->stop();
    }
    
    return true;
}

bool BoostTcpServer::IsListening() const {
    return running_.load();
}

bool BoostTcpServer::IsRunning() const {
    return running_.load() && !stopped_.load();
}

ITcpClient* BoostTcpServer::Accept() {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    if (!pending_clients_.empty()) {
        ITcpClient* client = pending_clients_.back();
        pending_clients_.pop_back();
        return client;
    }
    return nullptr;
}

ITcpClient* BoostTcpServer::GetClient(int index) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    if (index >= 0 && index < static_cast<int>(clients_.size())) {
        return clients_[index].get();
    }
    
    return nullptr;
}

bool BoostTcpServer::DisconnectClient(ITcpClient* client) {
    if (!client) return false;
    
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    auto it = client_index_map_.find(client);
    if (it != client_index_map_.end()) {
        size_t index = it->second;
        if (index < clients_.size() && clients_[index].get() == client) {
            clients_[index]->Disconnect();
            client_index_map_.erase(it);
            clients_[index].reset();
            return true;
        }
    }
    
    return false;
}

bool BoostTcpServer::DisconnectAllClients() {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    for (auto& client : clients_) {
        if (client) {
            client->Disconnect();
        }
    }
    
    client_index_map_.clear();
    pending_clients_.clear();
    clients_.clear();
    return true;
}

const char* BoostTcpServer::GetListenAddress() const {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    return listen_address_.c_str();
}

int BoostTcpServer::GetListenPort() const {
    return listen_port_;
}

int BoostTcpServer::GetMaxConnections() const {
    return max_connections_;
}

void BoostTcpServer::SetMaxConnections(int max) {
    max_connections_ = std::max(1, max);
}

int BoostTcpServer::GetConnectedCount() const {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    int count = 0;
    for (const auto& client : clients_) {
        if (client && client->IsConnected()) {
            count++;
        }
    }
    
    return count;
}

void BoostTcpServer::SetTimeout(int milliseconds) {
    timeout_ms_ = milliseconds;
    
    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (auto& client : clients_) {
        if (client) {
            client->SetTimeout(milliseconds);
        }
    }
}

int BoostTcpServer::GetTimeout() const {
    return timeout_ms_;
}

void BoostTcpServer::SetReuseAddress(bool enable) {
    reuse_address_enabled_ = enable;
    
    if (acceptor_ && acceptor_->is_open()) {
        boost::asio::socket_base::reuse_address option(enable);
        acceptor_->set_option(option);
    }
}

bool BoostTcpServer::IsReuseAddressEnabled() const {
    return reuse_address_enabled_;
}

const char* BoostTcpServer::GetLastError() const {
    std::lock_guard<std::mutex> lock(error_mutex_);
    return last_error_.c_str();
}

int BoostTcpServer::GetErrorCode() const {
    std::lock_guard<std::mutex> lock(error_mutex_);
    return error_code_;
}

// 私有方法实现

void BoostTcpServer::UpdateError(const boost::system::error_code& ec) {
    std::lock_guard<std::mutex> lock(error_mutex_);
    error_code_ = ec.value();
    last_error_ = ec.message();
}

void BoostTcpServer::StartAccept() {
    if (stopped_.load() || !acceptor_->is_open()) {
        return;
    }
    
    if (GetConnectedCount() >= max_connections_) {
        retry_timer_ = std::make_unique<boost::asio::deadline_timer>(*io_context_);
        retry_timer_->expires_from_now(boost::posix_time::milliseconds(100));
        retry_timer_->async_wait([this](const boost::system::error_code& ec) {
            if (!ec) {
                StartAccept();
            }
        });
        return;
    }
    
    accept_socket_ = std::make_unique<boost::asio::ip::tcp::socket>(*io_context_);
    acceptor_->async_accept(*accept_socket_, [this](const boost::system::error_code& ec) { HandleAccept(ec); });
}

void BoostTcpServer::HandleAccept(const boost::system::error_code& ec) {
    if (ec) {
        UpdateError(ec);
        if (ec != boost::asio::error::operation_aborted) {
            StartAccept();
        }
        return;
    }
    
    try {
        auto client = std::make_unique<BoostTcpClient>();
        
        // Fix 1.5: Transfer accepted socket ownership to client via AttachSocket
        if (!client->AttachSocket(std::move(*accept_socket_))) {
            UpdateError(boost::system::error_code(boost::system::errc::resource_unavailable_try_again, boost::system::system_category()));
            StartAccept();
            return;
        }
        
        std::lock_guard<std::mutex> lock(clients_mutex_);
        
        size_t index = GetNextClientIndex();
        if (index < clients_.size()) {
            clients_[index] = std::move(client);
        } else {
            clients_.push_back(std::move(client));
            index = clients_.size() - 1;
        }
        
        ITcpClient* client_ptr = clients_[index].get();
        client_index_map_[client_ptr] = index;
        client_ptr->SetTimeout(timeout_ms_);
        
        // Fix 1.13: Push to pending queue so Accept() returns only new connections
        pending_clients_.push_back(client_ptr);
        
    } catch (const std::exception& e) {
        UpdateError(boost::system::error_code(boost::system::errc::resource_unavailable_try_again, boost::system::system_category()));
    }
    
    StartAccept();
}

void BoostTcpServer::StartWorkerThreads() {
    // 根据CPU核心数确定线程数
    size_t thread_count = std::max(1u, std::thread::hardware_concurrency());
    thread_count = std::min(thread_count, THREAD_POOL_SIZE);
    
    for (size_t i = 0; i < thread_count; ++i) {
        worker_threads_.emplace_back(&BoostTcpServer::WorkerThread, this, i);
    }
}

void BoostTcpServer::StopWorkerThreads() {
    if (io_context_) {
        io_context_->stop();
    }
    
    for (auto& thread : worker_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    
    worker_threads_.clear();
}

void BoostTcpServer::RemoveClient(ITcpClient* client) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    auto it = client_index_map_.find(client);
    if (it != client_index_map_.end()) {
        size_t index = it->second;
        if (index < clients_.size()) {
            clients_[index].reset();
        }
        client_index_map_.erase(it);
    }
}

void BoostTcpServer::OptimizeAcceptor() {
    if (!acceptor_ || !acceptor_->is_open()) return;
    
    try {
        // 设置acceptor缓冲区大小
        boost::asio::socket_base::receive_buffer_size option(65536);
        acceptor_->set_option(option);
        
        // 启用快速循环
        boost::asio::ip::tcp::acceptor::reuse_address reuse(true);
        acceptor_->set_option(reuse);
        
    } catch (const std::exception& e) {
        // 忽略优化错误
    }
}

void BoostTcpServer::SetAcceptorOptions() {
    if (!acceptor_) return;
    
    try {
        // 设置地址重用
        SetReuseAddress(reuse_address_enabled_);
        
        // 优化acceptor
        OptimizeAcceptor();
        
    } catch (const std::exception& e) {
        // 忽略设置错误
    }
}

size_t BoostTcpServer::GetNextClientIndex() {
    // 查找空闲位置
    for (size_t i = 0; i < clients_.size(); ++i) {
        if (!clients_[i]) {
            return i;
        }
    }
    
    // 返回新位置
    return clients_.size();
}

void BoostTcpServer::PreAllocateClients() {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    // 预分配客户端容器以提高性能
    clients_.reserve(max_connections_);
}

void BoostTcpServer::CleanupDisconnectedClients() {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    for (auto it = clients_.begin(); it != clients_.end(); ++it) {
        if (*it && !(*it)->IsConnected()) {
            client_index_map_.erase(it->get());
            it->reset();
        }
    }
}

void BoostTcpServer::WorkerThread(size_t thread_id) {
    try {
        // Fix 3.10: Replace deprecated io_context::work with executor_work_guard
        auto work = boost::asio::make_work_guard(*io_context_);
        io_context_->run();
        
    } catch (const std::exception& e) {
        // 线程异常处理
        UpdateError(boost::system::error_code(boost::system::errc::resource_unavailable_try_again, 
                                            boost::system::system_category()));
    }
}
