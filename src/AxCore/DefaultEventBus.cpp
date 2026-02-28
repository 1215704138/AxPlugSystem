#include "DefaultEventBus.h"
#include <algorithm>
#include <cassert>

DefaultEventBus::DefaultEventBus()
{
    running_.store(true, std::memory_order_release);
    eventLoopThread_ = std::thread(&DefaultEventBus::EventLoopThread, this);
}

void DefaultEventBus::SetExceptionHandler(AxPlug::ExceptionHandler handler)
{
    std::lock_guard<std::mutex> lock(exceptionHandlerMutex_);
    if (handler) {
        exceptionHandler_ = std::make_shared<AxPlug::ExceptionHandler>(std::move(handler));
    } else {
        exceptionHandler_.reset();
    }
}

void DefaultEventBus::ReportException(const std::exception& e)
{
    std::shared_ptr<AxPlug::ExceptionHandler> handlerCopy;
    {
        std::lock_guard<std::mutex> lock(exceptionHandlerMutex_);
        handlerCopy = exceptionHandler_;
    }
    if (handlerCopy) {
        try { (*handlerCopy)(e); } catch (...) { fprintf(stderr, "[EventBus CRITICAL] Exception handler itself threw.\n"); }
    } else {
        fprintf(stderr, "[EventBus] Unhandled callback exception: %s\n", e.what());
    }
}

void DefaultEventBus::ReportUnknownException()
{
    static std::runtime_error unknownErr("[EventBus] Unknown non-std::exception caught in callback.");
    ReportException(unknownErr);
}

DefaultEventBus::~DefaultEventBus()
{
    Shutdown();
}

void DefaultEventBus::Shutdown()
{
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false, std::memory_order_acq_rel))
        return;

    queueCV_.notify_all();
    if (eventLoopThread_.joinable())
        eventLoopThread_.join();
}

// ============================================================
// Publish
// ============================================================
void DefaultEventBus::Publish(uint64_t eventId, std::shared_ptr<AxPlug::AxEvent> payload, AxPlug::DispatchMode mode)
{
    AX_PROFILE_SCOPE("EventBus::Publish");
    if (mode == AxPlug::DispatchMode::DirectCall)
    {
        DispatchDirect(eventId, std::move(payload));
    }
    else
    {
        // Queued: push into MPSC queue with enqueue timestamp for latency tracking
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            asyncQueue_.push({ eventId, std::move(payload), std::chrono::steady_clock::now() });
        }
        queueCV_.notify_one();
    }
}

// ============================================================
// Subscribe (COW write path)
// ============================================================
AxPlug::EventConnectionPtr DefaultEventBus::Subscribe(uint64_t eventId, AxPlug::EventCallback callback, void* specificSender)
{
    auto conn = std::make_shared<AxPlug::EventConnection>();

    Subscriber sub;
    sub.connection = conn;
    sub.callback = std::move(callback);
    sub.specificSender = specificSender;

    {
        std::lock_guard<std::mutex> lock(subscriberMutex_);
        auto it = subscriberMap_.find(eventId);
        if (it == subscriberMap_.end())
        {
            // First subscriber for this event: create new list
            auto newList = std::make_shared<std::vector<Subscriber>>();
            newList->push_back(std::move(sub));
            subscriberMap_[eventId] = std::move(newList);
        }
        else
        {
            // COW: deep clone existing list, append, then atomic replace
            auto newList = std::make_shared<std::vector<Subscriber>>(*it->second);
            newList->push_back(std::move(sub));
            it->second = std::move(newList);
        }
    }

    return conn;
}

// ============================================================
// GetSnapshot - COW read path (short lock, copy shared_ptr)
// ============================================================
DefaultEventBus::SubscriberList DefaultEventBus::GetSnapshot(uint64_t eventId)
{
    std::lock_guard<std::mutex> lock(subscriberMutex_);
    auto it = subscriberMap_.find(eventId);
    if (it == subscriberMap_.end())
        return nullptr;
    return it->second; // shared_ptr copy is atomic refcount bump
}

