# AxPlug 事件总线 (Event Bus) · 高级开发手册 v1.0

本文档面向需要在 AxPlug 框架中开发高级定制化事件（包含能够跨网络节点的分布式事件），以及理解事件总线如何与现有架构集成的开发者。

## 1. 原生跨动态库事件机制

AxPlug 将事件基类 `AxEvent` 暴露在公共头文件 `AxEventBus.h` 中。这意味着任意两个完全由不同团队编译的业务 DLL，在不知晓对方存在的情况下，可以通过一个共享的 64-bit 约定 Hash 码 (`EventID`) 与一个双方共同理解的 Payload 结构体来进行双盲通讯。

### 1.1 编写常规进程内事件

定义新事件只需从 `AxEvent` 派生，并分配一个不冲突的 ID。
为了绝对安全，推荐使用 `AxPlug::HashEventId("Domain::EventName")`。

```cpp
#pragma once
#include "AxPlug/AxEventBus.h"
#include <string>

// MyDomainEvents.h
constexpr uint64_t EVENT_DATA_ARRIVED = AxPlug::HashEventId("MyDomain::DataArrived");

class DataArrivedEvent : public AxPlug::AxEvent {
public:
    std::string internalBuffer;
    int payloadSize;
};
```

进程内的所有投递均使用该实例的智能指针，避免了二次拷贝的开销。对于大内存块转移，这是极高效率的。

## 2. 分布式：网络化事件 (Networkable Events)

在某些分布式场景，由于插件跨越多台物理节点（Exe实例），本地进程内的数据指针就失去了意义。

为了支持这类通讯，AxPlug 设计了 `INetworkableEvent` 基类。由网络插件 `INetworkEventBus` (例如组播 UDP 实现的 `NetworkEventBusPlugin.dll`) 发挥作用。

### 2.1 编写网络事件

你的类必须继承 `INetworkableEvent` 并实现序列化/反序列化两个接口。

```cpp
#include "AxPlug/AxEventBus.h"

constexpr uint64_t EVENT_REMOTE_SYNC = AxPlug::HashEventId("MyDomain::RemoteSync");

class RemoteSyncEvent : public AxPlug::INetworkableEvent {
public:
    std::string jsonPayload;

    // 转换为 std::string, 用于底层 UDP Plugin 的发送缓冲区
    std::string Serialize() const override {
        return jsonPayload;
    }

    // 从底层接收缓冲区重建数据
    void Deserialize(const std::string& data) override {
        jsonPayload = data;
    }
};
```

### 2.2 注册工厂 (Network Event Factory)

`INetworkEventBus` 接收到网络字节流后，需要知道如何重建该 C++ 对象。你需要为该网络事件提供一个默认工厂：

```cpp
#include "core/INetworkEventBus.h"

// 任何负责定义了此类型的主导插件，需要在 OnInit 中向网络总线注册其反序列化工场
void MySysPlugin::OnInit() {
    auto netBus = AxPlug::GetService<AxPlug::INetworkEventBus>("udp_multicast");
    if (netBus) {
       netBus->RegisterNetworkableEvent(EVENT_REMOTE_SYNC, []() {
           return std::make_shared<RemoteSyncEvent>();
       });
    }
}
```

没有注册此 Event 工厂的本地节点，如果收到了这个 `EVENT_REMOTE_SYNC` 的网络封包，将会被底层的解码器当做“未知异星事件”而被静默丢弃，有效隔离了无需关注的节点流量。

## 3. 架构解析：插件化事件总线的 "夺舍"

AxPlug 首个版本的一个精妙架构是将核心与插件界限彻底打通的 **"夺舍" (Takeover)** 机制。

* AxCore 核心库在系统启动时，硬编码提供了一个 `DefaultEventBus`，这是一个纯本地的无锁进程内路由总线。`Ax_GetEventBus()` 返回的就是它。
* 如果你编写并加载了 `NetworkEventBusPlugin`（该类实现了 `INetworkEventBus` 且内部包装了基于 Boost.Asio 的组播服务）。并且在这个网络插件初始化的时候调用 `Ax_SetEventBus(this->AsEventBus())`，它就会替换掉框架原本的通信线缆头。
* 此后，系统中各个插件业务层代码对 `Ax_GetEventBus()->Publish(...)` 的调用不变，但消息事实上已经被路由到了你的网络插件中，在那里你的插件可以判断其是否派生自 `INetworkableEvent` 进而投射向远端的 UDP 多播集群。
* 而由于新注入的网络总线仍然忠实地维护了原生的内存回调链表，原有的进程内通讯（如 `SystemInit`）丝毫不受干扰。

这是真正的开闭原则体现。框架本身只留有一个 `IEventBus` 接口插槽，即可支持未来发展成 Kafka 或 Redis PubSub 为核心的重型外总线结构，同时所有下游业务插件的 `EventBus` 调用指令无需重编与修改。
