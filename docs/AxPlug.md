# AxPlug 插件框架 · 使用手册 v1.0

## 目录

1. [框架简介](#1-框架简介)
2. [核心概念](#2-核心概念)
3. [快速开始](#3-快速开始)
4. [使用 Tool（工具插件）](#4-使用-tool工具插件)
5. [使用 Service（服务插件）](#5-使用-service服务插件)
6. [命名绑定（接口→多实现）](#6-命名绑定接口多实现)
7. [查询已加载插件](#7-查询已加载插件)
8. [开发插件](#8-开发插件)
9. [核心服务 — 日志](#9-核心服务--日志)
10. [核心服务 — 图像统一](#10-核心服务--图像统一)
11. [核心服务 — 事件总线 (Event Bus)](#11-核心服务--事件总线-event-bus)
12. [驱动插件 — TCP / UDP](#12-驱动插件--tcp--udp)
13. [构建与部署](#13-构建与部署)
14. [高级特性](#14-高级特性)
15. [API 参考手册 (AxPlug 命名空间)](#15-api-参考手册-axplug-命名空间)
16. [使用注意事项](#16-使用注意事项)
17. [常见问题](#17-常见问题)

---

## 1. 框架简介

AxPlug 是一个工业级 C++17 插件框架，支持动态加载 DLL 插件并通过类型安全的模板 API 进行调用。 此 v1.0 版本作为首个稳定投产的基石版本发布。

**核心特性：**

- 用户侧只需 `#include "AxPlug/AxPlug.h"` 一个头文件
- 编译期 FNV-1a typeId 作为类型键，跨 DLL 一致，O(1) 查找
- Tool（多实例）和 Service（命名单例）两种插件模型
- 声明式宏导出，一个 DLL 可导出一个或多个插件
- 插件 DLL 放在 exe 同目录即可自动发现
- **命名绑定**：同一接口支持多个实现，通过名称选择
- **智能指针** (`AxPtr<T>`) RAII 自动管理 + 原始指针手动管理双模式
- **内置 Profiler** 生成 Chrome trace.json，流式写入
- **跨模块异常保护** + 跨 DLL 线程局部错误存储
- **高性能无锁并发** `shared_mutex` 读写锁 + `std::call_once` 无死锁单例初始化
- **LIFO 安全卸载** 逆序调用 `OnShutdown()` 钩子，防悬空引用
- **静态析构防范 (SIOF Safe)** 独特的删除器守卫机制，绝对杜绝 DLL 卸载期 Use-After-Free
- **零死锁进程退出** 内置线程池及后台服务在退出/DLL Detach 时通过 Detach Fallback 斩断死锁根源
- **高吞吐无冲突 I/O** 提供原子级 TID+SEQ 文件写入能力，绝灭并发碰撞
- **ABI 版本检查** 确保插件兼容性
- **编译期 `static_assert` 防呆检查** 类型安全验证

---

## 2. 核心概念

### 2.1 两种插件类型

| 类型 | 生命周期 | 实例数 | 获取方式 | 释放方式 |
|------|----------|--------|----------|----------|
| **Tool** (智能指针) | RAII 自动 | 多实例 | `CreateTool<T>()` → `AxPtr<T>` | 作用域结束自动释放 / `DestroyTool(axptr)` |
| **Tool** (原始指针) | 用户管理 | 多实例 | `CreateToolRaw<T>()` → `T*` | `DestroyTool(ptr)` |
| **Service** | 框架托管 | 命名单例 | `GetService<T>(name)` | `ReleaseService<T>(name)` |

### 2.2 接口暴露与类型系统

所有对外暴露或插件间调用的接口必须继承自框架基类 `IAxObject`，这是框架识别多态和生命管理的前提。

**声明规范：** 必须使用 `AX_INTERFACE(InterfaceName)` 宏声明类型键，这使得框架在编译期为其生成一个唯一的 64 bit FNV-1a Hash 值（即类型戳），省去运行时的 RTTI 字符串比较。

```cpp
#include "AxPlug/IAxObject.h"

// IMath.h (作为公共接口对外暴露)
class IMath : public IAxObject {
    // 强制声明，使得 AxPlug<IMath> 知道这是哪个类型
    AX_INTERFACE(IMath)
public:
    virtual int Add(int a, int b) = 0;
    virtual int Sub(int a, int b) = 0;
};
```

**参数传递规范：**
由于插件系统横跨多个 DLL 模块，STL 容器直接穿越边界往往会导致 ABI 不兼容灾难。在涉及插件间或宿主发起的跨 DLL 接口调用时，**极度推荐：**
1. 传入纯 C 基本类型（`int`, `bool`, `double`, `const char*`等）。
2. 如果必须传递复杂长字符串或二进制序列，建议封装跨模块安全的字符串副本。
3. 返回实例对象务必返回由框架监管的智能指针（如 `AxPtr`）。

### 2.3 架构

```
宿主程序 (exe)
  │  #include "AxPlug/AxPlug.h"
  │  AxPlug::Init()
  │  AxPlug::CreateTool<IMath>()
  │  AxPlug::GetService<ILoggerService>("app")
  │
  ├── AxCore.dll              ← 插件管理器核心
  │     扫描目录、加载 DLL、维护单例缓存
  │
  ├── MathPlugin.dll          ← Tool 插件
  ├── LoggerPlugin.dll        ← Service 插件
  ├── TcpClientPlugin.dll     ← 多插件 DLL (默认 + boost)
  └── ...
```

---

## 3. 快速开始

### 3.1 环境要求

- C++17，CMake 3.15+
- MSVC 2019+（Windows）

### 3.2 最小示例

```cpp
#include "AxPlug/AxPlug.h"
#include "business/IMath.h"
#include "core/LoggerService.h"

int main() {
    AxPlug::Init();

    auto* logger = AxPlug::GetService<ILoggerService>("main");
    logger->Info("程序启动");

    {
        auto math = AxPlug::CreateTool<IMath>();
        int result = math->Add(10, 20);
        logger->InfoFormat("10 + 20 = %d", result);
    }

    AxPlug::ReleaseService<ILoggerService>("main");
    return 0;
}
```

### 3.3 构建

```bash
# Debug 版 SDK
scripts\build_debug_no_test.bat

# Release 版 SDK
scripts\build_release_no_test.bat
```

手动构建：

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
cmake --install build --config Debug --prefix publish
```

---

## 4. 使用 Tool（工具插件）

Tool 是多实例插件，每次调用 `CreateTool` 都创建独立的新对象。

### 4.1 智能指针模式（推荐）

```cpp
auto math = AxPlug::CreateTool<IMath>();
int sum = math->Add(10, 20);

AxPtr<IMath> copy = math;  // use_count = 2
copy.reset();              // use_count = 1
// 离开作用域时自动释放
```

### 4.2 原始指针模式

```cpp
auto* math = AxPlug::CreateToolRaw<IMath>();
int sum = math->Add(10, 20);
AxPlug::DestroyTool(math);  // 必须手动销毁
```

---

## 5. 使用 Service（服务插件）

Service 是命名单例，同一接口 + 同一名称始终返回同一实例。

```cpp
auto* logger = AxPlug::GetService<ILoggerService>("app");
auto* same = AxPlug::GetService<ILoggerService>("app");  // same == logger
auto* other = AxPlug::GetService<ILoggerService>("debug"); // other != logger

AxPlug::ReleaseService<ILoggerService>("app");
AxPlug::ReleaseService<ILoggerService>("debug");
```

省略名称使用默认全局单例：

```cpp
auto* logger = AxPlug::GetService<ILoggerService>();
AxPlug::ReleaseService<ILoggerService>();
```

---

## 6. 命名绑定（接口→多实现）

同一接口存在多个实现时，通过命名绑定选择具体实现。

```cpp
auto server = AxPlug::CreateTool<ITcpServer>();          // 默认实现
auto boostServer = AxPlug::CreateTool<ITcpServer>("boost"); // 命名实现
```

**内置命名绑定：**

| 接口 | 默认实现 | 命名实现 |
|------|----------|----------|
| `ITcpServer` | TcpServer (winsock) | `"boost"` → BoostTcpServer |
| `ITcpClient` | TcpClient (winsock) | `"boost"` → BoostTcpClient |
| `IUdpSocket` | UdpSocket (winsock) | `"boost"` → BoostUdpSocket |

---

## 7. 查询已加载插件

```cpp
int count = AxPlug::GetPluginCount();
for (int i = 0; i < count; i++) {
    auto info = AxPlug::GetPluginInfo(i);
    printf("%s | %s | %s | %s\n", info.fileName, info.interfaceName, info.isTool ? "Tool" : "Service", info.isLoaded ? "OK" : "FAIL");
}

// 查询特定接口的所有实现
auto impls = AxPlug::FindImplementations<ITcpServer>();
```

---

## 8. 开发插件

所有插件统一使用 `AX_BEGIN_PLUGIN_MAP()` / `AX_END_PLUGIN_MAP()` 声明式宏导出。

### 8.1 定义接口

```cpp
// include/business/IMath.h
#pragma once
#include "AxPlug/IAxObject.h"

class IMath : public IAxObject {
    AX_INTERFACE(IMath)
public:
    virtual int Add(int a, int b) = 0;
    virtual int Sub(int a, int b) = 0;
};
```

### 8.2 实现类

```cpp
// src/business/MathPlugin/include/MathPlugin.h
#pragma once
#include "business/IMath.h"

class CMath : public IMath {
public:
    int Add(int a, int b) override;
    int Sub(int a, int b) override;
protected:
    void Destroy() override { delete this; }
};
```

### 8.3 导出插件

```cpp
// src/business/MathPlugin/src/module.cpp
#include "MathPlugin.h"
#include "AxPlug/AxPluginExport.h"

AX_BEGIN_PLUGIN_MAP()
    AX_PLUGIN_TOOL(CMath, IMath)
AX_END_PLUGIN_MAP()
```

**多插件 DLL 导出：**

```cpp
AX_BEGIN_PLUGIN_MAP()
    AX_PLUGIN_TOOL(TcpServer, ITcpServer)
    AX_PLUGIN_TOOL_NAMED(BoostTcpServer, ITcpServer, "boost")
AX_END_PLUGIN_MAP()
```

**导出宏一览：**

| 宏 | 用途 |
|------|------|
| `AX_PLUGIN_TOOL(TClass, InterfaceType)` | Tool 插件（默认实现） |
| `AX_PLUGIN_TOOL_NAMED(TClass, InterfaceType, ImplName)` | Tool 插件（命名实现） |
| `AX_PLUGIN_SERVICE(TClass, InterfaceType)` | Service 插件（默认实现） |
| `AX_PLUGIN_SERVICE_NAMED(TClass, InterfaceType, ImplName)` | Service 插件（命名实现） |

### 8.4 CMakeLists.txt

```cmake
add_library(MathPlugin SHARED src/MathPlugin.cpp src/module.cpp)
target_include_directories(MathPlugin PRIVATE include ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(MathPlugin PRIVATE AxInterface)
target_compile_definitions(MathPlugin PRIVATE AX_PLUGIN_EXPORTS)
```

### 8.5 Service 插件的生命周期钩子

Service 插件可重写 `OnInit()` 和 `OnShutdown()`：

```cpp
class MyService : public IMyService {
public:
    void OnInit() override { /* 初始化逻辑，可调用其他 Service */ }
    void OnShutdown() override { /* 清理逻辑，框架退出时逆序调用 */ }
protected:
    void Destroy() override { delete this; }
};
```

---

## 9. 核心服务 — 日志

```cpp
#include "core/LoggerService.h"

auto* logger = AxPlug::GetService<ILoggerService>("app");

logger->Info("消息");
logger->Warn("警告");
logger->Error("错误");
logger->Debug("调试");
logger->InfoFormat("用户 %s 登录，ID=%d", "张三", 42);

logger->SetLevel(LogLevel::Debug);
logger->EnableConsoleOutput(true);

AxPlug::ReleaseService<ILoggerService>("app");
```

**日志级别**：`Trace` → `Debug` → `Info` → `Warn` → `Error` → `Critical`

---

## 10. 核心服务 — 图像统一

```cpp
#include "core/IImageUnifyService.h"

auto* svc = AxPlug::GetService<IImageUnifyService>();

uint64_t fid = svc->SubmitFrame(data, w, h, PixelFormat::U8_C3);
ImageDescriptor view = svc->GetView(fid, MemoryLayout::Planar);
// 使用 view.data / view.R() / view.G() / view.B() ...
svc->ReleaseView(fid, view.data);
svc->RemoveFrame(fid);

AxPlug::ReleaseService<IImageUnifyService>();
```

详见 [ImageUnifyService_Dev.md](ImageUnifyService_Dev.md)。

---

## 11. 核心服务 — 事件总线 (Event Bus)

v1.0 引入了极其轻量的框架级事件总线 `IEventBus` 以及可选支持跨节点 UDP 组播互联的 `INetworkEventBus` 插件。

详细使用参见：
- [EventBus.md](EventBus.md) (事件总线常规使用手册)
- [EventBus_Dev.md](EventBus_Dev.md) (事件总线高级定制开发)

---

## 12. 驱动插件 — TCP / UDP

所有网络驱动接口支持命名绑定，默认为 winsock 实现，可通过 `"boost"` 获取 Boost.Asio 实现。

```cpp
// TCP 客户端
auto client = AxPlug::CreateTool<ITcpClient>();
client->SetTimeout(3000);
client->Connect("127.0.0.1", 8080);
client->SendString("Hello");

// TCP 服务器
auto server = AxPlug::CreateTool<ITcpServer>();
server->SetMaxConnections(10);
server->Listen(8080);

// UDP
auto udp = AxPlug::CreateTool<IUdpSocket>();
udp->Bind(9000);
udp->SendStringTo("127.0.0.1", 9001, "Hello UDP");
```

---

## 13. 构建与部署

### 13.1 项目结构

```
AxPlugSystem/
├── include/              公共接口头文件
│   ├── AxPlug/           框架头文件 (AxPlug.h, IAxObject.h, AxPluginExport.h, AxProfiler.h, AxException.h, OSUtils.hpp)
│   ├── business/         业务接口 (IMath.h)
│   ├── core/             核心服务接口 (LoggerService.h, IImageUnifyService.h)
│   └── driver/           驱动接口 (ITcpClient.h, ITcpServer.h, IUdpSocket.h)
├── src/
│   ├── AxCore/           框架核心 DLL
│   ├── business/         业务插件实现
│   ├── core/             核心服务插件实现
│   └── driver/           驱动插件实现
├── test/                 测试程序
├── scripts/              构建脚本
├── deps/                 第三方依赖 (spdlog, boost)
├── docs/                 文档
└── CMakeLists.txt        根构建文件
```

### 13.2 部署要求

- `AxCore.dll` 必须在 exe 同目录
- 所有插件 DLL 默认放在 exe 同目录（可通过 `AxPlug::Init(dir)` 指定其他目录）
- 所有模块需使用相同的 MSVC 运行时（MD/MDd）

---

## 14. 高级特性

### 14.1 智能指针 (AxPtr)

```cpp
{
    AxPtr<IMath> math = AxPlug::CreateTool<IMath>();
    AxPtr<IMath> copy = math;   // use_count = 2
    copy.reset();               // use_count = 1
} // 自动调用 Destroy()
```

### 14.2 内置 Profiler

```cpp
AxPlug::ProfilerBegin("MyApp", "trace.json");

void MyFunction() {
    AX_PROFILE_FUNCTION();
    // ...
}

AxPlug::ProfilerEnd();
// 在 chrome://tracing 中打开 trace.json 可视化
```

### 14.3 异常处理与错误码

```cpp
// 检查错误
auto* obj = AxPlug::CreateToolRaw<IMath>();
if (!obj && AxPlug::HasError()) {
    printf("错误: %s\n", AxPlug::GetLastError());
    AxPlug::ClearLastError();
}

// noexcept 错误码 API (适用于析构/清理场景)
auto [service, error] = AxPlug::TryGetService<ILoggerService>("main");
if (error != AxInstanceError::kSuccess) { /* 处理错误 */ }
```

### 14.4 多线程并发

框架所有 API 线程安全（`shared_mutex` 读写锁），读操作并发无锁竞争。

### 14.5 编译期安全检查

```cpp
auto* obj = AxPlug::CreateTool<int>();          // 编译错误：T 必须继承自 IAxObject
struct Bad : IAxObject {};
auto* b = AxPlug::CreateTool<Bad>();            // 编译错误：缺少 ax_type_id 定义
```

### 14.6 跨平台工具

```cpp
#include "AxPlug/OSUtils.hpp"
bool ok = AxPlug::OSUtils::AtomicWriteFile("config.json", content);
```

---

## 15. API 参考手册 (AxPlug 命名空间)

以下是 `AxPlug` 命名空间中暴露的所有核心 API 详细说明。这些 API 全部是线程安全的，确保了在多线程环境下的调用可靠性。

### 15.1 初始化阶段 (Initialization)

#### `void Init(const char *pluginDir = "")`
- **功能**: 初始化插件系统并从指定目录加载所有插件。
- **参数**:
  - `pluginDir` (可选): 插件扫描目录。如果为空（默认），则自动侦测并加载 exe 所在目录中的插件 DLL。
- **说明**: 宿主程序运行早期必须首先调用此函数，或者使用更便捷的全局初始化宏 `AX_HOST_INIT()`。

### 15.2 Tool 插件生命周期管理 (Tool API)

#### `template <typename T> std::shared_ptr<T> CreateTool()`
- **功能**: 创建一个具有框架默认实现的 Tool 插件实例。
- **返回**: 托管该实例生命周期的标准智能指针。离开作用域自动销毁。失败时返回 `nullptr`。

#### `template <typename T> std::shared_ptr<T> CreateTool(const char *implName)`
- **功能**: 根据命名绑定机制，创建一个指定名称实现的 Tool 插件实例。
- **参数**:
  - `implName`: 具体的实现类绑定的名称（如网络驱动中的 `"boost"`）。

#### `template <typename T> void DestroyTool(std::shared_ptr<T> &tool)`
- **功能**: 显式重置并通知框架销毁管理 Tool 的智能指针，主要用于打破循环引用或提前释放内存。

#### `template <typename T> T *CreateToolRaw()`
- **功能**: 仅获取具备默认实现的 Tool 插件的裸指针实例（非托管方式）。
- **说明**: 要求使用者必须负责在其用尽后显式配对调用 `DestroyTool(ptr)` 释放内存以防止内存泄漏。

#### `template <typename T> T *CreateToolRaw(const char *implName)`
- **功能**: 通过命名绑定方式，获取创建的 Tool 插件的裸指针实例。

#### `void DestroyTool(IAxObject *tool)`
- **功能**: 用于主动销毁由任意 `CreateToolRaw` 系函数分配返回的纯虚基类裸指针插件实例。

### 15.3 Service 单例服务管理 (Service API)

#### `template <typename T> std::shared_ptr<T> GetService(const char *name = "")`
- **功能**: 懒加载式获取系统中运行的一个 Service 单例。同一名称、同类接口将始终返回统一的实例引用。
- **参数**: 
  - `name` (可选): 单例绑定的命名区分符。缺省传空字符串 `""` 则表示请求获取同类型最基础的全局单例。
- **返回**: 框架内含 UAF（释放后使用）和 SIOF（静态初始顺序死锁）等自动守卫的高安全机制包装型智能指针。

#### `template <typename T> void ReleaseService(const char *name = "")`
- **功能**: 主动干预单例释放（内部使其全局强引用计数减 1。仅当该类型单量没有任何外部调用栈引用时，则其实例才将彻底析构卸载）。

#### `template <typename T> std::pair<std::shared_ptr<T>, AxInstanceError> TryGetService(const char *name = "") noexcept`
- **功能**: [无异常降级版] 尝试安全平缓获取服务，获取失败时函数决不抛弃任何 C++ 异常，而是直接返回故障错误码机制。
- **返回**: 返回涵盖服务实例与 `AxInstanceError` 标准状态对结构。最佳使用场景存在于深层析构函数或其他被 `noexcept` 隔离保护的应用清理安全路径里。

#### `template <typename T> T *GetServiceRaw(const char *name = "")`
- **功能**: [降权兼容模式] 退化以获取单例的直接裸结构指针。
- **注意**: 由于此模式完全丧失对 UAF 内存安全的感知机制保障，仅保留供老版本兼容集成期使用，并不主张于新建重点业务组件代码采用。

### 15.4 运行时状态与自省 (Query & Introspection API)

#### `int GetPluginCount()`
- **功能**: 获取当前框架实例在本地扫描记载并启动注册妥当的所有插件模块及实现的绝对数量总和。

#### `AxPluginQueryInfo GetPluginInfo(int index)`
- **功能**: 按递增顺序获取任意确切单个插件模块详情。返回包涵 `fileName` (文件名), `interfaceName` (接口), `isTool` (是否独立实例), `isLoaded` (启动情况) 之信息统集结构体。

#### `template<typename T> std::vector<AxPluginQueryInfo> FindImplementations()`
- **功能**: 针对特定请求类型的运行时自省。一键反向定位查询出实现类与系统挂接绑定成功的各类实现版本分布。
- **返回**: 可用实现包体记录集合。特别适宜应用界面选项卡动态呈列该算力下能提供的一切可选驱动实现方案。

### 15.5 底层性能调优与检修诊断 (Profiler & Error API)

#### `void ProfilerBegin(const char *name = "AxPlug", const char *filepath = "trace.json")`
- **功能**: 控制初始化激活底层侵入式性能测量追踪系统，把探针数据流向默认 Chrome Trace (.json) 指定格式记录文件。

#### `void ProfilerEnd()`
- **功能**: 停止测量监听器并把积压在运行库日志缓存全盘原子刷写至磁盘目标中完结此会话侦探。

#### `const char *GetLastError()`
- **功能**: 以 TLS（线程局部存储）的形式索求该线程池工作环境下遭受的框架级别最后已知错误现场根源异常说明摘要。

#### `void ClearLastError()`
- **功能**: 精准洗除本单独调用栈引发出的错误残留报警标志。

#### `bool HasError()`
- **功能**: 用于超快速鉴定最近操作序列调用到底触发底层挂钩崩溃状态保护没有。

### 15.6 全局事件协调总线 (Event Bus API)

#### `IEventBus *GetEventBus()`
- **功能**: 取出当前主导执行跨对象发布与响应运转的顶层总线核心实例句柄引用。

#### `void SetEventBus(IEventBus *bus)`
- **功能**: (内核覆盖高级操作): 指令让底层放弃内部自旋默认事件分发总线，转接路由流量交给指定外挂网络分布或集群网格驱动模块。

#### `void Publish(uint64_t eventId, std::shared_ptr<AxEvent> payload, DispatchMode mode = DispatchMode::DirectCall)`
- **功能**: 给总线的干路频道投入一段附着特定消息类型 `eventId` 并携带内容负荷 `payload` 之通讯信号。配合不同步态 `mode` 发挥同步阻滞（DirectCall）、队尾排班（Queued）乃至投递完待机监听等丰富行为序列。

#### `EventConnectionPtr Subscribe(uint64_t eventId, EventCallback callback, void *specificSender = nullptr)`
- **功能**: 要求订阅并响应关注的类目标 ID (`eventId`) 动作事件。一旦成功，会回馈一个遵循 RAII 惯例的保活指针令牌 (`EventConnectionPtr`)——令牌对象在生存周期内代表你始终保持在线侦听活跃状态；出界或析构其就意味向总线完成注销安全释放操作。此外可透过可选的 `specificSender` 实质收缩仅监看只源于单独确定对等体发信才有所触警行动的精确过滤网络机制。

---

## 16. 使用注意事项

- **禁止**直接 `delete` 插件对象，必须通过 `DestroyTool` 或智能指针释放
- `AX_PROFILE_SCOPE(name)` 的 `name` 必须是**静态字符串**
- 在 `OnShutdown()` 中**避免**调用 `GetService()`，应在 `OnInit()` 时缓存引用
- 网络插件的 socket 对象应在 DLL 卸载前释放

---

## 17. 常见问题

| 现象 | 解决方案 |
|------|----------|
| `CreateTool` 返回 nullptr | 检查插件 DLL 是否在 exe 目录，接口是否有 `AX_INTERFACE` 宏，检查 `AxPlug::GetLastError()` |
| 命名绑定返回 nullptr | 检查 `implName` 拼写，确认插件使用了 `AX_PLUGIN_TOOL_NAMED` 宏 |
| 链接错误：找不到 `Ax_*` 函数 | CMake 中添加 `target_link_libraries(... AxCore)` |
| Service 返回旧实例 | 先 `ReleaseService<T>(name)` 再重新 `GetService` |
| DLL 加载失败 | 确保所有模块使用相同的 MSVC 运行时 (MD/MDd) |
| 接口匹配失败 | 确认接口头文件中有 `AX_INTERFACE(类名)` 宏 |
| Profiler 无输出 | 确认调用了 `ProfilerBegin()` 和 `ProfilerEnd()` |

---

*AxPlug v1.0 · 使用手册*
