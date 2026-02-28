#pragma once

#include <cstdint>
#include <memory>
#include <functional>
#include <atomic>
#include <string>

// AxCore DLL export/import control
#ifdef AX_CORE_EXPORTS
#define AX_CORE_API __declspec(dllexport)
#else
#ifdef _WIN32
#define AX_CORE_API __declspec(dllimport)
#else
#define AX_CORE_API
#endif
#endif

namespace AxPlug
{

// ============================================================
// Compile-time FNV-1a hash for event ID (reuses same constants as AxTypeHash)
// ============================================================
constexpr uint64_t AX_EVENT_FNV_OFFSET = 14695981039346656037ULL;
constexpr uint64_t AX_EVENT_FNV_PRIME  = 1099511628211ULL;

constexpr uint64_t HashEventId(const char* str, uint64_t hash = AX_EVENT_FNV_OFFSET)
{
    return (*str == 0) ? hash : HashEventId(str + 1, (hash ^ static_cast<uint64_t>(*str)) * AX_EVENT_FNV_PRIME);
}

// ============================================================
// AxEvent - base class for all event payloads
// ============================================================
class AxEvent
{
public:
    virtual ~AxEvent() = default;

    // Sender identity: who published this event. nullptr = anonymous.
    void* sender = nullptr;
};

// ============================================================
// EventConnection - RAII subscription handle (Lazy GC)
// ============================================================
class EventConnection
{
public:
    virtual ~EventConnection()
    {
        Disconnect();
    }

    void Disconnect()
    {
        m_active.store(false, std::memory_order_release);
    }

    bool IsActive() const
    {
        return m_active.load(std::memory_order_acquire);
    }

private:
    std::atomic<bool> m_active{ true };
};

using EventConnectionPtr = std::shared_ptr<EventConnection>;

// ============================================================
// DispatchMode - controls how callbacks are invoked
// ============================================================
enum class DispatchMode
{
    DirectCall, // Synchronous: callback runs in publisher's thread immediately
    Queued      // Asynchronous: enqueued to internal EventLoop thread
};

// ============================================================
// EventCallback - consumer callback signature
// ============================================================
using EventCallback = std::function<void(std::shared_ptr<AxEvent>)>;

// ============================================================
// IEventBus - abstract event bus interface
// ============================================================
class IEventBus
{
public:
    virtual ~IEventBus() = default;

    // Publish an event (sync or async)
    virtual void Publish(uint64_t eventId, std::shared_ptr<AxEvent> payload, DispatchMode mode = DispatchMode::DirectCall) = 0;

    // Subscribe to an event. Keep the returned EventConnectionPtr alive to stay subscribed.
    // If specificSender != nullptr, only events from that sender trigger callback.
    virtual EventConnectionPtr Subscribe(uint64_t eventId, EventCallback callback, void* specificSender = nullptr) = 0;
};

// ============================================================
// INetworkableEvent - extension for cross-process serialization
// ============================================================
class INetworkableEvent : public AxEvent
{
public:
    virtual ~INetworkableEvent() = default;

    virtual std::string Serialize() const = 0;
    virtual void Deserialize(const std::string& data) = 0;
};

// ============================================================
// Framework core event IDs
// ============================================================
constexpr uint64_t EVENT_SYSTEM_INIT    = HashEventId("Core::SystemInit");
constexpr uint64_t EVENT_PLUGIN_LOADED  = HashEventId("Core::PluginLoaded");
constexpr uint64_t EVENT_PLUGIN_UNLOADED = HashEventId("Core::PluginUnloaded");
constexpr uint64_t EVENT_SYSTEM_SHUTDOWN = HashEventId("Core::SystemShutdown");

// ============================================================
// Framework core event payloads
// ============================================================
class PluginLoadedEvent : public AxEvent
{
public:
    std::string pluginName;
    std::string version;
};

class SystemInitEvent : public AxEvent
{
public:
    std::string pluginDir;
};

class SystemShutdownEvent : public AxEvent {};

// ============================================================
// Example networkable event (Phase 4)
// ============================================================
class RemoteDataSyncEvent : public INetworkableEvent
{
public:
    std::string payloadJson;

    std::string Serialize() const override
    {
        return payloadJson;
    }

    void Deserialize(const std::string& data) override
    {
        payloadJson = data;
    }
};

} // namespace AxPlug

// ============================================================
// C API for event bus (exported from AxCore.dll)
// ============================================================
extern "C"
{
    AX_CORE_API AxPlug::IEventBus* Ax_GetEventBus();
    AX_CORE_API void Ax_SetEventBus(AxPlug::IEventBus* bus);
}
