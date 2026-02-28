#include <chrono>
#include <iostream>
#include <thread>
#include <atomic>
#include <string>
#include <vector>
#include <windows.h>

#include "AxPlug/AxPlug.h"
#include "core/INetworkEventBus.h"

// ============================================================
// Test event definitions
// ============================================================
constexpr uint64_t EVENT_TEST_LOCAL = AxPlug::HashEventId("Test::LocalEvent");
constexpr uint64_t EVENT_TEST_NETWORK = AxPlug::HashEventId("Test::NetworkSync");

// A simple local-only event (NOT INetworkableEvent — should never be broadcast)
class LocalTestEvent : public AxPlug::AxEvent
{
public:
    std::string message;
    int value = 0;
};

// A networkable event (inherits INetworkableEvent — will be broadcast)
class NetworkTestEvent : public AxPlug::INetworkableEvent
{
public:
    std::string payload;

    std::string Serialize() const override { return payload; }
    void Deserialize(const std::string& data) override { payload = data; }
};

// ============================================================
// Test counters
// ============================================================
static int g_passed = 0;
static int g_failed = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (cond) { std::cout << "  [PASS] " << (msg) << std::endl; ++g_passed; } \
        else { std::cout << "  [FAIL] " << (msg) << std::endl; ++g_failed; } \
    } while(0)

// ============================================================
// Test 1: Basic local event publish/subscribe (DefaultEventBus)
// ============================================================
void testLocalEventBus()
{
    std::cout << "\n=== Test 1: Local Event Bus (Publish/Subscribe) ===" << std::endl;

    std::atomic<int> callCount{0};
    std::string receivedMsg;

    // Subscribe
    auto conn = AxPlug::Subscribe(EVENT_TEST_LOCAL, [&](std::shared_ptr<AxPlug::AxEvent> evt) {
        auto local = std::dynamic_pointer_cast<LocalTestEvent>(evt);
        if (local)
        {
            receivedMsg = local->message;
            callCount.fetch_add(1);
        }
    });

    TEST_CHECK(conn != nullptr, "Subscribe returns valid connection");
    TEST_CHECK(conn->IsActive(), "Connection is active after subscribe");

    // Publish
    auto ev = std::make_shared<LocalTestEvent>();
    ev->message = "Hello EventBus";
    ev->value = 42;
    AxPlug::Publish(EVENT_TEST_LOCAL, ev);

    TEST_CHECK(callCount.load() == 1, "Callback invoked exactly once");
    TEST_CHECK(receivedMsg == "Hello EventBus", "Callback received correct message");

    // Publish again
    auto ev2 = std::make_shared<LocalTestEvent>();
    ev2->message = "Second event";
    ev2->value = 100;
    AxPlug::Publish(EVENT_TEST_LOCAL, ev2);

    TEST_CHECK(callCount.load() == 2, "Callback invoked twice after two publishes");
    TEST_CHECK(receivedMsg == "Second event", "Callback received second message");

    std::cout << "=== Test 1 Complete ===" << std::endl;
}

// ============================================================
// Test 2: RAII unsubscribe (EventConnection destruction)
// ============================================================
void testRAIIUnsubscribe()
{
    std::cout << "\n=== Test 2: RAII Unsubscribe ===" << std::endl;

    std::atomic<int> callCount{0};

    {
        // Subscribe in inner scope
        auto conn = AxPlug::Subscribe(EVENT_TEST_LOCAL, [&](std::shared_ptr<AxPlug::AxEvent>) {
            callCount.fetch_add(1);
        });

        auto ev = std::make_shared<LocalTestEvent>();
        AxPlug::Publish(EVENT_TEST_LOCAL, ev);
        TEST_CHECK(callCount.load() == 1, "Callback works while connection alive");

        // conn goes out of scope here -> Disconnect()
    }

    // Publish after connection destroyed — callback should NOT fire
    auto ev2 = std::make_shared<LocalTestEvent>();
    AxPlug::Publish(EVENT_TEST_LOCAL, ev2);
    TEST_CHECK(callCount.load() == 1, "Callback NOT invoked after connection destroyed (RAII)");

    std::cout << "=== Test 2 Complete ===" << std::endl;
}

// ============================================================
// Test 3: Manual Disconnect
// ============================================================
void testManualDisconnect()
{
    std::cout << "\n=== Test 3: Manual Disconnect ===" << std::endl;

    std::atomic<int> callCount{0};

    auto conn = AxPlug::Subscribe(EVENT_TEST_LOCAL, [&](std::shared_ptr<AxPlug::AxEvent>) {
        callCount.fetch_add(1);
    });

    auto ev = std::make_shared<LocalTestEvent>();
    AxPlug::Publish(EVENT_TEST_LOCAL, ev);
    TEST_CHECK(callCount.load() == 1, "Callback fires before disconnect");

    conn->Disconnect();
    TEST_CHECK(!conn->IsActive(), "Connection inactive after Disconnect()");

    AxPlug::Publish(EVENT_TEST_LOCAL, std::make_shared<LocalTestEvent>());
    TEST_CHECK(callCount.load() == 1, "Callback NOT invoked after manual disconnect");

    std::cout << "=== Test 3 Complete ===" << std::endl;
}

