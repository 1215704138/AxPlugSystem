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
11. [驱动插件 — TCP / UDP](#11-驱动插件--tcp--udp)
12. [构建与部署](#12-构建与部署)
13. [高级特性](#13-高级特性)
14. [使用注意事项](#14-使用注意事项)
15. [常见问题](#15-常见问题)

---

## 1. 框架简介

AxPlug 是一个工业级 C++17 插件框架，支持动态加载 DLL 插件并通过类型安全的模板 API 进行调用。

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
- **高性能并发** `shared_mutex` 读写锁 + `std::call_once` 无锁单例初始化
- **LIFO 安全卸载** 逆序调用 `OnShutdown()` 钩子
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

### 2.2 接口基类

所有插件接口必须继承 `IAxObject` 并使用 `AX_INTERFACE` 宏声明类型键：

```cpp
#include "AxPlug/IAxObject.h"

class IMath : public IAxObject {
    AX_INTERFACE(IMath)
public:
    virtual int Add(int a, int b) = 0;
    virtual int Sub(int a, int b) = 0;
};
```

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

## 11. 驱动插件 — TCP / UDP

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

## 12. 构建与部署

### 12.1 项目结构

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

### 12.2 部署要求

- `AxCore.dll` 必须在 exe 同目录
- 所有插件 DLL 默认放在 exe 同目录（可通过 `AxPlug::Init(dir)` 指定其他目录）
- 所有模块需使用相同的 MSVC 运行时（MD/MDd）

---

## 13. 高级特性

### 13.1 智能指针 (AxPtr)

```cpp
{
    AxPtr<IMath> math = AxPlug::CreateTool<IMath>();
    AxPtr<IMath> copy = math;   // use_count = 2
    copy.reset();               // use_count = 1
} // 自动调用 Destroy()
```

### 13.2 内置 Profiler

```cpp
AxPlug::ProfilerBegin("MyApp", "trace.json");

void MyFunction() {
    AX_PROFILE_FUNCTION();
    // ...
}

AxPlug::ProfilerEnd();
// 在 chrome://tracing 中打开 trace.json 可视化
```

### 13.3 异常处理与错误码

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

### 13.4 多线程并发

框架所有 API 线程安全（`shared_mutex` 读写锁），读操作并发无锁竞争。

### 13.5 编译期安全检查

```cpp
auto* obj = AxPlug::CreateTool<int>();          // 编译错误：T 必须继承自 IAxObject
struct Bad : IAxObject {};
auto* b = AxPlug::CreateTool<Bad>();            // 编译错误：缺少 ax_type_id 定义
```

### 13.6 跨平台工具

```cpp
#include "AxPlug/OSUtils.hpp"
bool ok = AxPlug::OSUtils::AtomicWriteFile("config.json", content);
```

---

## 14. 使用注意事项

- **禁止**直接 `delete` 插件对象，必须通过 `DestroyTool` 或智能指针释放
- `AX_PROFILE_SCOPE(name)` 的 `name` 必须是**静态字符串**
- 在 `OnShutdown()` 中**避免**调用 `GetService()`，应在 `OnInit()` 时缓存引用
- 网络插件的 socket 对象应在 DLL 卸载前释放

---

## 15. 常见问题

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
