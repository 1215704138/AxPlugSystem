#pragma once

#include "AxPlug/AxEventBus.h"
#include "AxPlug/AxProfiler.h"
#include <vector>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <queue>
#include <chrono>
#include <cstdio>

// DefaultEventBus - COW + Lazy GC + MPSC async queue implementation
class DefaultEventBus : public AxPlug::IEventBus
{
public:
    DefaultEventBus();
    ~DefaultEventBus() override;

    // IEventBus interface
    void Publish(uint64_t eventId, std::shared_ptr<AxPlug::AxEvent> payload, AxPlug::DispatchMode mode = AxPlug::DispatchMode::DirectCall) override;
    AxPlug::EventConnectionPtr Subscribe(uint64_t eventId, AxPlug::EventCallback callback, void* specificSender = nullptr) override;

    // Shutdown the async event loop
    void Shutdown();

private:
    // Internal subscriber record
    struct Subscriber
    {
        std::weak_ptr<AxPlug::EventConnection> connection;
        AxPlug::EventCallback callback;
        void* specificSender;
    };

    // COW subscriber list per event ID
    using SubscriberList = std::shared_ptr<std::vector<Subscriber>>;

    // Get a COW snapshot of subscribers for an eventId (lock-free read after copy)
    SubscriberList GetSnapshot(uint64_t eventId);

    // Dispatch to subscribers synchronously on current thread
    void DispatchDirect(uint64_t eventId, std::shared_ptr<AxPlug::AxEvent> payload);

    // Lazy GC: purge expired connections from a subscriber list
    void PurgeExpired(uint64_t eventId);

    // MPSC async event loop thread function
    void EventLoopThread();

    // --- Data members ---

    // COW registry: eventId -> shared_ptr<vector<Subscriber>>
    std::unordered_map<uint64_t, SubscriberList> subscriberMap_;
    std::mutex subscriberMutex_;

    // MPSC queue for DispatchMode::Queued
    struct QueuedEvent
    {
        uint64_t eventId;
        std::shared_ptr<AxPlug::AxEvent> payload;
        std::chrono::steady_clock::time_point enqueueTime;
    };

    std::queue<QueuedEvent> asyncQueue_;
    std::mutex queueMutex_;
    std::condition_variable queueCV_;
    std::thread eventLoopThread_;
    std::atomic<bool> running_{ false };

    // GC counter: triggers purge every N publishes
    std::atomic<uint32_t> publishCount_{ 0 };
    static constexpr uint32_t GC_INTERVAL = 64;

    // Phase 3: Callback timeout WARNING threshold (microseconds)
    static constexpr int64_t CALLBACK_WARN_THRESHOLD_US = 16000; // 16ms
};