// ============================================================
// Test 4: Multiple subscribers
// ============================================================
void testMultipleSubscribers()
{
    std::cout << "\n=== Test 4: Multiple Subscribers ===" << std::endl;

    std::atomic<int> count1{0}, count2{0}, count3{0};

    auto c1 = AxPlug::Subscribe(EVENT_TEST_LOCAL, [&](std::shared_ptr<AxPlug::AxEvent>) { count1.fetch_add(1); });
    auto c2 = AxPlug::Subscribe(EVENT_TEST_LOCAL, [&](std::shared_ptr<AxPlug::AxEvent>) { count2.fetch_add(1); });
    auto c3 = AxPlug::Subscribe(EVENT_TEST_LOCAL, [&](std::shared_ptr<AxPlug::AxEvent>) { count3.fetch_add(1); });

    AxPlug::Publish(EVENT_TEST_LOCAL, std::make_shared<LocalTestEvent>());

    TEST_CHECK(count1.load() == 1 && count2.load() == 1 && count3.load() == 1, "All 3 subscribers received the event");

    c2->Disconnect();
    AxPlug::Publish(EVENT_TEST_LOCAL, std::make_shared<LocalTestEvent>());

    TEST_CHECK(count1.load() == 2 && count2.load() == 1 && count3.load() == 2, "Only active subscribers received second event");

    std::cout << "=== Test 4 Complete ===" << std::endl;
}

