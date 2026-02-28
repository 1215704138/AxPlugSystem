# AxPlug 事件总线 (Event Bus) · 使用手册

## 1. 简介

AxPlug 提供完全整合的进程内与跨节点级别的事件总线 (Event Bus)。它基于发布/订阅 (Pub/Sub) 模式，用于彻底解耦插件之间的相互依赖调用。

**核心特性：**
* **类型安全**：借助 `constexpr` 和 FNV-1a Hash 编译期生成 O(1) 匹配的 `EventID`。
* **高低并发隔离**：支持直接同步调用 (`DirectCall`) 与内部队列排队异步执行 (`Queued`)。
* **无缝网络扩展**：加载可选的网络事件总线插件后，本地进程内事件总线自动升级为基于 UDP 组播的网络事件集群。
* **延迟释放**：基于 `shared_ptr` 的 `EventConnection` 句柄自动管理订阅生命周期。

---

## 2. 如何订阅事件

你可以通过调用框架导出的全局 C API `Ax_GetEventBus()` 来获取总线实例。

```cpp
#include "AxPlug/AxEventBus.h"

class MyPlugin : public IMyPlugin {
    AxPlug::EventConnectionPtr subHandle;

public:
    void OnInit() override {
        // 订阅系统启动事件
        subHandle = Ax_GetEventBus()->Subscribe(AxPlug::EVENT_SYSTEM_INIT, [this](std::shared_ptr<AxPlug::AxEvent> evt) {
            auto initEvt = std::static_pointer_cast<AxPlug::SystemInitEvent>(evt);
            printf("系统已初始化！插件目录: %s\n", initEvt->pluginDir.c_str());
        });
    }

    void OnShutdown() override {
        // 可选：显式断开。或者让 subHandle 随对象析构自然释放
        if (subHandle) {
            subHandle->Disconnect();
        }
    }
};
```

**重要说明**：`Subscribe` 必然返回一个 `EventConnectionPtr` 智能指针句柄。如果该句柄被提早析构（例如没有作为一个成员变量保存），订阅会立刻失效。

---

## 3. 如何发布事件

发布事件同样通过 `Ax_GetEventBus()`。

### 3.1 同步发布 (DirectCall)

默认情况下，`Publish` 采用同步直接调用模式。事件发布时，所有当前订阅者的回调将在**发布者的当前线程中立刻同步执行**。

```cpp
#include "AxPlug/AxEventBus.h"

// 假设定义了一个事件类及 ID
class UserLoginEvent : public AxPlug::AxEvent {
public:
    int userId;
};
constexpr uint64_t EVENT_USER_LOGIN = AxPlug::HashEventId("Business::UserLogin");

// 发布事件
auto evt = std::make_shared<UserLoginEvent>();
evt->userId = 1001;
evt->sender = this; // sender 设置为自身句柄以供过滤

Ax_GetEventBus()->Publish(EVENT_USER_LOGIN, evt, AxPlug::DispatchMode::DirectCall);
```

### 3.2 异步发布 (Queued)

如果你不希望阻塞当前线程，或希望强制事件在框架独立的 Event Loop 线程中执行，请传入 `DispatchMode::Queued`。

```cpp
// 将事件推入队列后立刻返回，框架后台线程将接管后续回调
Ax_GetEventBus()->Publish(EVENT_USER_LOGIN, evt, AxPlug::DispatchMode::Queued);
```

---

## 4. 框架内置核心事件

AxPlug 在其生命周期关键节点会自动向总线抛出基本事件，以便插件模块捕获：

| 预置 EventID | 事件 Payload 类 | 触发时机 |
|---|---|---|
| `EVENT_SYSTEM_INIT` | `SystemInitEvent` | 当管理器 `Init()` 设置完基本管线、尚未加载插件时触发。载有 `pluginDir` |
| `EVENT_PLUGIN_LOADED` | `PluginLoadedEvent` | 每一个插件成功被管理器加载（载入内存）之后立刻派发。 |
| `EVENT_PLUGIN_UNLOADED`| *(未使用)* | *保留用于未来动态卸载机制。* |
| `EVENT_SYSTEM_SHUTDOWN`| `SystemShutdownEvent` | 整个 AxPlug 系统进入关闭清扫序列时首先发出。 |

---

## 5. 多对一与定向寄信（特定发送者过滤）

`Subscribe` API 的第三个参数为 `void* specificSender = nullptr`。
* 若为 `nullptr`（默认），则此订阅将接收全网**任何人**发出的此 `EventID`。
* 若非空，仅当发布者的事件中其 `payload->sender` 与之一致时，才触发回调。

```cpp
// 仅监听特定硬件设备实例发来的断线事件
subHandle = Ax_GetEventBus()->Subscribe(EVENT_DEVICE_DROP, myCallback, specificDevicePtr);
```
