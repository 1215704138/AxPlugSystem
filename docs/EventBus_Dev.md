# 事件总线 (EventBus) · 0基础开发交接指南

> 本文档面向刚接手本项目的开发者，帮助你快速理解事件总线的技术栈、底层实现原理、以及如何在此基础上进行维护和扩展。

---

## 1. 技术栈总览

| 技术 | 用途 | 在本系统中的位置 |
|------|------|------------------|
| **C++17 `constexpr`** | 编译期 FNV-1a 哈希，将事件名字符串变为 `uint64_t` ID | `AxEventBus.h` — `HashEventId()` |
| **`std::shared_ptr` / `std::weak_ptr`** | RAII 订阅句柄 + Lazy GC 弱引用失效检测 | `EventConnection` / `Subscriber::connection` |
| **COW (Copy-On-Write)** | 订阅列表的并发安全读写分离 | `DefaultEventBus::subscriberMap_` |
| **MPSC 队列** | 异步事件派发（多生产者单消费者） | `DefaultEventBus::asyncQueue_` + `eventLoopThread_` |
| **`std::mutex` + `std::condition_variable`** | 异步队列的线程同步 | `queueMutex_` / `queueCV_` |
| **`std::atomic`** | 无锁标志位（运行状态、GC 计数器） | `running_` / `publishCount_` / `EventConnection::m_active` |
| **UDP 多播 (Multicast)** | 跨进程网络事件广播 | `NetworkEventBusImpl` (Winsock2 API) |
| **Proxy 设计模式** | 透明替换全局事件总线（"夺舍"机制） | `EventBusProxy` 代理类 |

---

## 2. 学习路线（推荐阅读顺序）

### 2.1 必备前置知识

如果你对以下概念不熟悉，建议先花 1-2 天学习：

1. **C++ 智能指针** — `shared_ptr` / `weak_ptr` / `make_shared`
   - 推荐：《C++ Primer》第12章，或 cppreference.com 的 `shared_ptr` 页面
   - 重点理解：引用计数、weak_ptr 的 `lock()` 语义、循环引用问题

2. **C++ 多线程基础** — `std::mutex` / `std::thread` / `std::condition_variable` / `std::atomic`
   - 推荐：《C++ Concurrency in Action》第2-4章
   - 重点理解：`lock_guard` 的 RAII 加锁、条件变量的 wait/notify 模式、`memory_order` 语义

3. **观察者模式 / 发布-订阅模式**
   - 推荐：《设计模式》Observer 章节，或搜索 "Event Bus pattern C++"
   - 重点理解：发布者与订阅者的解耦、回调函数注册

4. **FNV-1a 哈希**
   - 搜索 "FNV-1a hash algorithm" 了解原理即可（5分钟）
   - 在本系统中用于将字符串事件名编译期转为 `uint64_t`

### 2.2 代码阅读顺序

```
① include/AxPlug/AxEventBus.h        ← 接口层：IEventBus、AxEvent、EventConnection、DispatchMode
② src/AxCore/DefaultEventBus.h        ← 实现层头文件：COW 数据结构、MPSC 队列声明
③ src/AxCore/DefaultEventBus.cpp      ← 实现层：Subscribe (COW写)、Publish、DispatchDirect、EventLoopThread
④ include/core/INetworkEventBus.h     ← 网络扩展接口
⑤ src/core/NetworkEventBus/           ← 网络事件总线实现（UDP多播）
```

---

## 3. 核心机制深度解析

### 3.1 COW (Copy-On-Write) 订阅列表

**问题**：如果在 `Publish` 遍历订阅者列表时，某个回调内部调用了 `Subscribe` 或销毁了 `EventConnection`，会导致迭代器失效或死锁。

**解决方案**：订阅列表使用 `shared_ptr<vector<Subscriber>>` 存储。

```
写路径 (Subscribe):
  1. 加锁 subscriberMutex_
  2. 深拷贝当前列表 → 新 shared_ptr<vector>
  3. 向新列表追加订阅者
  4. 原子替换 map 中的 shared_ptr
  5. 解锁

读路径 (Publish → GetSnapshot):
  1. 加锁 subscriberMutex_ (极短)
  2. 拷贝 shared_ptr (仅引用计数 +1)
  3. 解锁
  4. 在快照上遍历派发 — 完全无锁
```

**关键源码**：`DefaultEventBus.cpp` 的 `Subscribe()` 和 `GetSnapshot()` 方法。

### 3.2 Lazy GC (惰性垃圾回收)

**问题**：订阅者模块卸载后，其回调不能再被调用，但立即从列表中删除会引发并发问题。

**解决方案**：

- `EventConnection` 内部只有一个 `atomic<bool> m_active`
- `Subscriber` 持有 `weak_ptr<EventConnection>`
- 派发时通过 `weak_ptr::lock()` 检查是否存活，失效则跳过
- 每 64 次 Publish 触发一次 `PurgeExpired()`，用 COW 方式清理死亡订阅