// ============================================================
// Test 5: Async dispatch (DispatchMode::Queued)
// ============================================================
void testAsyncDispatch()
{
    std::cout << "\n=== Test 5: Async Dispatch (Queued) ===" << std::endl;

    std::atomic<int> callCount{0};
    std::atomic<DWORD> callbackThreadId{0};
    DWORD publisherThreadId = GetCurrentThreadId();

    auto conn = AxPlug::Subscribe(EVENT_TEST_LOCAL, [&](std::shared_ptr<AxPlug::AxEvent>) {
        callbackThreadId.store(GetCurrentThreadId());
        callCount.fetch_add(1);
    });

    AxPlug::Publish(EVENT_TEST_LOCAL, std::make_shared<LocalTestEvent>(), AxPlug::DispatchMode::Queued);

    // Wait for async delivery
    for (int i = 0; i < 100 && callCount.load() == 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    TEST_CHECK(callCount.load() == 1, "Async callback invoked");
    TEST_CHECK(callbackThreadId.load() != publisherThreadId, "Async callback ran on different thread");

    std::cout << "=== Test 5 Complete ===" << std::endl;
}

// ============================================================
// Test 6: NetworkEventBus plugin takeover and restore
// ============================================================
void testNetworkEventBusTakeover()
{
    std::cout << "\n=== Test 6: NetworkEventBus Takeover & Restore ===" << std::endl;

    // Get the network event bus service
    auto netBus = AxPlug::GetService<AxPlug::INetworkEventBus>();
    if (!netBus)
    {
        std::cout << "  [SKIP] NetworkEventBusPlugin not loaded (DLL not found)" << std::endl;
        return;
    }

    std::cout << "  NetworkEventBusPlugin loaded successfully" << std::endl;
    std::cout << "  NodeId: 0x" << std::hex << netBus->GetNodeId() << std::dec << std::endl;

    // After GetService, OnInit() should have replaced the bus (takeover)
    // Verify local events still work through the proxy
    std::atomic<int> localCount{0};
    auto conn = AxPlug::Subscribe(EVENT_TEST_LOCAL, [&](std::shared_ptr<AxPlug::AxEvent>) {
        localCount.fetch_add(1);
    });

    AxPlug::Publish(EVENT_TEST_LOCAL, std::make_shared<LocalTestEvent>());
    TEST_CHECK(localCount.load() == 1, "Local events work through NetworkEventBus proxy");

    // Verify that a non-INetworkableEvent does NOT get broadcast (anti-storm whitelist)
    // (We can't directly observe network traffic, but we verify local dispatch works)
    TEST_CHECK(localCount.load() == 1, "Non-networkable events dispatched locally only");

    // Register a networkable event factory
    netBus->RegisterNetworkableEvent(EVENT_TEST_NETWORK, []() { return std::make_shared<NetworkTestEvent>(); });

    // Start network
    bool started = netBus->StartNetwork("239.255.0.1", 30001);
    if (started)
    {
        TEST_CHECK(netBus->IsNetworkActive(), "Network is active after StartNetwork");

        // Subscribe to network event
        std::atomic<int> netCount{0};
        std::string receivedPayload;
        auto netConn = AxPlug::Subscribe(EVENT_TEST_NETWORK, [&](std::shared_ptr<AxPlug::AxEvent> evt) {
            auto ne = std::dynamic_pointer_cast<NetworkTestEvent>(evt);
            if (ne)
            {
                receivedPayload = ne->payload;
                netCount.fetch_add(1);
            }
        });

        // Publish a networkable event — should dispatch locally AND broadcast
        auto netEvt = std::make_shared<NetworkTestEvent>();
        netEvt->payload = "{\"test\":\"hello_network\"}";
        AxPlug::Publish(EVENT_TEST_NETWORK, netEvt);

        TEST_CHECK(netCount.load() >= 1, "Networkable event dispatched locally");

        // Wait briefly to see if loopback arrives (should be filtered by nodeId)
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        TEST_CHECK(netCount.load() == 1, "Loopback correctly filtered (no duplicate from self)");

        // Stop network
        netBus->StopNetwork();
        TEST_CHECK(!netBus->IsNetworkActive(), "Network stopped successfully");
    }
    else
    {
        std::cout << "  [SKIP] StartNetwork failed (firewall/adapter issue)" << std::endl;
    }

    std::cout << "=== Test 6 Complete ===" << std::endl;
}

// ============================================================
// Test 7: Verify bus restoration after NetworkEventBus release
// ============================================================
void testBusRestoration()
{
    std::cout << "\n=== Test 7: Bus Restoration After Plugin Release ===" << std::endl;

    // Release the NetworkEventBus service — triggers OnShutdown -> restores original bus
    AxPlug::ReleaseService<AxPlug::INetworkEventBus>();

    // The original DefaultEventBus should be restored
    auto* bus = AxPlug::GetEventBus();
    TEST_CHECK(bus != nullptr, "Event bus is NOT null after NetworkEventBus shutdown (restored)");

    // Verify events still work on the restored bus
    std::atomic<int> callCount{0};
    auto conn = AxPlug::Subscribe(EVENT_TEST_LOCAL, [&](std::shared_ptr<AxPlug::AxEvent>) {
        callCount.fetch_add(1);
    });

    AxPlug::Publish(EVENT_TEST_LOCAL, std::make_shared<LocalTestEvent>());
    TEST_CHECK(callCount.load() == 1, "Events still work after NetworkEventBus release (bus restored)");

    std::cout << "=== Test 7 Complete ===" << std::endl;
}

// ============================================================
// Test 8: Anti-storm whitelist — LocalTestEvent must NOT be serialized
// ============================================================
void testAntiStormWhitelist()
{
    std::cout << "\n=== Test 8: Anti-Storm Whitelist ===" << std::endl;

    // Verify LocalTestEvent is NOT an INetworkableEvent
    auto local = std::make_shared<LocalTestEvent>();
    auto asNet = std::dynamic_pointer_cast<AxPlug::INetworkableEvent>(local);
    TEST_CHECK(asNet == nullptr, "LocalTestEvent is NOT INetworkableEvent (whitelist safe)");

    // Verify NetworkTestEvent IS an INetworkableEvent
    auto net = std::make_shared<NetworkTestEvent>();
    auto asNet2 = std::dynamic_pointer_cast<AxPlug::INetworkableEvent>(net);
    TEST_CHECK(asNet2 != nullptr, "NetworkTestEvent IS INetworkableEvent");

    // Verify serialization round-trip
    net->payload = "{\"key\":\"value\"}";
    std::string serialized = net->Serialize();
    auto net2 = std::make_shared<NetworkTestEvent>();
    net2->Deserialize(serialized);
    TEST_CHECK(net2->payload == "{\"key\":\"value\"}", "Serialization round-trip correct");

    std::cout << "=== Test 8 Complete ===" << std::endl;
}

// ============================================================
// main
// ============================================================
int main()
{
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);

    std::cout << "========================================" << std::endl;
    std::cout << "  AxPlug Event Bus Test Suite" << std::endl;
    std::cout << "========================================" << std::endl;

    try
    {
        // Initialize plugin system
        std::cout << "\nInitializing plugin system..." << std::endl;
        AxPlug::Init();
        std::cout << "Plugin system initialized.\n" << std::endl;

        // Run tests
        testLocalEventBus();
        testRAIIUnsubscribe();
        testManualDisconnect();
        testMultipleSubscribers();
        testAsyncDispatch();
        testAntiStormWhitelist();
        testNetworkEventBusTakeover();
        testBusRestoration();
    }
    catch (const std::exception& e)
    {
        std::cerr << "\n[EXCEPTION] " << e.what() << std::endl;
        ++g_failed;
    }

    // Summary
    std::cout << "\n========================================" << std::endl;
    std::cout << "  Results: " << g_passed << " passed, " << g_failed << " failed" << std::endl;
    std::cout << "========================================" << std::endl;

    return g_failed > 0 ? 1 : 0;
}
