#include "../include/TcpServer.h"
#include <iostream>
#include <cstring>

// Winsock 初始化
class WinsockInitializer {
public:
    WinsockInitializer() {
        WSADATA wsaData;
        int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (result != 0) {
            std::cerr << "WSAStartup failed: " << result << std::endl;
        }
    }
    ~WinsockInitializer() {
        WSACleanup();
    }
};
static WinsockInitializer wsInit;

// TcpClientConnection 实现
TcpClientConnection::TcpClientConnection(SOCKET socket, TcpServer* server) 
    : socket_(socket)
    , isConnected_(true)
    , timeout_(5000)
    , bufferSize_(4096)
    , localPort_(0)
    , remotePort_(0)
    , errorCode_(0)
    , server_(server) {
    
    // 获取地址信息
    sockaddr_in localAddr, remoteAddr;
    int addrLen = sizeof(localAddr);
    getsockname(socket_, (sockaddr*)&localAddr, &addrLen);
    getpeername(socket_, (sockaddr*)&remoteAddr, &addrLen);
    
    localAddr_ = inet_ntoa(localAddr.sin_addr);
    localPort_ = ntohs(localAddr.sin_port);
    remoteAddr_ = inet_ntoa(remoteAddr.sin_addr);
    remotePort_ = ntohs(remoteAddr.sin_port);
}

TcpClientConnection::~TcpClientConnection() {
    std::cout << "DEBUG: Destroying TcpClientConnection " << this << std::endl;
    Disconnect();
    
    // 安全回调：通知服务器移除自身引用，避免悬挂指针
    // 只有在服务器仍然有效时才调用回调
    if (server_) {
        try {
            server_->OnClientDisconnected(this);
        } catch (...) {
            // 如果服务器已经被销毁，忽略异常
            std::cout << "DEBUG: Server already destroyed, skipping callback" << std::endl;
        }
    }
    std::cout << "DEBUG: TcpClientConnection Destroyed " << this << std::endl;
}

bool TcpClientConnection::Connect(const char* host, int port) {
    setError("已连接的客户端不能重新连接", 0);
    return false;
}

bool TcpClientConnection::Disconnect() {
    if (socket_ != INVALID_SOCKET) {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }
    isConnected_ = false;
    return true;
}

bool TcpClientConnection::IsConnected() const {
    return isConnected_;
}

bool TcpClientConnection::IsConnecting() const {
    return false;
}

bool TcpClientConnection::Send(const uint8_t* data, size_t len) {
    if (!isConnected_ || socket_ == INVALID_SOCKET) {
        setError("未连接", 0);
        return false;
    }
    
    int sent = send(socket_, (char*)data, static_cast<int>(len), 0);
    if (sent == SOCKET_ERROR) {
        setError("发送失败", WSAGetLastError());
        return false;
    }
    
    return sent == static_cast<int>(len);
}

bool TcpClientConnection::SendString(const char* data) {
    if (!data) return false;
    size_t len = strlen(data);
    return Send(reinterpret_cast<const uint8_t*>(data), len);
}

bool TcpClientConnection::Receive(uint8_t* buffer, size_t maxLen, size_t& outLen) {
    if (!isConnected_ || socket_ == INVALID_SOCKET) {
        setError("未连接", 0);
        outLen = 0;
        return false;
    }
    
    int received = recv(socket_, (char*)buffer, static_cast<int>(maxLen), 0);
    
    if (received == SOCKET_ERROR) {
        int error = WSAGetLastError();
        if (error == WSAEWOULDBLOCK) {
            outLen = 0; // 非阻塞模式下无数据可读
            return true;
        }
        setError("接收失败", error);
        outLen = 0;
        return false;
    }
    
    if (received == 0) {
        // 连接关闭
        isConnected_ = false;
        outLen = 0;
        return true;
    }
    
    outLen = static_cast<size_t>(received);
    return true;
}

bool TcpClientConnection::ReceiveString(char* buffer, size_t maxLen, size_t& outLen) {
    return Receive(reinterpret_cast<uint8_t*>(buffer), maxLen, outLen);
}

const char* TcpClientConnection::GetLocalAddress() const {
    return localAddr_.c_str();
}

const char* TcpClientConnection::GetRemoteAddress() const {
    return remoteAddr_.c_str();
}

