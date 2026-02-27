# AxPlug 插件框架 · 使用手册

## 目录

1. [框架简介](#1-框架简介)
2. [核心概念](#2-核心概念)
3. [快速开始](#3-快速开始)
4. [使用 Tool（工具插件）](#4-使用-tool工具插件)
5. [使用 Service（服务插件）](#5-使用-service服务插件)
6. [命名绑定（接口→多实现）](#6-命名绑定接口多实现)
7. [查询已加载插件](#7-查询已加载插件)
8. [开发 Tool 插件](#8-开发-tool-插件)
9. [开发多插件 DLL](#9-开发多插件-dll)
10. [开发 Service 插件](#10-开发-service-插件)
11. [核心服务 — 日志](#11-核心服务--日志)
12. [核心服务 — 图像统一](#12-核心服务--图像统一)
13. [驱动插件 — TCP / UDP](#13-驱动插件--tcp--udp)
14. [构建与部署](#14-构建与部署)
15. [智能指针 / Profiler / 异常处理 / 并发](#15-智能指针--profiler--异常处理--并发)
16. [使用注意事项](#16-使用注意事项)
17. [常见问题](#17-常见问题)

---

## 1. 框架简介

AxPlug 是一个现代化的工业级 C++17 插件框架，支持动态加载 DLL 插件并通过类型安全的模板 API 进行调用。

**核心特性：**

- 用户侧只需 `#include "AxPlug/AxPlug.h"` 一个头文件
- 基于 C++ 接口类型自动匹配，无需手写字符串 ID
- 编译期字符串字面量 + FNV-1a typeId 作为类型键，保证跨 DLL 一致
- Tool（多实例）和 Service（命名单例）两种插件模型
- 插件 DLL 放在 exe 同目录即可自动发现
- 一个 DLL 可导出多个插件，声明式宏，完全向后兼容
- **命名绑定**：同一接口支持多个实现，通过名称选择（如 `CreateTool<ITcpServer>("boost")`）
- **智能指针** (`AxPtr<T>` / `shared_ptr`) 自动引用计数 + 手动 `DestroyTool` 双模式
- **内置 Profiler** 生成 Chrome trace.json，流式写入防止内存溢出
- **跨模块异常保护** `AxExceptionGuard` + 跨 DLL 线程局部错误存储（通过 AxCore C API 路由）
- **高性能并发** `shared_mutex` 读写锁 + typeId O(1) 热路径

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
    AX_INTERFACE(IMath)   // 编译期类型键，保证跨 DLL 一致
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
  ├── TcpClientPlugin.dll     ← Tool 插件
  └── ...
```

---

## 3. 快速开始

### 3.1 环境要求

- C++17，CMake 3.10+
- MSVC 2019+（Windows）

### 3.2 最小示例

```cpp
#include "AxPlug/AxPlug.h"
#include "business/IMath.h"
#include "core/LoggerService.h"

int main() {
    // 初始化（自动扫描 exe 目录下所有插件 DLL）
    AxPlug::Init();

    // 使用 Service
    auto* logger = AxPlug::GetService<ILoggerService>("main");
    logger->Info("程序启动");

    // v3: 使用智能指针 Tool（RAII 自动释放）
    {
        auto math = AxPlug::CreateTool<IMath>();  // 返回 AxPtr<IMath>
        int result = math->Add(10, 20);
        logger->InfoFormat("10 + 20 = %d", result);
    } // math 自动释放，无需手动 DestroyTool

    // 向后兼容: 原始指针 Tool（手动释放）
    auto* math2 = AxPlug::CreateToolRaw<IMath>();
    math2->Sub(30, 10);
    AxPlug::DestroyTool(math2);

    // 释放服务
    AxPlug::ReleaseService<ILoggerService>("main");
    return 0;
}
```

### 3.3 构建

### 3.3 构建

**推荐方式（一键构建 SDK）：**

```bash
# 生成 Debug版 SDK (publish/debug)
scripts\build_publish_debug.bat

# 生成 Release版 SDK (publish/release)
scripts\build_publish_release.bat
```

**手动构建：**

```bash
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Debug
cmake --install . --config Debug --prefix ../publish
```

---

## 4. 使用 Tool（工具插件）

Tool 是多实例插件，每次调用 `CreateTool` 都创建独立的新对象。

### 4.1 智能指针模式（v3 推荐）

```cpp
// 创建 - 返回 AxPtr<T> (shared_ptr)
auto math = AxPlug::CreateTool<IMath>();

// 使用
int sum = math->Add(10, 20);

// 引用计数
AxPtr<IMath> copy = math;  // use_count = 2
copy.reset();              // use_count = 1

// 自动释放：离开作用域时 shared_ptr 析构 → Destroy()
// 或显式释放：
AxPlug::DestroyTool(math); // math 变为 nullptr
```

### 4.2 原始指针模式（向后兼容）

```cpp
// 创建
auto* math = AxPlug::CreateToolRaw<IMath>();

// 使用
int sum = math->Add(10, 20);

// 必须手动销毁
AxPlug::DestroyTool(math);
```

可同时创建多个独立实例：

```cpp
// 智能指针版本 - 容器管理，自动释放
{
    std::vector<AxPtr<IMath>> tools;
    for (int i = 0; i < 5; i++) {
        tools.push_back(AxPlug::CreateTool<IMath>());
    }
    // 使用 tools...
} // 全部自动释放
```

---

## 5. 使用 Service（服务插件）

Service 是命名单例，同一接口 + 同一名称始终返回同一实例。

```cpp
// 获取（首次调用自动创建）
auto* logger = AxPlug::GetService<ILoggerService>("app");

// 再次获取同名 → 同一实例
auto* same = AxPlug::GetService<ILoggerService>("app");
// same == logger

// 不同名称 → 不同实例
auto* other = AxPlug::GetService<ILoggerService>("debug");
// other != logger

// 释放
AxPlug::ReleaseService<ILoggerService>("app");
AxPlug::ReleaseService<ILoggerService>("debug");
```

省略名称时使用默认全局单例：

```cpp
auto* logger = AxPlug::GetService<ILoggerService>();   // 默认单例
AxPlug::ReleaseService<ILoggerService>();               // 释放默认单例
```

---

## 6. 命名绑定（接口→多实现）

当同一接口存在多个实现时（如 `ITcpServer` 有 winsock 版和 boost 版），可通过命名绑定选择具体实现。

### 6.1 使用方式

```cpp
#include "driver/ITcpServer.h"

// 获取默认实现（第一个注册的）
auto server = AxPlug::CreateTool<ITcpServer>();

// 获取指定名称的实现
auto boostServer = AxPlug::CreateTool<ITcpServer>("boost");

// 不存在的名称返回 nullptr
auto invalid = AxPlug::CreateTool<ITcpServer>("nonexistent"); // nullptr
```

### 6.2 注册命名实现（插件开发者）

在 `module.cpp` 中使用 `_NAMED` 宏：

```cpp
AX_BEGIN_PLUGIN_MAP()
    AX_PLUGIN_TOOL(TcpServer, ITcpServer)                       // 默认实现
    AX_PLUGIN_TOOL_NAMED(BoostTcpServer, ITcpServer, "boost")   // 命名实现
AX_END_PLUGIN_MAP()
```

单插件 DLL 版本：

```cpp
AX_EXPORT_TOOL_NAMED(BoostTcpServer, ITcpServer, "boost")
```

### 6.3 内置命名绑定

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
    printf("%s | %s | %s | %s\n",
        info.fileName,
        info.interfaceName,
        info.isTool ? "Tool" : "Service",
        info.isLoaded ? "OK" : "FAIL");
}
```

---

## 8. 开发 Tool 插件

### 7.1 定义接口

放在 `include/` 目录，供使用者和实现者共同引用。

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

### 7.2 实现

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

```cpp
// src/business/MathPlugin/src/MathPlugin.cpp
#include "MathPlugin.h"
int CMath::Add(int a, int b) { return a + b; }
int CMath::Sub(int a, int b) { return a - b; }
```

### 7.3 导出（一行）

```cpp
// src/business/MathPlugin/src/module.cpp
#include "MathPlugin.h"
#include "AxPlug/AxPluginExport.h"

AX_EXPORT_TOOL(CMath, IMath)
```

### 7.4 CMakeLists.txt

```cmake
add_library(MathPlugin SHARED
    src/MathPlugin.cpp
    src/module.cpp
)
target_include_directories(MathPlugin PRIVATE
    include
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(MathPlugin PRIVATE AxInterface)
target_compile_definitions(MathPlugin PRIVATE AX_PLUGIN_EXPORTS)
set_target_properties(MathPlugin PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
```

---

## 9. 开发多插件 DLL

一个 DLL 可以导出多个插件，使用声明式宏，清晰易用。

### 9.1 使用场景

- **功能相关的插件组合**：如数学库同时提供基础计算和高级计算
- **减少 DLL 数量**：简化部署，减少文件数量
- **共享内部实现**：多个插件可共享内部辅助类或资源

### 9.2 开发步骤

**1. 定义多个接口**

```cpp
// include/business/IMath.h
class IMath : public IAxObject {
    AX_INTERFACE(IMath)
public:
    virtual int Add(int a, int b) = 0;
    virtual int Sub(int a, int b) = 0;
};

// include/business/ICalculator.h
class ICalculator : public IAxObject {
    AX_INTERFACE(ICalculator)
public:
    virtual double Calculate(const char* expression) = 0;
};
```

**2. 实现多个插件类**

```cpp
// src/business/MathSuite/include/MathSuite.h
class CMath : public IMath {
public:
    int Add(int a, int b) override { return a + b; }
    int Sub(int a, int b) override { return a - b; }
protected:
    void Destroy() override { delete this; }
};

class CCalculator : public ICalculator {
public:
    double Calculate(const char* expression) override {
        // 实现表达式计算...
        return 0.0;
    }
protected:
    void Destroy() override { delete this; }
};
```

**3. 导出多插件（声明式）**

```cpp
// src/business/MathSuite/src/module.cpp
#include "MathSuite.h"
#include "AxPlug/AxPluginExport.h"

AX_BEGIN_PLUGIN_MAP()
    AX_PLUGIN_TOOL(CMath, IMath)
    AX_PLUGIN_TOOL(CCalculator, ICalculator)
AX_END_PLUGIN_MAP()
```

### 9.3 宏说明

- `AX_BEGIN_PLUGIN_MAP()` 开始多插件声明
- `AX_PLUGIN_TOOL(TClass, InterfaceType)` 声明 Tool 插件（默认实现）
- `AX_PLUGIN_TOOL_NAMED(TClass, InterfaceType, ImplName)` 声明命名 Tool 插件
- `AX_PLUGIN_SERVICE(TClass, InterfaceType)` 声明 Service 插件（默认实现）
- `AX_PLUGIN_SERVICE_NAMED(TClass, InterfaceType, ImplName)` 声明命名 Service 插件
- `AX_END_PLUGIN_MAP()` 结束声明

### 9.4 使用方式

用户侧 API 完全不变：

```cpp
// 创建不同插件实例
auto* math = AxPlug::CreateTool<IMath>();
auto* calc = AxPlug::CreateTool<ICalculator>();

// 查询插件
int count = AxPlug::GetPluginCount();
for (int i = 0; i < count; i++) {
    auto info = AxPlug::GetPluginInfo(i);
    printf("插件 %d: %s (%s)\n", i, info.interfaceName, info.fileName);
}
// 输出：
// 插件 0: IMath (MathSuite.dll)
// 插件 1: ICalculator (MathSuite.dll)
```

### 9.5 向后兼容

- **现有单插件 DLL**：无需修改，继续使用 `AX_EXPORT_TOOL` / `AX_EXPORT_SERVICE`
- **用户侧代码**：无需修改，API 保持不变
- **查询 API**：每个插件独立索引，多插件 DLL 占多个索引

---

## 10. 开发 Service 插件

与 Tool 唯一的区别是导出宏使用 `AX_EXPORT_SERVICE`：

```cpp
// module.cpp
#include "LoggerService.h"
#include "AxPlug/AxPluginExport.h"

AX_EXPORT_SERVICE(LoggerService, ILoggerService)
```

接口定义和实现方式完全相同，`Destroy()` 固定写法 `{ delete this; }`。

---

## 11. 核心服务 — 日志

```cpp
#include "core/LoggerService.h"

auto* logger = AxPlug::GetService<ILoggerService>("app");

// 日志输出
logger->Info("消息");
logger->Warn("警告");
logger->Error("错误");
logger->Debug("调试");

// 格式化
logger->InfoFormat("用户 %s 登录，ID=%d", "张三", 42);

// 配置
logger->SetLevel(LogLevel::Debug);
logger->EnableConsoleOutput(true);
logger->SetTimestampFormat("detailed");  // detailed | simple | none

// 文件日志
const char* logFile = logger->GetLogFile();
logger->Flush();

// 释放
AxPlug::ReleaseService<ILoggerService>("app");
```

**日志级别**：`Trace` → `Debug` → `Info` → `Warn` → `Error` → `Critical`

---

## 12. 核心服务 — 图像统一

```cpp
#include "core/IImageUnifyService.h"

auto* imageService = AxPlug::GetService<IImageUnifyService>();

// 注册插件、获取视图、释放资源
imageService->RegisterPlugin(pluginInfo);
auto* view = imageService->GetImageView(frameId, requirement);
imageService->ReleaseView(frameId, view);

AxPlug::ReleaseService<IImageUnifyService>();
```

---

## 13. 驱动插件 — TCP / UDP

所有网络驱动接口支持命名绑定，默认为 winsock 实现，可通过 `"boost"` 获取 Boost.Asio 实现。

### 13.1 TCP 客户端

```cpp
#include "driver/ITcpClient.h"

// 默认实现 (winsock)
auto client = AxPlug::CreateTool<ITcpClient>();
// Boost 实现
auto boostClient = AxPlug::CreateTool<ITcpClient>("boost");

client->SetTimeout(3000);
client->Connect("127.0.0.1", 8080);
client->SendString("Hello");

char buf[1024]; size_t len;
client->ReceiveString(buf, sizeof(buf), len);
client->Disconnect();
```

### 13.2 TCP 服务器

```cpp
#include "driver/ITcpServer.h"

// 默认实现 (winsock)
auto server = AxPlug::CreateTool<ITcpServer>();
// Boost 实现
auto boostServer = AxPlug::CreateTool<ITcpServer>("boost");

server->SetMaxConnections(10);
server->Listen(8080);
// ... 接受连接、收发数据 ...
server->StopListening();
```

### 13.3 UDP 套接字

```cpp
#include "driver/IUdpSocket.h"

auto udp = AxPlug::CreateTool<IUdpSocket>();          // 默认
auto boostUdp = AxPlug::CreateTool<IUdpSocket>("boost"); // Boost

udp->Bind(9000);
udp->SendStringTo("127.0.0.1", 9001, "Hello UDP");

char buf[1024]; size_t len;
char host[64]; int port;
udp->ReceiveStringFrom(host, sizeof(host), port, buf, sizeof(buf), len);
udp->Unbind();
```

---

## 14. 构建与部署

### 13.1 项目结构

```
AxPlug/
├── include/              公共接口头文件
│   ├── AxPlug/           框架头文件（AxPlug.h, IAxObject.h, AxPluginExport.h）
│   ├── business/         业务接口（IMath.h）
│   ├── core/             核心服务接口（LoggerService.h, IImageUnifyService.h）
│   └── driver/           驱动接口（ITcpClient.h, ITcpServer.h, IUdpSocket.h）
├── src/
│   ├── AxCore/           框架核心 DLL
│   ├── business/         业务插件实现
│   ├── core/             核心服务插件实现
│   └── driver/           驱动插件实现
├── test/                 测试程序
├── CMakeLists.txt        根构建文件
└── docs/                 文档
```

### 13.2 部署要求

- `AxCore.dll` 必须在 exe 同目录
- 所有插件 DLL 默认放在 exe 同目录（可通过 `AxPlug::Init(dir)` 指定其他目录）
- 所有模块需使用相同的 MSVC 运行时（MD/MDd）

---

## 15. 智能指针 / Profiler / 异常处理 / 并发

### 15.1 智能指针 (AxPtr)

```cpp
// 自动引用计数 - 无需手动 DestroyTool
{
    AxPtr<IMath> math = AxPlug::CreateTool<IMath>();
    AxPtr<IMath> copy = math;              // use_count = 2
    std::cout << math.use_count();         // 输出 2
    copy.reset();                          // use_count = 1
} // 自动调用 Destroy()

// 显式释放
AxPtr<IMath> math = AxPlug::CreateTool<IMath>();
AxPlug::DestroyTool(math);  // math 变为 nullptr
```

### 15.2 内置 Profiler

Profiler 采用流式写入，每 8192 条自动刷盘，适合长时间运行场景。

```cpp
// 启动分析会话
AxPlug::ProfilerBegin("MyApp", "trace.json");

// 在函数中使用 RAII 宏自动记录耗时
void MyFunction() {
    AX_PROFILE_FUNCTION();  // 自动记录函数名和耗时
    // ... 业务代码 ...
}

void MyLoop() {
    AX_PROFILE_SCOPE("MainLoop");  // 自定义名称
    // ... 循环逻辑 ...
}

// 结束会话，输出 trace.json
AxPlug::ProfilerEnd();
// 在 chrome://tracing 中打开 trace.json 可视化
```

### 15.3 异常处理

错误存储在 AxCore.dll 中的 `thread_local` 变量中，所有 DLL（包括插件）通过 C API 路由，确保跨 DLL 可见性。

```cpp
// 所有 API 调用自动包裹异常保护
auto* obj = AxPlug::CreateToolRaw<IMath>();
if (!obj) {
    if (AxPlug::HasError()) {
        const char* err = AxPlug::GetLastError();
        printf("错误: %s\n", err);
    }
    AxPlug::ClearLastError();
}
```

### 15.4 多线程并发

```cpp
// v3 使用 shared_mutex 读写锁，多线程安全
// 读操作（GetService 命中缓存）并发无锁竞争
std::vector<std::thread> threads;
for (int i = 0; i < 8; i++) {
    threads.emplace_back([&]() {
        // 并发获取 Service - 安全
        auto* logger = AxPlug::GetService<ILoggerService>();
        // 并发创建 Tool - 安全
        auto math = AxPlug::CreateTool<IMath>();
        math->Add(1, 2);
    });
}
for (auto& t : threads) t.join();
```

---

## 16. 使用注意事项

### 16.1 对象销毁

- **推荐**使用智能指针 (`CreateTool<T>()`) 自动管理生命周期
- 使用原始指针时，**必须**通过 `AxPlug::DestroyTool(ptr)` 或让 `shared_ptr` deleter 自动释放
- **禁止**直接 `delete` 插件对象，必须通过框架提供的 `DestroyTool` 接口销毁

### 16.2 Profiler 宏

`AX_PROFILE_SCOPE(name)` 的 `name` 参数必须是**静态字符串**（字符串字面量或 `__FUNCTION__`），不要传入动态生成的 `std::string::c_str()`。

```cpp
// 正确
AX_PROFILE_SCOPE("MyFunction");
AX_PROFILE_FUNCTION();  // 使用 __FUNCTION__

// 危险 — 避免
std::string name = "Process_" + std::to_string(id);
AX_PROFILE_SCOPE(name.c_str());  // name 必须在 scope 结束前有效
```

### 16.3 WinsockInit 析构顺序

网络插件 DLL 使用 `WinsockInit` 管理 WSA 生命周期。在正常关闭流程中，socket 对象应在 DLL 卸载前释放。进程退出时 Windows 自动回收 socket 资源。

---

## 17. 常见问题

| 现象 | 解决方案 |
|------|----------|
| `CreateTool` 返回 nullptr | 检查插件 DLL 是否在 exe 目录，接口是否有 `AX_INTERFACE` 宏。检查 `AxPlug::GetLastError()` |
| 命名绑定返回 nullptr | 检查 `implName` 拼写是否正确，确认插件使用了 `AX_PLUGIN_TOOL_NAMED` 宏注册 |
| 链接错误：找不到 `Ax_*` 函数 | CMake 中添加 `target_link_libraries(... AxCore)` |
| Service 返回旧实例 | 先 `ReleaseService<T>(name)` 再重新 `GetService` |
| DLL 加载失败 | 确保所有模块使用相同的 MSVC 运行时 (MD/MDd) |
| 接口匹配失败 | 确认接口头文件中有 `AX_INTERFACE(类名)` 宏 |
| 多线程崩溃 | 框架 API 线程安全（shared_mutex）。检查插件自身是否线程安全 |
| Profiler 无输出 | 确认调用了 `AxPlug::ProfilerBegin()` 和 `AxPlug::ProfilerEnd()` |

---

*AxPlug 插件框架 · 使用手册*
