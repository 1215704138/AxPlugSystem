#pragma once

#include "AxPlug/AxEventBus.h"
#include "AxPlug/IAxObject.h"
#include <functional>

namespace AxPlug
{

// Factory function to create an empty INetworkableEvent for deserialization
using NetworkEventFactory = std::function<std::shared_ptr<INetworkableEvent>()>;

// INetworkEventBus - Plugin interface that extends IAxObject for plugin lifecycle
// and proxies IEventBus for transparent event bus replacement ("夺舍" takeover).
//
// Usage:
//   auto netBus = AxPlug::GetService<INetworkEventBus>();
//   netBus->StartNetwork("239.255.0.1", 30001);
//   netBus->RegisterNetworkableEvent(MY_EVENT_ID, []() { return std::make_shared<MyEvent>(); });
class INetworkEventBus : public IAxObject
{
    AX_INTERFACE(INetworkEventBus)
public:
    // Start UDP multicast network transport
    virtual bool StartNetwork(const char* multicastGroup, int port) = 0;

    // Stop network transport and receiver thread
    virtual void StopNetwork() = 0;

    // Query network state
    virtual bool IsNetworkActive() const = 0;

    // Register a factory for a networkable event type (required for deserialization)
    virtual void RegisterNetworkableEvent(uint64_t eventId, NetworkEventFactory factory) = 0;

    // Get the IEventBus interface pointer (for SetEventBus takeover)
    virtual IEventBus* AsEventBus() = 0;

    // Get the 64-bit node ID of this process (for cross-node sender identity)
    virtual uint64_t GetNodeId() const = 0;
};

} // namespace AxPlug