// ============================================================
// DispatchDirect - synchronous fan-out on caller's thread
// ============================================================
void DefaultEventBus::DispatchDirect(uint64_t eventId, std::shared_ptr<AxPlug::AxEvent> payload)
{
    AX_PROFILE_SCOPE("EventBus::DispatchDirect");
    SubscriberList snapshot = GetSnapshot(eventId);
    if (!snapshot || snapshot->empty())
        return;

    for (const auto& sub : *snapshot)
    {
        auto conn = sub.connection.lock();
        if (!conn || !conn->IsActive())
            continue;

        // Sender filter: if specificSender was set, only match that sender
        if (sub.specificSender != nullptr && sub.specificSender != payload->sender)
            continue;

        // Phase 3: Per-callback timing with WARNING on timeout
        // Exception isolation: catch callback throws to prevent crashing the bus
        auto cbStart = std::chrono::steady_clock::now();
        try {
            sub.callback(payload);
        } catch (const std::exception& e) {
            ReportException(e);
        } catch (...) {
            ReportUnknownException();
        }
        auto cbEnd = std::chrono::steady_clock::now();
        auto cbDurationUs = std::chrono::duration_cast<std::chrono::microseconds>(cbEnd - cbStart).count();
        if (cbDurationUs > CALLBACK_WARN_THRESHOLD_US)
        {
            fprintf(stderr, "[EventBus WARNING] Callback for eventId=0x%llx blocked bus for %lld us (threshold=%lld us)\n", static_cast<unsigned long long>(eventId), static_cast<long long>(cbDurationUs), static_cast<long long>(CALLBACK_WARN_THRESHOLD_US));
        }
    }

    // Periodic lazy GC
    uint32_t count = publishCount_.fetch_add(1, std::memory_order_relaxed);
    if ((count & (GC_INTERVAL - 1)) == 0)
    {
        PurgeExpired(eventId);
    }
}

// ============================================================
// PurgeExpired - Lazy GC: remove dead subscribers (COW write)
// ============================================================
void DefaultEventBus::PurgeExpired(uint64_t eventId)
{
    std::lock_guard<std::mutex> lock(subscriberMutex_);
    auto it = subscriberMap_.find(eventId);
    if (it == subscriberMap_.end())
        return;

    // Check if any expired
    bool hasExpired = false;
    for (const auto& sub : *it->second)
    {
        auto conn = sub.connection.lock();
        if (!conn || !conn->IsActive())
        {
            hasExpired = true;
            break;
        }
    }

    if (!hasExpired)
        return;

    // COW: clone, erase expired, replace
    auto newList = std::make_shared<std::vector<Subscriber>>();
    newList->reserve(it->second->size());
    for (auto& sub : *it->second)
    {
        auto conn = sub.connection.lock();
        if (conn && conn->IsActive())
            newList->push_back(std::move(sub));
    }
    it->second = std::move(newList);
}

// ============================================================
// EventLoopThread - MPSC consumer for DispatchMode::Queued
// ============================================================
void DefaultEventBus::EventLoopThread()
{
    while (running_.load(std::memory_order_acquire))
    {
        QueuedEvent evt;
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueCV_.wait(lock, [this]() { return !asyncQueue_.empty() || !running_.load(std::memory_order_acquire); });

            if (!running_.load(std::memory_order_acquire) && asyncQueue_.empty())
                break;

            if (asyncQueue_.empty())
                continue;

            evt = std::move(asyncQueue_.front());
            asyncQueue_.pop();
        }

        // Phase 3: Queue latency monitoring
        auto dequeueTime = std::chrono::steady_clock::now();
        auto latencyUs = std::chrono::duration_cast<std::chrono::microseconds>(dequeueTime - evt.enqueueTime).count();
        if (latencyUs > CALLBACK_WARN_THRESHOLD_US)
        {
            fprintf(stderr, "[EventBus WARNING] Queued event 0x%llx waited %lld us in queue\n", static_cast<unsigned long long>(evt.eventId), static_cast<long long>(latencyUs));
        }

        // Dispatch on the event loop thread (with exception isolation)
        try {
            DispatchDirect(evt.eventId, std::move(evt.payload));
        } catch (const std::exception& e) {
            ReportException(e);
        } catch (...) {
            ReportUnknownException();
        }
    }

    // Drain remaining events before exit (with exception isolation)
    std::lock_guard<std::mutex> lock(queueMutex_);
    while (!asyncQueue_.empty())
    {
        auto evt = std::move(asyncQueue_.front());
        asyncQueue_.pop();
        try {
            DispatchDirect(evt.eventId, std::move(evt.payload));
        } catch (const std::exception& e) {
            ReportException(e);
        } catch (...) {
            ReportUnknownException();
        }
    }
}
