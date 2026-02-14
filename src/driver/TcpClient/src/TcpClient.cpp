#include "../include/TcpClient.h"
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

TcpClient::TcpClient() 
    : socket_(INVALID_SOCKET)
    , isConnected_(false)
    , isConnecting_(false)
    , timeout_(5000)
    , bufferSize_(4096)
    , keepAlive_(false)
    , localPort_(0)
    , remotePort_(0)
    , errorCode_(0) {
}

TcpClient::~TcpClient() {
    Disconnect();
}

bool TcpClient::createSocket() {
    socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_ == INVALID_SOCKET) {
        setError("创建socket失败", WSAGetLastError());
        return false;
    }
    return true;
}

bool TcpClient::setSocketOptions() {
    // 设置超时
    if (timeout_ > 0) {
        setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout_, sizeof(timeout_));
        setsockopt(socket_, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout_, sizeof(timeout_));
    }
    
    // 设置缓冲区大小
    if (bufferSize_ > 0) {
        setsockopt(socket_, SOL_SOCKET, SO_RCVBUF, (char*)&bufferSize_, sizeof(bufferSize_));
        setsockopt(socket_, SOL_SOCKET, SO_SNDBUF, (char*)&bufferSize_, sizeof(bufferSize_));
    }
    
    // 设置KeepAlive
    if (keepAlive_) {
        BOOL opt = TRUE;
        setsockopt(socket_, SOL_SOCKET, SO_KEEPALIVE, (char*)&opt, sizeof(opt));
    }
    
    return true;
}

void TcpClient::setError(const std::string& error, int code) const {
    lastError_ = error;
    errorCode_ = code;
}

std::string TcpClient::getAddressFromSockaddr(const sockaddr_in& addr) const {
    char str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(addr.sin_addr), str, INET_ADDRSTRLEN);
    return std::string(str);
}

int TcpClient::getPortFromSockaddr(const sockaddr_in& addr) const {
    return ntohs(addr.sin_port);
}

bool TcpClient::Connect(const char* host, int port) {
    if (isConnected_ || isConnecting_) {
        setError("已经连接或正在连接", 0);
        return false;
    }
    
    if (!createSocket()) {
        return false;
    }
    
    if (!setSocketOptions()) {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
        return false;
    }
    
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    inet_pton(AF_INET, host, &serverAddr.sin_addr);
    
    isConnecting_ = true;
    
    if (connect(socket_, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        int error = WSAGetLastError();
        if (error == WSAEWOULDBLOCK) {
            // 非阻塞模式下的连接正在进行
            return true;
        }
        
        setError("连接失败", error);
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
        isConnecting_ = false;
        return false;
    }
    
    isConnected_ = true;
    isConnecting_ = false;
    remoteAddr_ = host;
    remotePort_ = port;
    
    // 获取本地地址信息
    sockaddr_in localAddr;
    int addrLen = sizeof(localAddr);
    getsockname(socket_, (sockaddr*)&localAddr, &addrLen);
    localAddr_ = getAddressFromSockaddr(localAddr);
    localPort_ = getPortFromSockaddr(localAddr);
    
    return true;
}

bool TcpClient::Disconnect() {
    if (socket_ != INVALID_SOCKET) {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }
    
    isConnected_ = false;
    isConnecting_ = false;
    return true;
}

bool TcpClient::IsConnected() const {
    return isConnected_;
}

bool TcpClient::IsConnecting() const {
    return isConnecting_;
}

bool TcpClient::Send(const uint8_t* data, size_t len) {
    if (!isConnected_ || socket_ == INVALID_SOCKET) {
        setError("未连接", 0);
        return false;
    }
    
    int totalSent = 0;
    int dataSize = static_cast<int>(len);
    
    while (totalSent < dataSize) {
        int sent = send(socket_, (char*)(data + totalSent), dataSize - totalSent, 0);
        if (sent == SOCKET_ERROR) {
            setError("发送失败", WSAGetLastError());
            return false;
        }
        totalSent += sent;
    }
    
    return true;
}

bool TcpClient::SendString(const char* data) {
    if (!data) return false;
    size_t len = strlen(data);
    return Send(reinterpret_cast<const uint8_t*>(data), len);
}

bool TcpClient::Receive(uint8_t* buffer, size_t maxLen, size_t& outLen) {
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

bool TcpClient::ReceiveString(char* buffer, size_t maxLen, size_t& outLen) {
    return Receive(reinterpret_cast<uint8_t*>(buffer), maxLen, outLen);
}

const char* TcpClient::GetLocalAddress() const {
    return localAddr_.c_str();
}

const char* TcpClient::GetRemoteAddress() const {
    return remoteAddr_.c_str();
}

int TcpClient::GetLocalPort() const {
    return localPort_;
}

int TcpClient::GetRemotePort() const {
    return remotePort_;
}

void TcpClient::SetTimeout(int milliseconds) {
    timeout_ = milliseconds;
    if (socket_ != INVALID_SOCKET) {
        setSocketOptions();
    }
}

int TcpClient::GetTimeout() const {
    return timeout_;
}

void TcpClient::SetBufferSize(int size) {
    bufferSize_ = size;
    if (socket_ != INVALID_SOCKET) {
        setSocketOptions();
    }
}

int TcpClient::GetBufferSize() const {
    return bufferSize_;
}

void TcpClient::SetKeepAlive(bool enable) {
    keepAlive_ = enable;
    if (socket_ != INVALID_SOCKET) {
        setSocketOptions();
    }
}

bool TcpClient::IsKeepAliveEnabled() const {
    return keepAlive_;
}

const char* TcpClient::GetLastError() const {
    return lastError_.c_str();
}

int TcpClient::GetErrorCode() const {
    return errorCode_;
}

