#include "../include/UdpSocket.h"
#include <iostream>
#include <cstring>
#include <algorithm>

// Winsock 初始化 (复用现有的)
extern class WinsockInitializer {
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

UdpSocket::UdpSocket() 
    : socket_(INVALID_SOCKET)
    , isBound_(false)
    , timeout_(5000)
    , bufferSize_(4096)
    , ttl_(64)
    , broadcastEnabled_(false)
    , localPort_(0)
    , errorCode_(0) {
}

UdpSocket::~UdpSocket() {
    Unbind();
}

bool UdpSocket::createSocket() {
    socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_ == INVALID_SOCKET) {
        setError("创建socket失败", WSAGetLastError());
        return false;
    }
    return true;
}

bool UdpSocket::setSocketOptions() {
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
    
    // 设置TTL
    if (ttl_ > 0) {
        setsockopt(socket_, IPPROTO_IP, IP_TTL, (char*)&ttl_, sizeof(ttl_));
    }
    
    // 设置广播
    if (broadcastEnabled_) {
        bool broadcast = true;
        setsockopt(socket_, SOL_SOCKET, SO_BROADCAST, (char*)&broadcast, sizeof(broadcast));
    }
    
    return true;
}

void UdpSocket::setError(const std::string& error, int code) const {
    lastError_ = error;
    errorCode_ = code;
}

std::string UdpSocket::getAddressFromSockaddr(const sockaddr_in& addr) const {
    char str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(addr.sin_addr), str, INET_ADDRSTRLEN);
    return std::string(str);
}

int UdpSocket::getPortFromSockaddr(const sockaddr_in& addr) const {
    return ntohs(addr.sin_port);
}

bool UdpSocket::Bind(int port) {
    if (isBound_) {
        setError("已经绑定", 0);
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
    
    sockaddr_in localAddr;
    localAddr.sin_family = AF_INET;
    localAddr.sin_addr.s_addr = INADDR_ANY;
    localAddr.sin_port = htons(port);
    
    if (bind(socket_, (sockaddr*)&localAddr, sizeof(localAddr)) == SOCKET_ERROR) {
        setError("绑定失败", WSAGetLastError());
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
        return false;
    }
    
    isBound_ = true;
    localPort_ = port;
    localAddr_ = "0.0.0.0";
    
    return true;
}

bool UdpSocket::Unbind() {
    if (socket_ != INVALID_SOCKET) {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }
    
    isBound_ = false;
    return true;
}

bool UdpSocket::IsBound() const {
    return isBound_;
}

bool UdpSocket::Send(const uint8_t* data, size_t len) {
    if (socket_ == INVALID_SOCKET) {
        setError("socket未创建", 0);
        return false;
    }
    
    int sent = send(socket_, (char*)data, static_cast<int>(len), 0);
    if (sent == SOCKET_ERROR) {
        setError("发送失败", WSAGetLastError());
        return false;
    }
    
    return sent == static_cast<int>(len);
}

bool UdpSocket::SendString(const char* data) {
    if (!data) return false;
    size_t len = strlen(data);
    return Send(reinterpret_cast<const uint8_t*>(data), len);
}

bool UdpSocket::SendTo(const char* host, int port, const uint8_t* data, size_t len) {
    if (socket_ == INVALID_SOCKET) {
        setError("socket未创建", 0);
        return false;
    }
    
    sockaddr_in destAddr;
    destAddr.sin_family = AF_INET;
    destAddr.sin_port = htons(port);
    inet_pton(AF_INET, host, &destAddr.sin_addr);
    
    int sent = sendto(socket_, (char*)data, static_cast<int>(len), 0, 
                     (sockaddr*)&destAddr, sizeof(destAddr));
    
    if (sent == SOCKET_ERROR) {
        setError("发送失败", WSAGetLastError());
        return false;
    }
    
    return sent == static_cast<int>(len);
}

bool UdpSocket::SendStringTo(const char* host, int port, const char* data) {
    if (!data) return false;
    size_t len = strlen(data);
    return SendTo(host, port, reinterpret_cast<const uint8_t*>(data), len);
}

bool UdpSocket::Receive(uint8_t* buffer, size_t maxLen, size_t& outLen) {
    if (socket_ == INVALID_SOCKET) {
        setError("socket未创建", 0);
        outLen = 0;
        return false;
    }
    
    sockaddr_in fromAddr;
    int addrLen = sizeof(fromAddr);
    
    int received = recvfrom(socket_, (char*)buffer, static_cast<int>(maxLen), 0, (sockaddr*)&fromAddr, &addrLen);
    
    if (received == SOCKET_ERROR) {
        setError("接收失败", WSAGetLastError());
        outLen = 0;
        return false;
    }
    
    outLen = static_cast<size_t>(received);
    return true;
}

bool UdpSocket::ReceiveString(char* buffer, size_t maxLen, size_t& outLen) {
    return Receive(reinterpret_cast<uint8_t*>(buffer), maxLen, outLen);
}

bool UdpSocket::ReceiveFrom(char* host, int hostLen, int& port, uint8_t* buffer, size_t maxLen, size_t& outLen) {
    if (socket_ == INVALID_SOCKET) {
        setError("socket未创建", 0);
        outLen = 0;
        return false;
    }
    
    sockaddr_in fromAddr;
    int addrLen = sizeof(fromAddr);
    
    int received = recvfrom(socket_, (char*)buffer, static_cast<int>(maxLen), 0, (sockaddr*)&fromAddr, &addrLen);
    
    if (received == SOCKET_ERROR) {
        setError("接收失败", WSAGetLastError());
        outLen = 0;
        return false;
    }
    
    // Copy host address to output buffer
    std::string addrStr = getAddressFromSockaddr(fromAddr);
    strncpy_s(host, hostLen, addrStr.c_str(), _TRUNCATE);
    
    port = getPortFromSockaddr(fromAddr);
    outLen = static_cast<size_t>(received);
    return true;
}

bool UdpSocket::ReceiveStringFrom(char* host, int hostLen, int& port, char* buffer, size_t maxLen, size_t& outLen) {
    return ReceiveFrom(host, hostLen, port, reinterpret_cast<uint8_t*>(buffer), maxLen, outLen);
}

bool UdpSocket::EnableBroadcast(bool enable) {
    if (socket_ == INVALID_SOCKET) {
        setError("socket未创建", 0);
        return false;
    }
    
    broadcastEnabled_ = enable;
    bool broadcast = enable;
    if (setsockopt(socket_, SOL_SOCKET, SO_BROADCAST, (char*)&broadcast, sizeof(broadcast)) == SOCKET_ERROR) {
        setError("设置广播失败", WSAGetLastError());
        return false;
    }
    
    return true;
}

bool UdpSocket::IsBroadcastEnabled() const {
    return broadcastEnabled_;
}

bool UdpSocket::JoinMulticast(const char* group) {
    if (socket_ == INVALID_SOCKET) {
        setError("socket未创建", 0);
        return false;
    }
    
    ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr(group);
    mreq.imr_interface.s_addr = INADDR_ANY;
    
    if (setsockopt(socket_, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*)&mreq, sizeof(mreq)) == SOCKET_ERROR) {
        setError("加入多播组失败", WSAGetLastError());
        return false;
    }
    
    multicastGroups_.push_back(std::string(group));
    return true;
}