**GC 触发条件**：`publishCount_` 每 64 次（`GC_INTERVAL`）执行一次清扫。

### 3.3 MPSC 异步事件队列

```
生产者线程 (N个)                    消费者线程 (1个 EventLoopThread)
     │                                       │
     │  Publish(Queued)                      │  wait(queueCV_)
     │  ──► lock(queueMutex_)               │
     │      push(asyncQueue_)               │  ──► pop(asyncQueue_)
     │      unlock                          │      DispatchDirect(...)
     │      notify_one(queueCV_)            │
```

- 生产者：任意线程调用 `Publish(..., DispatchMode::Queued)`
- 消费者：`EventLoopThread` 循环等待，取出事件后调用 `DispatchDirect`
- 关机时：`Shutdown()` 设置 `running_=false`，drain 队列中剩余事件

### 3.4 异常隔离

每个回调执行都包裹在 `try/catch` 中：

- 如果设置了 `ExceptionHandler`（通过 `SetExceptionHandler`），异常被路由到该处理器
- 否则输出到 `stderr`
- 回调抛异常**不会**导致事件总线崩溃或影响后续订阅者

### 3.5 网络事件总线 — "夺舍"机制

```
启动前:  Ax_GetEventBus() → DefaultEventBus
                                    ↑
启动后:  Ax_SetEventBus(proxy)      │
         Ax_GetEventBus() → EventBusProxy ──► 本地: DefaultEventBus
                                             ──► 网络: UDP 多播广播
```

`NetworkEventBusImpl` 的工作流程：
1. `OnInit()` 时保存原始 `localBus_` 指针
2. `StartNetwork()` 创建 UDP 多播 socket，启动接收线程
3. 调用 `Ax_SetEventBus(proxy_.get())` 替换全局总线
4. `EventBusProxy::Publish()` 对每个事件：
   - 先调用 `localBus_->Publish()` 本地派发
   - 如果 payload 是 `INetworkableEvent`，序列化后通过 UDP 广播
5. 接收线程收到网络包后，反序列化并在本地重新 Publish

**Wire Protocol** (小端序): `[8B eventId][8B nodeId][4B payloadLen][payload]`

**防风暴**: 每个 eventId 每秒最多 100 次广播 (`RATE_LIMIT_MAX`)

---

## 4. 文件清单与职责

| 文件 | 行数 | 职责 |
|------|------|------|
| `include/AxPlug/AxEventBus.h` | ~183 | 公开接口：`IEventBus`、`AxEvent`、`EventConnection`、`DispatchMode`、内置事件ID与Payload、`INetworkableEvent`、C API |
| `src/AxCore/DefaultEventBus.h` | ~88 | 默认实现头文件：COW 数据结构、MPSC 队列、GC 配置常量 |
| `src/AxCore/DefaultEventBus.cpp` | ~262 | 默认实现：`Publish`/`Subscribe`/`DispatchDirect`/`PurgeExpired`/`EventLoopThread` |
| `include/core/INetworkEventBus.h` | ~44 | 网络事件总线接口：`StartNetwork`/`StopNetwork`/`RegisterNetworkableEvent`/`AsEventBus` |
| `src/core/NetworkEventBus/NetworkEventBusImpl.h` | ~124 | 网络实现头文件：`EventBusProxy`、UDP socket、Rate Limit |
| `src/core/NetworkEventBus/NetworkEventBusImpl.cpp` | ~400+ | 网络实现：Proxy 派发、UDP 多播收发、序列化/反序列化 |
| `src/core/NetworkEventBus/module.cpp` | 6 | 插件注册入口 |

---

## 5. 在当前系统中如何使用

### 5.1 定义自定义事件

```cpp
// MyEvents.h — 放在 include/ 目录下供多个插件共享
#pragma once
#include "AxPlug/AxEventBus.h"

// 编译期生成唯一ID
constexpr uint64_t EVENT_CAMERA_FRAME = AxPlug::HashEventId("Vision::CameraFrame");

// 事件载荷
class CameraFrameEvent : public AxPlug::AxEvent {
public:
    int cameraId;
    int width, height;
    void* dataPtr;  // 注意：跨DLL传递只用 POD 类型和指针
};
```

### 5.2 订阅事件

```cpp
class MyProcessor {
    AxPlug::EventConnectionPtr conn_;  // 必须作为成员变量保持存活！
public:
    void Start() {
        conn_ = Ax_GetEventBus()->Subscribe(EVENT_CAMERA_FRAME, [this](std::shared_ptr<AxPlug::AxEvent> evt) {
            auto frame = std::static_pointer_cast<CameraFrameEvent>(evt);
            ProcessFrame(frame->cameraId, frame->dataPtr, frame->width, frame->height);
        });
    }
    void Stop() {
        if (conn_) conn_->Disconnect();  // 可选：手动断开
    }
};
```

