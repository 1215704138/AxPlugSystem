# AxPlug 事件总线 (Event Bus) · 使用与架构手册

> 本文档为事件总线的**使用手册**，涵盖 API 用法、使用场景和架构原理。如需了解底层实现细节与维护指南，请参阅 [EventBus_DEV.md](EventBus_DEV.md)。

---

## 1. 简介

### 1.1 什么是事件总线？

事件总线是 AxPlugSystem 中用于**彻底解耦**插件间通信的核心机制。它实现了经典的发布-订阅模式：

- **发布者 (Publisher)**：不需要知道谁在监听，只需将事件投递到总线
- **订阅者 (Subscriber)**：不需要知道谁在发送，只需注册感兴趣的事件类型
- **结果**：发布者和订阅者代码中无需 `#include` 对方头文件，完全解耦

### 1.2 核心特性

| 特性 | 说明 |
|------|------|
| **编译期事件ID** | FNV-1a 哈希将字符串在编译期变为 `uint64_t`，运行时 O(1) 查找 |
| **COW 并发安全** | 订阅列表使用写时复制策略，发布路径无锁遍历 |
| **Lazy GC** | 订阅句柄销毁后自动失效，后台周期性清理死亡订阅 |
| **同步/异步双模式** | `DirectCall` 当前线程阻塞派发；`Queued` 推入队列立即返回 |
| **异常隔离** | 回调抛异常不会崩溃总线，可设置全局异常处理器 |
| **发送者过滤** | 订阅时可指定只接收特定发送者的事件 |
| **网络事件扩展** | 通过 `INetworkEventBus` 插件透明实现跨进程 UDP 多播 |

---

## 2. 快速开始

### 2.1 订阅事件

```cpp
#include "AxPlug/AxEventBus.h"

class MyPlugin {
    // 必须作为成员变量！局部变量会在函数结束时自动退订
    AxPlug::EventConnectionPtr conn_;

public:
    void Start() {
        conn_ = Ax_GetEventBus()->Subscribe(
            AxPlug::EVENT_SYSTEM_INIT,
            [this](std::shared_ptr<AxPlug::AxEvent> evt) {
                auto initEvt = std::static_pointer_cast<AxPlug::SystemInitEvent>(evt);
                printf("系统初始化完成, 插件目录: %s\n", initEvt->pluginDir.c_str());
            }
        );
    }

    void Stop() {
        // 可选：手动断开。不调用也会在 conn_ 析构时自动退订。
        if (conn_) conn_->Disconnect();
    }
};
```

> **重要**：`EventConnectionPtr` 必须存为成员变量。如果写成局部变量，函数结束时变量被销毁，订阅立即失效。

### 2.2 发布事件

```cpp
// 同步发布 — 阻塞当前线程直到所有回调完成
auto evt = std::make_shared<AxPlug::SystemInitEvent>();
evt->pluginDir = "/path/to/plugins";
Ax_GetEventBus()->Publish(AxPlug::EVENT_SYSTEM_INIT, evt, AxPlug::DispatchMode::DirectCall);

// 异步发布 — 推入队列立即返回，后台线程派发
Ax_GetEventBus()->Publish(AxPlug::EVENT_SYSTEM_INIT, evt, AxPlug::DispatchMode::Queued);
```

### 2.3 便捷 API（推荐）

```cpp
#include "AxPlug/AxPlug.h"

// 等价于 Ax_GetEventBus()->Subscribe(...)
auto conn = AxPlug::Subscribe(eventId, callback);

// 等价于 Ax_GetEventBus()->Publish(...)
AxPlug::Publish(eventId, payload);
AxPlug::Publish(eventId, payload, AxPlug::DispatchMode::Queued);
```

---

## 3. 自定义事件

### 3.1 定义事件ID和载荷

```cpp
// MyEvents.h — 放在共享头文件目录供多个插件使用
#pragma once
#include "AxPlug/AxEventBus.h"

// 编译期生成唯一ID（命名建议: "Domain::EventName"）
constexpr uint64_t EVENT_USER_LOGIN = AxPlug::HashEventId("Business::UserLogin");

// 事件载荷 — 继承自 AxEvent
class UserLoginEvent : public AxPlug::AxEvent {
public:
    int userId;
    const char* userName;  // 跨DLL建议用 const char* 而非 std::string
};
```

### 3.2 发布自定义事件

```cpp
auto evt = std::make_shared<UserLoginEvent>();
evt->userId = 1001;
evt->userName = "张三";
evt->sender = this;  // 可选：标记发送者

AxPlug::Publish(EVENT_USER_LOGIN, evt);
```

### 3.3 订阅自定义事件

```cpp
conn_ = AxPlug::Subscribe(EVENT_USER_LOGIN, [](std::shared_ptr<AxPlug::AxEvent> evt) {
    auto login = std::static_pointer_cast<UserLoginEvent>(evt);
    printf("用户 %s (ID=%d) 已登录\n", login->userName, login->userId);
});
```

### 3.4 发送者过滤

`Subscribe` 的第三个参数指定只接收特定发送者的事件：

