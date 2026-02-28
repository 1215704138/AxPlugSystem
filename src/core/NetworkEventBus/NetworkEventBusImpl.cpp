#include "NetworkEventBusImpl.h"
#include <cstring>
#include <cstdio>
#include <chrono>
#include <random>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

// ============================================================
// Helper: little-endian encode/decode
// ============================================================
static void WriteU64LE(uint8_t* buf, uint64_t val)
{
    for (int i = 0; i < 8; ++i)
        buf[i] = static_cast<uint8_t>((val >> (i * 8)) & 0xFF);
}

static uint64_t ReadU64LE(const uint8_t* buf)
{
    uint64_t val = 0;
    for (int i = 0; i < 8; ++i)
        val |= static_cast<uint64_t>(buf[i]) << (i * 8);
    return val;
}

static void WriteU32LE(uint8_t* buf, uint32_t val)
{
    for (int i = 0; i < 4; ++i)
        buf[i] = static_cast<uint8_t>((val >> (i * 8)) & 0xFF);
}

static uint32_t ReadU32LE(const uint8_t* buf)
{
    uint32_t val = 0;
    for (int i = 0; i < 4; ++i)
        val |= static_cast<uint32_t>(buf[i]) << (i * 8);
    return val;
}

// ============================================================
// EventBusProxy - delegates to NetworkEventBusImpl
// ============================================================
void EventBusProxy::Publish(uint64_t eventId, std::shared_ptr<AxPlug::AxEvent> payload, AxPlug::DispatchMode mode)
{
    owner_->ProxyPublish(eventId, std::move(payload), mode);
}

AxPlug::EventConnectionPtr EventBusProxy::Subscribe(uint64_t eventId, AxPlug::EventCallback callback, void* specificSender)
{
    return owner_->ProxySubscribe(eventId, std::move(callback), specificSender);
}

void EventBusProxy::SetExceptionHandler(AxPlug::ExceptionHandler handler)
{
    if (owner_->localBus_) owner_->localBus_->SetExceptionHandler(std::move(handler));
}

// ============================================================
// NetworkEventBusImpl
// ============================================================
NetworkEventBusImpl::NetworkEventBusImpl()
{
    nodeId_ = GenerateNodeId();
    proxy_ = std::make_unique<EventBusProxy>(this);
}

NetworkEventBusImpl::~NetworkEventBusImpl()
{
    StopNetwork();
}

void NetworkEventBusImpl::OnInit()
{
    // Save the current local bus and perform takeover ("夺舍")
    localBus_ = Ax_GetEventBus();
    Ax_SetEventBus(proxy_.get());
}

void NetworkEventBusImpl::OnShutdown()
{
    StopNetwork();
    // Restore original local bus (critical: nullptr would kill all event routing)
    if (localBus_)
    {
        Ax_SetEventBus(localBus_);
        localBus_ = nullptr;
    }
}

