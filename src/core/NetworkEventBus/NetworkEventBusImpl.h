#pragma once

#include "core/INetworkEventBus.h"
#include "AxPlug/AxEventBus.h"
#include "AxPlug/WinsockInit.hpp"
#include <unordered_map>
#include <mutex>
#include <thread>
#include <atomic>
#include <vector>
#include <string>
#include <cstdint>

// NetworkEventBusImpl - Proxy pattern implementation that wraps the local DefaultEventBus
// and adds UDP multicast network transport for INetworkableEvent subtypes.
//
// Architecture:
//   - Implements INetworkEventBus (IAxObject) for plugin lifecycle
//   - Contains an inner EventBusProxy (IEventBus) for transparent bus replacement
//   - On Publish: local dispatch via wrapped bus + network broadcast if INetworkableEvent
//   - Receiver thread: listens for UDP multicast, deserializes, re-publishes locally
//
// Wire protocol (little-endian):
//   [8 bytes eventId] [8 bytes nodeId] [4 bytes payloadLen] [payloadLen bytes data]

class NetworkEventBusImpl;

// Inner IEventBus proxy that delegates to NetworkEventBusImpl
class EventBusProxy : public AxPlug::IEventBus
{
public:
    explicit EventBusProxy(NetworkEventBusImpl* owner) : owner_(owner) {}
    ~EventBusProxy() override = default;

    void Publish(uint64_t eventId, std::shared_ptr<AxPlug::AxEvent> payload, AxPlug::DispatchMode mode = AxPlug::DispatchMode::DirectCall) override;
    AxPlug::EventConnectionPtr Subscribe(uint64_t eventId, AxPlug::EventCallback callback, void* specificSender = nullptr) override;
    void SetExceptionHandler(AxPlug::ExceptionHandler handler) override;

private:
    NetworkEventBusImpl* owner_;
};

class NetworkEventBusImpl : public AxPlug::INetworkEventBus
{
    friend class EventBusProxy;
public:
    NetworkEventBusImpl();
    ~NetworkEventBusImpl() override;

    // IAxObject lifecycle
    void OnInit() override;
    void OnShutdown() override;

    // INetworkEventBus interface
    bool StartNetwork(const char* multicastGroup, int port) override;
    void StopNetwork() override;
    bool IsNetworkActive() const override;
    void RegisterNetworkableEvent(uint64_t eventId, AxPlug::NetworkEventFactory factory) override;
    AxPlug::IEventBus* AsEventBus() override;
    uint64_t GetNodeId() const override;

protected:
    void Destroy() override { delete this; }

private:
    // Called by EventBusProxy
    void ProxyPublish(uint64_t eventId, std::shared_ptr<AxPlug::AxEvent> payload, AxPlug::DispatchMode mode);
    AxPlug::EventConnectionPtr ProxySubscribe(uint64_t eventId, AxPlug::EventCallback callback, void* specificSender);

    // Network send: serialize INetworkableEvent and broadcast via UDP multicast
    void BroadcastToNetwork(uint64_t eventId, const std::shared_ptr<AxPlug::INetworkableEvent>& evt);

    // Network receiver thread
    void ReceiverThread();

    // Generate a 64-bit node ID for this process
    static uint64_t GenerateNodeId();

    // --- Data members ---

    // The original local event bus (saved before takeover)
    AxPlug::IEventBus* localBus_ = nullptr;

    // The proxy IEventBus that replaces the default bus
    std::unique_ptr<EventBusProxy> proxy_;

    // Network event factory registry: eventId -> factory
    std::unordered_map<uint64_t, AxPlug::NetworkEventFactory> factoryRegistry_;
    std::mutex factoryMutex_;

    // UDP multicast state
    std::string multicastGroup_;
    int multicastPort_ = 0;
    uint64_t nodeId_ = 0;

#ifdef _WIN32
    uintptr_t sendSocket_ = ~static_cast<uintptr_t>(0);
    uintptr_t recvSocket_ = ~static_cast<uintptr_t>(0);
#else
    int sendSocket_ = -1;
    int recvSocket_ = -1;
#endif

    std::thread receiverThread_;
    std::atomic<bool> networkRunning_{ false };

    // Anti-storm rate limiting: max broadcasts per event ID per window
    struct RateLimit
    {
        std::chrono::steady_clock::time_point windowStart;
        uint32_t count = 0;
    };
    std::unordered_map<uint64_t, RateLimit> rateLimitMap_;
    std::mutex rateLimitMutex_;
    static constexpr uint32_t RATE_LIMIT_MAX = 100;       // max packets per window per eventId
    static constexpr int64_t  RATE_LIMIT_WINDOW_MS = 1000; // 1 second window

    bool CheckRateLimit(uint64_t eventId);

    // Wire protocol header size
    static constexpr size_t HEADER_SIZE = 8 + 8 + 4; // eventId + nodeId + payloadLen
    static constexpr size_t MAX_PACKET_SIZE = 65000;  // UDP practical limit
};