int TcpClientConnection::GetLocalPort() const {
    return localPort_;
}

int TcpClientConnection::GetRemotePort() const {
    return remotePort_;
}

void TcpClientConnection::SetTimeout(int milliseconds) {
    timeout_ = milliseconds;
}

int TcpClientConnection::GetTimeout() const {
    return timeout_;
}

void TcpClientConnection::SetBufferSize(int size) {
    bufferSize_ = size;
}

int TcpClientConnection::GetBufferSize() const {
    return bufferSize_;
}

void TcpClientConnection::SetKeepAlive(bool enable) {
    // 实现KeepAlive设置
}

bool TcpClientConnection::IsKeepAliveEnabled() const {
    return false;
}

const char* TcpClientConnection::GetLastError() const {
    return lastError_.c_str();
}

int TcpClientConnection::GetErrorCode() const {
    return errorCode_;
}

void TcpClientConnection::setError(const std::string& error, int code) const {
    lastError_ = error;
    errorCode_ = code;
}

std::string TcpClientConnection::getAddressFromSockaddr(const sockaddr_in& addr) const {
    return inet_ntoa(addr.sin_addr);
}

int TcpClientConnection::getPortFromSockaddr(const sockaddr_in& addr) const {
    return ntohs(addr.sin_port);
}


// TcpServer 实现
TcpServer::TcpServer() 
    : listenSocket_(INVALID_SOCKET)
    , isListening_(false)
    , isRunning_(false)
    , maxConnections_(10)
    , timeout_(5000)
    , reuseAddress_(true)
    , listenPort_(0)
    , errorCode_(0)
    , isDestroying_(false) {
}

TcpServer::~TcpServer() {
    fprintf(stderr, "[TcpServer] DEBUG: Entering ~TcpServer destructor %p\n", this);
    isDestroying_ = true;  // 设置析构标志
    StopListening();
    DisconnectAllClients();
    fprintf(stderr, "[TcpServer] DEBUG: TcpServer Destroyed\n");
}

bool TcpServer::createSocket() {
    listenSocket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket_ == INVALID_SOCKET) {
        setError("创建socket失败", WSAGetLastError());
        return false;
    }
    return true;
}

bool TcpServer::setSocketOptions() {
    // 设置超时
    if (timeout_ > 0) {
        setsockopt(listenSocket_, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout_, sizeof(timeout_));
        setsockopt(listenSocket_, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout_, sizeof(timeout_));
    }
    
    // 设置重用地址
    if (reuseAddress_) {
        int opt = 1;
        setsockopt(listenSocket_, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
    }
    
    return true;
}

void TcpServer::setError(const std::string& error, int code) const {
    lastError_ = error;
    errorCode_ = code;
}

void TcpServer::removeClient(TcpClientConnection* client) {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    auto it = std::find(clients_.begin(), clients_.end(), client);
    if (it != clients_.end()) {
        clients_.erase(it);
    }
}

void TcpServer::OnClientDisconnected(TcpClientConnection* client) {
    if (!client || isDestroying_) return;  // 如果正在析构，忽略回调
    
    std::lock_guard<std::mutex> lock(clientsMutex_);
    
    // 安全查找并移除客户端，避免访问已删除对象
    auto it = std::find(clients_.begin(), clients_.end(), client);
    if (it != clients_.end()) {
        clients_.erase(it);
        std::cout << "DEBUG: Client " << client << " removed from server list" << std::endl;
    } else {
        std::cout << "DEBUG: Client " << client << " not found in server list (already removed)" << std::endl;
    }
}

void TcpServer::CleanupInvalidClients() {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    auto it = clients_.begin();
    while (it != clients_.end()) {
        if (*it && (*it)->IsConnected()) {
            ++it;
        } else {
            // 修复：先删除对象，再移除指针
            if (*it) {
                (*it)->Disconnect(); // 确保Socket关闭
                delete *it;          // 释放内存
            }
            it = clients_.erase(it);
        }
    }
}

bool TcpServer::Listen(int port, int backlog) {
    if (isListening_) {
        setError("已经在监听", 0);
        return false;
    }
    
    if (!createSocket()) {
        return false;
    }
    
    if (!setSocketOptions()) {
        closesocket(listenSocket_);
        listenSocket_ = INVALID_SOCKET;
        return false;
    }
    
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);
    
    if (bind(listenSocket_, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        setError("绑定失败", WSAGetLastError());
        closesocket(listenSocket_);
        listenSocket_ = INVALID_SOCKET;
        return false;
    }
    
    if (listen(listenSocket_, backlog) == SOCKET_ERROR) {
        setError("监听失败", WSAGetLastError());
        closesocket(listenSocket_);
        listenSocket_ = INVALID_SOCKET;
        return false;
    }
    
    isListening_ = true;
    isRunning_ = true;
    listenPort_ = port;
    listenAddr_ = "0.0.0.0";
    
    return true;
}

bool TcpServer::StopListening() {
    if (listenSocket_ != INVALID_SOCKET) {
        closesocket(listenSocket_);
        listenSocket_ = INVALID_SOCKET;
    }
    
    isListening_ = false;
    isRunning_ = false;
    return true;
}

bool TcpServer::IsListening() const {
    return isListening_;
}

bool TcpServer::IsRunning() const {
    return isRunning_;
}

ITcpClient* TcpServer::Accept() {
    if (!isListening_ || listenSocket_ == INVALID_SOCKET) {
        setError("未在监听", 0);
        return nullptr;
    }
    
    sockaddr_in clientAddr;
    int addrLen = sizeof(clientAddr);
    
    SOCKET clientSocket = accept(listenSocket_, (sockaddr*)&clientAddr, &addrLen);
    if (clientSocket == INVALID_SOCKET) {
        setError("接受连接失败", WSAGetLastError());
        return nullptr;
    }
    
    // 检查连接数限制
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        if (static_cast<int>(clients_.size()) >= maxConnections_) {
            closesocket(clientSocket);
            setError("连接数已达上限", 0);
            return nullptr;
        }
    }
    
    auto* client = new TcpClientConnection(clientSocket, this);
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        clients_.push_back(client);
    }
    
    return client;
}