bool UdpSocket::LeaveMulticast(const char* group) {
    if (socket_ == INVALID_SOCKET) {
        setError("socket未创建", 0);
        return false;
    }
    
    ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr(group);
    mreq.imr_interface.s_addr = INADDR_ANY;
    
    if (setsockopt(socket_, IPPROTO_IP, IP_DROP_MEMBERSHIP, (char*)&mreq, sizeof(mreq)) == SOCKET_ERROR) {
        setError("离开多播组失败", WSAGetLastError());
        return false;
    }
    
    auto it = std::find(multicastGroups_.begin(), multicastGroups_.end(), std::string(group));
    if (it != multicastGroups_.end()) {
        multicastGroups_.erase(it);
    }
    
    return true;
}

bool UdpSocket::GetMulticastGroups(char* groups, int maxLen, int& count) const {
    if (!groups || maxLen <= 0) {
        count = 0;
        return false;
    }
    
    count = 0;
    int offset = 0;
    
    for (const auto& group : multicastGroups_) {
        int groupLen = static_cast<int>(group.length()) + 1; // +1 for null terminator
        if (offset + groupLen > maxLen) {
            break; // Buffer full
        }
        
        strcpy_s(groups + offset, maxLen - offset, group.c_str());
        offset += groupLen;
        count++;
    }
    
    return true;
}

const char* UdpSocket::GetLocalAddress() const {
    return localAddr_.c_str();
}

int UdpSocket::GetLocalPort() const {
    return localPort_;
}

void UdpSocket::SetTimeout(int milliseconds) {
    timeout_ = milliseconds;
    if (socket_ != INVALID_SOCKET) {
        setSocketOptions();
    }
}

int UdpSocket::GetTimeout() const {
    return timeout_;
}

void UdpSocket::SetBufferSize(int size) {
    bufferSize_ = size;
    if (socket_ != INVALID_SOCKET) {
        setSocketOptions();
    }
}

int UdpSocket::GetBufferSize() const {
    return bufferSize_;
}

void UdpSocket::SetTTL(int ttl) {
    ttl_ = ttl;
    if (socket_ != INVALID_SOCKET) {
        setSocketOptions();
    }
}

int UdpSocket::GetTTL() const {
    return ttl_;
}

const char* UdpSocket::GetLastError() const {
    return lastError_.c_str();
}

int UdpSocket::GetErrorCode() const {
    return errorCode_;
}