// ============================================================
// INetworkEventBus interface
// ============================================================
bool NetworkEventBusImpl::StartNetwork(const char* multicastGroup, int port)
{
    if (networkRunning_.load(std::memory_order_acquire))
        return true;

    if (!multicastGroup || port <= 0)
        return false;

    multicastGroup_ = multicastGroup;
    multicastPort_ = port;

#ifdef _WIN32
    // Ensure Winsock is initialized
    AxPlug::GetWinsockInit();

    // Create send socket (UDP)
    sendSocket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sendSocket_ == INVALID_SOCKET)
    {
        fprintf(stderr, "[NetworkEventBus] Failed to create send socket\n");
        return false;
    }

    // Set multicast TTL
    int ttl = 32;
    setsockopt(static_cast<SOCKET>(sendSocket_), IPPROTO_IP, IP_MULTICAST_TTL, reinterpret_cast<const char*>(&ttl), sizeof(ttl));

    // Enable loopback so local machine also receives
    int loopback = 1;
    setsockopt(static_cast<SOCKET>(sendSocket_), IPPROTO_IP, IP_MULTICAST_LOOP, reinterpret_cast<const char*>(&loopback), sizeof(loopback));

    // Create receive socket (UDP)
    recvSocket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (recvSocket_ == INVALID_SOCKET)
    {
        closesocket(static_cast<SOCKET>(sendSocket_));
        sendSocket_ = INVALID_SOCKET;
        fprintf(stderr, "[NetworkEventBus] Failed to create recv socket\n");
        return false;
    }

    // Allow address reuse
    int reuse = 1;
    setsockopt(static_cast<SOCKET>(recvSocket_), SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    // Bind receive socket
    sockaddr_in bindAddr{};
    bindAddr.sin_family = AF_INET;
    bindAddr.sin_port = htons(static_cast<u_short>(port));
    bindAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(static_cast<SOCKET>(recvSocket_), reinterpret_cast<sockaddr*>(&bindAddr), sizeof(bindAddr)) == SOCKET_ERROR)
    {
        closesocket(static_cast<SOCKET>(sendSocket_));
        closesocket(static_cast<SOCKET>(recvSocket_));
        sendSocket_ = INVALID_SOCKET;
        recvSocket_ = INVALID_SOCKET;
        fprintf(stderr, "[NetworkEventBus] Failed to bind recv socket on port %d\n", port);
        return false;
    }

    // Join multicast group
    ip_mreq mreq{};
    inet_pton(AF_INET, multicastGroup, &mreq.imr_multiaddr);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    if (setsockopt(static_cast<SOCKET>(recvSocket_), IPPROTO_IP, IP_ADD_MEMBERSHIP, reinterpret_cast<const char*>(&mreq), sizeof(mreq)) == SOCKET_ERROR)
    {
        closesocket(static_cast<SOCKET>(sendSocket_));
        closesocket(static_cast<SOCKET>(recvSocket_));
        sendSocket_ = INVALID_SOCKET;
        recvSocket_ = INVALID_SOCKET;
        fprintf(stderr, "[NetworkEventBus] Failed to join multicast group %s\n", multicastGroup);
        return false;
    }

    // Set recv timeout so thread can check running_ flag periodically
    DWORD timeout = 500; // 500ms
    setsockopt(static_cast<SOCKET>(recvSocket_), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
    // POSIX implementation (Linux/macOS)
    sendSocket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sendSocket_ < 0) return false;

    int ttl = 32;
    setsockopt(sendSocket_, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
    int loopback = 1;
    setsockopt(sendSocket_, IPPROTO_IP, IP_MULTICAST_LOOP, &loopback, sizeof(loopback));

    recvSocket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (recvSocket_ < 0) { close(sendSocket_); sendSocket_ = -1; return false; }

    int reuse = 1;
    setsockopt(recvSocket_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in bindAddr{};
    bindAddr.sin_family = AF_INET;
    bindAddr.sin_port = htons(static_cast<uint16_t>(port));
    bindAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (::bind(recvSocket_, reinterpret_cast<sockaddr*>(&bindAddr), sizeof(bindAddr)) < 0)
    {
        close(sendSocket_); close(recvSocket_);
        sendSocket_ = -1; recvSocket_ = -1;
        return false;
    }

    ip_mreq mreq{};
    inet_pton(AF_INET, multicastGroup, &mreq.imr_multiaddr);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    if (setsockopt(recvSocket_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0)
    {
        close(sendSocket_); close(recvSocket_);
        sendSocket_ = -1; recvSocket_ = -1;
        return false;
    }

    struct timeval tv;
    tv.tv_sec = 0; tv.tv_usec = 500000;
    setsockopt(recvSocket_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

    networkRunning_.store(true, std::memory_order_release);
    receiverThread_ = std::thread(&NetworkEventBusImpl::ReceiverThread, this);

    fprintf(stderr, "[NetworkEventBus] Started on %s:%d (nodeId=0x%llx)\n", multicastGroup, port, static_cast<unsigned long long>(nodeId_));
    return true;
}

void NetworkEventBusImpl::StopNetwork()
{
    bool expected = true;
    if (!networkRunning_.compare_exchange_strong(expected, false, std::memory_order_acq_rel))
        return;

    if (receiverThread_.joinable())
        receiverThread_.join();

#ifdef _WIN32
    if (sendSocket_ != INVALID_SOCKET) { closesocket(static_cast<SOCKET>(sendSocket_)); sendSocket_ = INVALID_SOCKET; }
    if (recvSocket_ != INVALID_SOCKET) { closesocket(static_cast<SOCKET>(recvSocket_)); recvSocket_ = INVALID_SOCKET; }
#else
    if (sendSocket_ >= 0) { close(sendSocket_); sendSocket_ = -1; }
    if (recvSocket_ >= 0) { close(recvSocket_); recvSocket_ = -1; }
#endif
}

bool NetworkEventBusImpl::IsNetworkActive() const
{
    return networkRunning_.load(std::memory_order_acquire);
}

void NetworkEventBusImpl::RegisterNetworkableEvent(uint64_t eventId, AxPlug::NetworkEventFactory factory)
{
    std::lock_guard<std::mutex> lock(factoryMutex_);
    factoryRegistry_[eventId] = std::move(factory);
}

AxPlug::IEventBus* NetworkEventBusImpl::AsEventBus()
{
    return proxy_.get();
}

uint64_t NetworkEventBusImpl::GetNodeId() const
{
    return nodeId_;
}

// ============================================================
// Proxy Publish: local dispatch + network broadcast
// ============================================================
void NetworkEventBusImpl::ProxyPublish(uint64_t eventId, std::shared_ptr<AxPlug::AxEvent> payload, AxPlug::DispatchMode mode)
{
    // Step 1: Always dispatch locally first via the original bus
    if (localBus_)
    {
        localBus_->Publish(eventId, payload, mode);
    }

    // Step 2: Anti-storm filter — only INetworkableEvent subtypes go over network + rate limiting
    if (networkRunning_.load(std::memory_order_acquire))
    {
        auto netEvent = std::dynamic_pointer_cast<AxPlug::INetworkableEvent>(payload);
        if (netEvent && CheckRateLimit(eventId))
        {
            BroadcastToNetwork(eventId, netEvent);
        }
    }
}

AxPlug::EventConnectionPtr NetworkEventBusImpl::ProxySubscribe(uint64_t eventId, AxPlug::EventCallback callback, void* specificSender)
{
    // Subscribe always goes to the local bus
    if (localBus_)
    {
        return localBus_->Subscribe(eventId, std::move(callback), specificSender);
    }
    return nullptr;
}

// ============================================================
// CheckRateLimit - anti-storm: throttle per eventId per time window
// ============================================================
bool NetworkEventBusImpl::CheckRateLimit(uint64_t eventId)
{
    auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(rateLimitMutex_);
    auto& rl = rateLimitMap_[eventId];
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - rl.windowStart).count();
    if (elapsed >= RATE_LIMIT_WINDOW_MS)
    {
        rl.windowStart = now;
        rl.count = 1;
        return true;
    }
    if (rl.count >= RATE_LIMIT_MAX)
    {
        return false; // throttled
    }
    ++rl.count;
    return true;
}

// ============================================================
// BroadcastToNetwork - serialize and send via UDP multicast
// ============================================================
void NetworkEventBusImpl::BroadcastToNetwork(uint64_t eventId, const std::shared_ptr<AxPlug::INetworkableEvent>& evt)
{
    std::string serialized = evt->Serialize();
    size_t totalSize = HEADER_SIZE + serialized.size();

    if (totalSize > MAX_PACKET_SIZE)
    {
        fprintf(stderr, "[NetworkEventBus] Event 0x%llx payload too large (%zu bytes), skipping\n", static_cast<unsigned long long>(eventId), serialized.size());
        return;
    }

    std::vector<uint8_t> packet(totalSize);
    WriteU64LE(packet.data(), eventId);
    WriteU64LE(packet.data() + 8, nodeId_);
    WriteU32LE(packet.data() + 16, static_cast<uint32_t>(serialized.size()));
    if (!serialized.empty())
    {
        memcpy(packet.data() + HEADER_SIZE, serialized.data(), serialized.size());
    }

    sockaddr_in destAddr{};
    destAddr.sin_family = AF_INET;
    destAddr.sin_port = htons(static_cast<uint16_t>(multicastPort_));
    inet_pton(AF_INET, multicastGroup_.c_str(), &destAddr.sin_addr);

#ifdef _WIN32
    sendto(static_cast<SOCKET>(sendSocket_), reinterpret_cast<const char*>(packet.data()), static_cast<int>(totalSize), 0, reinterpret_cast<sockaddr*>(&destAddr), sizeof(destAddr));
#else
    sendto(sendSocket_, packet.data(), totalSize, 0, reinterpret_cast<sockaddr*>(&destAddr), sizeof(destAddr));
#endif
}

// ============================================================
// ReceiverThread - listens for UDP multicast and re-publishes locally
// ============================================================
void NetworkEventBusImpl::ReceiverThread()
{
    std::vector<uint8_t> buffer(MAX_PACKET_SIZE);

    while (networkRunning_.load(std::memory_order_acquire))
    {
        sockaddr_in srcAddr{};
        int addrLen = sizeof(srcAddr);

#ifdef _WIN32
        int received = recvfrom(static_cast<SOCKET>(recvSocket_), reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()), 0, reinterpret_cast<sockaddr*>(&srcAddr), &addrLen);
#else
        socklen_t sAddrLen = sizeof(srcAddr);
        ssize_t received = recvfrom(recvSocket_, buffer.data(), buffer.size(), 0, reinterpret_cast<sockaddr*>(&srcAddr), &sAddrLen);
#endif

        if (received < static_cast<int>(HEADER_SIZE))
            continue; // Timeout or too-small packet

        uint64_t eventId = ReadU64LE(buffer.data());
        uint64_t senderNodeId = ReadU64LE(buffer.data() + 8);
        uint32_t payloadLen = ReadU32LE(buffer.data() + 16);

        // Skip packets from ourselves (loopback prevention)
        if (senderNodeId == nodeId_)
            continue;

        if (HEADER_SIZE + payloadLen > static_cast<size_t>(received))
            continue; // Truncated packet

        // Look up factory for this eventId
        AxPlug::NetworkEventFactory factory;
        {
            std::lock_guard<std::mutex> lock(factoryMutex_);
            auto it = factoryRegistry_.find(eventId);
            if (it == factoryRegistry_.end())
                continue; // Unknown event type, skip
            factory = it->second;
        }

        // Deserialize
        auto evt = factory();
        if (!evt)
            continue;

        std::string data(reinterpret_cast<const char*>(buffer.data() + HEADER_SIZE), payloadLen);
        evt->Deserialize(data);

        // Re-publish locally (NOT through proxy to avoid re-broadcasting)
        if (localBus_)
        {
            localBus_->Publish(eventId, evt, AxPlug::DispatchMode::DirectCall);
        }
    }
}

// ============================================================
// GenerateNodeId - unique 64-bit ID for this process instance
// ============================================================
uint64_t NetworkEventBusImpl::GenerateNodeId()
{
    auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    std::random_device rd;
    uint64_t r = rd();
    // Mix time + random for uniqueness
    uint64_t id = static_cast<uint64_t>(now) ^ (r << 32) ^ (r >> 32);
    return id;
}