```cpp
// 仅接收来自 specificDevice 指针的事件
conn_ = AxPlug::Subscribe(EVENT_DEVICE_DATA, callback, specificDevice);
```

设为 `nullptr`（默认）则接收所有发送者的事件。

---

## 4. 异常处理

### 4.1 设置全局异常处理器

当订阅者回调抛出异常时，事件总线会捕获并路由到全局处理器，而非崩溃：

```cpp
AxPlug::SetExceptionHandler([](const std::exception& e) {
    fprintf(stderr, "[EventBus Error] %s\n", e.what());
    // 或写入日志系统
});
```

不设置处理器时，异常信息会输出到 `stderr`。

---

## 5. 网络事件（跨进程通信）

### 5.1 定义网络可序列化事件

继承 `INetworkableEvent`（而非 `AxEvent`），实现序列化/反序列化：

```cpp
constexpr uint64_t EVENT_REMOTE_SYNC = AxPlug::HashEventId("MyDomain::RemoteSync");

class RemoteSyncEvent : public AxPlug::INetworkableEvent {
public:
    std::string jsonPayload;

    std::string Serialize() const override { return jsonPayload; }
    void Deserialize(const std::string& data) override { jsonPayload = data; }
};
```

### 5.2 注册反序列化工厂

在接收端插件中注册事件工厂，让网络层知道如何重建对象：

```cpp
auto netBus = AxPlug::GetService<AxPlug::INetworkEventBus>();
if (netBus) {
    netBus->RegisterNetworkableEvent(EVENT_REMOTE_SYNC, []() {
        return std::make_shared<RemoteSyncEvent>();
    });
}
```

### 5.3 启动网络事件总线

```cpp
auto netBus = AxPlug::GetService<AxPlug::INetworkEventBus>();
if (netBus) {
    // 启动 UDP 多播
    netBus->StartNetwork("239.255.0.1", 30001);
    // 替换全局总线（"夺舍"）
    AxPlug::SetEventBus(netBus->AsEventBus());
}
```

替换后，所有 `AxPlug::Publish()` 调用无需修改：
- 普通 `AxEvent`：仅本地派发
- `INetworkableEvent`：本地派发 + 网络广播

### 5.4 INetworkEventBus 接口

| 方法 | 说明 |
|------|------|
| `StartNetwork(group, port)` | 启动 UDP 多播传输 |
| `StopNetwork()` | 停止网络传输和接收线程 |
| `IsNetworkActive()` | 查询网络状态 |
| `RegisterNetworkableEvent(id, factory)` | 注册反序列化工厂 |
| `AsEventBus()` | 获取 `IEventBus*` 用于全局总线替换 |
| `GetNodeId()` | 获取本进程的64位节点ID |

---

## 6. API 参考

### 6.1 IEventBus 接口

| 方法 | 说明 |
|------|------|
| `Publish(eventId, payload, mode)` | 发布事件。`mode` 默认 `DirectCall` |
| `Subscribe(eventId, callback, sender)` | 订阅事件。返回 `EventConnectionPtr` |
| `SetExceptionHandler(handler)` | 设置全局异常处理器。传 `nullptr` 清除 |

### 6.2 AxPlug 命名空间便捷函数

| 函数 | 说明 |
|------|------|
| `AxPlug::GetEventBus()` | 获取当前全局事件总线实例 |
| `AxPlug::SetEventBus(bus)` | 替换全局事件总线 |
| `AxPlug::Publish(id, payload, mode)` | 便捷发布 |
| `AxPlug::Subscribe(id, callback, sender)` | 便捷订阅 |
| `AxPlug::SetExceptionHandler(handler)` | 设置异常处理器 |

### 6.3 DispatchMode 枚举

| 值 | 说明 |
|----|------|
| `DirectCall` | 同步：在发布者线程中立即执行所有回调 |
| `Queued` | 异步：推入内部队列，由独立 EventLoop 线程派发 |

### 6.4 框架内置事件

| 事件ID | 载荷类 | 触发时机 |
|--------|--------|----------|
| `EVENT_SYSTEM_INIT` | `SystemInitEvent` | 管理器 `Init()` 完成后、加载插件前 |
| `EVENT_PLUGIN_LOADED` | `PluginLoadedEvent` | 每个插件成功加载后 |
| `EVENT_PLUGIN_UNLOADED` | `AxEvent` | 插件卸载时 |
| `EVENT_SYSTEM_SHUTDOWN` | `SystemShutdownEvent` | 系统进入关闭序列时 |

---

## 7. 使用注意事项

| 注意事项 | 说明 |
|----------|------|
| `EventConnectionPtr` 生命周期 | **必须**存为成员变量，局部变量会导致订阅立即失效 |
| 回调中避免耗时操作 | `DirectCall` 模式回调阻塞发布者线程。超过 16ms 会输出 WARNING |
| 跨DLL载荷字段类型 | 建议用 POD 类型和 `const char*`，避免 `std::string`/`std::vector` |
| 回调线程安全 | `DirectCall` 回调在发布者线程执行；`Queued` 回调在 EventLoop 线程执行 |
| 递归发布 | 回调中再 `Publish` 同一事件可能递归。需要解耦时改用 `Queued` 模式 |