### 5.3 发布事件

```cpp
auto evt = std::make_shared<CameraFrameEvent>();
evt->cameraId = 1;
evt->width = 1920;
evt->height = 1080;
evt->dataPtr = imageBuffer;
evt->sender = this;  // 可选

// 同步派发（当前线程阻塞直到所有回调完成）
Ax_GetEventBus()->Publish(EVENT_CAMERA_FRAME, evt, AxPlug::DispatchMode::DirectCall);

// 或异步派发（立即返回，由后台线程处理）
Ax_GetEventBus()->Publish(EVENT_CAMERA_FRAME, evt, AxPlug::DispatchMode::Queued);
```

### 5.4 使用便捷 API (AxPlug 命名空间)

```cpp
#include "AxPlug/AxPlug.h"

// 等价于 Ax_GetEventBus()->Publish(...)
AxPlug::Publish(EVENT_CAMERA_FRAME, evt);

// 等价于 Ax_GetEventBus()->Subscribe(...)
auto conn = AxPlug::Subscribe(EVENT_CAMERA_FRAME, myCallback);

// 设置全局异常处理器
AxPlug::SetExceptionHandler([](const std::exception& e) {
    fprintf(stderr, "EventBus callback error: %s\n", e.what());
});
```

---

## 6. 扩展指南

### 6.1 添加新的内置事件

1. 在 `AxEventBus.h` 底部添加新的 `constexpr uint64_t EVENT_XXX = HashEventId("Core::XXX");`
2. 定义对应的 Payload 类继承自 `AxEvent`
3. 在框架适当位置调用 `Publish`

### 6.2 实现自定义事件总线

如果需要替换默认实现（如基于 Redis/Kafka 的分布式总线）：

1. 继承 `AxPlug::IEventBus` 实现三个虚方法：`Publish`、`Subscribe`、`SetExceptionHandler`
2. 在系统初始化后调用 `Ax_SetEventBus(yourBusPtr)` 替换全局总线
3. 参考 `NetworkEventBusImpl` 的 Proxy 模式：保存原始 `localBus_`，本地事件仍通过它派发

### 6.3 添加新的网络可序列化事件

1. 继承 `AxPlug::INetworkableEvent`（而非 `AxEvent`）
2. 实现 `Serialize()` 和 `Deserialize()` 方法
3. 在使用方的 `OnInit()` 中注册反序列化工厂：
   ```cpp
   auto netBus = AxPlug::GetService<AxPlug::INetworkEventBus>();
   netBus->RegisterNetworkableEvent(MY_EVENT_ID, []() { return std::make_shared<MyNetEvent>(); });
   ```

---

## 7. 维护注意事项

### 7.1 常见陷阱

| 陷阱 | 说明 | 解决方式 |
|------|------|----------|
| `EventConnectionPtr` 作为局部变量 | 订阅在函数结束时立即失效 | **必须**存为类成员变量 |
| 回调中阻塞耗时操作 | `DirectCall` 模式会阻塞发布者线程 | 耗时操作改用 `Queued` 模式 |
| 回调中再次 Publish 同一事件 | 可能导致递归调用栈溢出 | 使用 `Queued` 模式打断递归 |
| 跨 DLL 传递 `std::string` | ABI 不兼容导致崩溃 | Payload 字段用 `const char*` 或 POD |

### 7.2 性能调优参数

| 参数 | 位置 | 默认值 | 含义 |
|------|------|--------|------|
| `GC_INTERVAL` | `DefaultEventBus.h` | 64 | 每 N 次 Publish 触发一次死亡订阅清理 |
| `CALLBACK_WARN_THRESHOLD_US` | `DefaultEventBus.h` | 16000 (16ms) | 回调耗时超过此值输出 WARNING |
| `RATE_LIMIT_MAX` | `NetworkEventBusImpl.h` | 100 | 每个 eventId 每秒最大网络广播次数 |
| `RATE_LIMIT_WINDOW_MS` | `NetworkEventBusImpl.h` | 1000 | 限流窗口大小 (毫秒) |
| `MAX_PACKET_SIZE` | `NetworkEventBusImpl.h` | 65000 | UDP 包最大尺寸 |

### 7.3 调试技巧

- **回调耗时告警**：stderr 会输出 `[EventBus WARNING] Callback for eventId=0x... blocked bus for XXX us`
- **队列延迟告警**：stderr 会输出 `[EventBus WARNING] Queued event 0x... waited XXX us in queue`
- **Profiler 集成**：`Publish` 和 `DispatchDirect` 内置 `AX_PROFILE_SCOPE`，启用 Profiler 后可在 chrome://tracing 中查看时序
- **异常追踪**：设置 `SetExceptionHandler` 可以集中捕获所有回调异常