ITcpClient* TcpServer::GetClient(int index) {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    if (index >= 0 && index < static_cast<int>(clients_.size())) {
        // 检查客户端是否仍然有效且已连接
        auto* client = clients_[index];
        if (client && client->IsConnected()) {
            return client;
        }
    }
    return nullptr;
}

bool TcpServer::DisconnectClient(ITcpClient* client) {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    auto it = std::find(clients_.begin(), clients_.end(), 
                      static_cast<TcpClientConnection*>(client));
    if (it != clients_.end()) {
        (*it)->Disconnect();
        delete *it;
        clients_.erase(it);
        return true;
    }
    return false;
}

bool TcpServer::DisconnectAllClients() {
    fprintf(stderr, "[TcpServer] DEBUG: DisconnectAllClients called\n");
    std::lock_guard<std::mutex> lock(clientsMutex_);
    for (auto* client : clients_) {
        if (client) {
            client->Disconnect();
            delete client;
        }
    }
    clients_.clear();
    fprintf(stderr, "[TcpServer] DEBUG: DisconnectAllClients completed\n");
    return true;
}

const char* TcpServer::GetListenAddress() const {
    return listenAddr_.c_str();
}

int TcpServer::GetListenPort() const {
    return listenPort_;
}

int TcpServer::GetMaxConnections() const {
    return maxConnections_;
}

void TcpServer::SetMaxConnections(int max) {
    maxConnections_ = max;
}

int TcpServer::GetConnectedCount() const {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    
    int count = 0;
    for (auto* client : clients_) {
        if (client && client->IsConnected()) {
            count++;
        }
    }
    return count;
}

void TcpServer::SetTimeout(int milliseconds) {
    timeout_ = milliseconds;
    if (listenSocket_ != INVALID_SOCKET) {
        setSocketOptions();
    }
}

int TcpServer::GetTimeout() const {
    return timeout_;
}

void TcpServer::SetReuseAddress(bool enable) {
    reuseAddress_ = enable;
    if (listenSocket_ != INVALID_SOCKET) {
        setSocketOptions();
    }
}

bool TcpServer::IsReuseAddressEnabled() const {
    return reuseAddress_;
}

const char* TcpServer::GetLastError() const {
    return lastError_.c_str();
}

int TcpServer::GetErrorCode() const {
    return errorCode_;
}

