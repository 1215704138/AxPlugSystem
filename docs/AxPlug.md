# AxPlug 插件框架 · 使用手册

## 目录

1. [框架简介](#1-框架简介)
2. [核心概念](#2-核心概念)
3. [快速开始](#3-快速开始)
4. [使用 Tool（工具插件）](#4-使用-tool工具插件)
5. [使用 Service（服务插件）](#5-使用-service服务插件)
6. [查询已加载插件](#6-查询已加载插件)
7. [开发 Tool 插件](#7-开发-tool-插件)
8. [开发多插件 DLL](#8-开发多插件-dll)
9. [开发 Service 插件](#9-开发-service-插件)
10. [核心服务 — 日志](#10-核心服务--日志)
11. [核心服务 — 图像统一](#11-核心服务--图像统一)
12. [驱动插件 — TCP / UDP](#12-驱动插件--tcp--udp)
13. [构建与部署](#13-构建与部署)
14. [常见问题](#14-常见问题)

---

## 1. 框架简介

AxPlug 是一个轻量级 C++ 插件框架，支持动态加载 DLL 插件并通过类型安全的模板 API 进行调用。

**核心特性：**

- 用户侧只需 `#include "AxPlug/AxPlug.h"` 一个头文件
- 基于 C++ 接口类型自动匹配，无需手写字符串 ID
- 编译期字符串字面量作为类型键，保证跨 DLL 一致
- Tool（多实例）和 Service（命名单例）两种插件模型
- 插件 DLL 放在 exe 同目录即可自动发现
- **一个 DLL 可导出多个插件**，声明式宏，完全向后兼容

---

## 2. 核心概念

### 2.1 两种插件类型

| 类型 | 生命周期 | 实例数 | 获取方式 | 释放方式 |
|------|----------|--------|----------|----------|
| **Tool** | 用户管理 | 多实例 | `CreateTool<T>()` | `DestroyTool(ptr)` |
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

    // 使用 Tool
    auto* math = AxPlug::CreateTool<IMath>();
    int result = math->Add(10, 20);
    logger->InfoFormat("10 + 20 = %d", result);
    AxPlug::DestroyTool(math);

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

Tool 是多实例插件，每次调用 `CreateTool` 都创建独立的新对象，用户负责释放。

```cpp
// 创建
auto* math = AxPlug::CreateTool<IMath>();

// 使用
int sum = math->Add(10, 20);

// 销毁
AxPlug::DestroyTool(math);
```

可同时创建多个独立实例：

```cpp
auto* m1 = AxPlug::CreateTool<IMath>();
auto* m2 = AxPlug::CreateTool<IMath>();
// m1 != m2，各自独立
AxPlug::DestroyTool(m1);
AxPlug::DestroyTool(m2);
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

## 6. 查询已加载插件

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

## 7. 开发 Tool 插件

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

## 8. 开发多插件 DLL

一个 DLL 可以导出多个插件，使用声明式宏，清晰易用。

### 8.1 使用场景

- **功能相关的插件组合**：如数学库同时提供基础计算和高级计算
- **减少 DLL 数量**：简化部署，减少文件数量
- **共享内部实现**：多个插件可共享内部辅助类或资源

### 8.2 开发步骤

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

### 8.3 宏说明

- `AX_BEGIN_PLUGIN_MAP()` 开始多插件声明
- `AX_PLUGIN_TOOL(TClass, InterfaceType)` 声明 Tool 插件
- `AX_PLUGIN_SERVICE(TClass, InterfaceType)` 声明 Service 插件
- `AX_END_PLUGIN_MAP()` 结束声明

### 8.4 使用方式

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

### 8.5 向后兼容

- **现有单插件 DLL**：无需修改，继续使用 `AX_EXPORT_TOOL` / `AX_EXPORT_SERVICE`
- **用户侧代码**：无需修改，API 保持不变
- **查询 API**：每个插件独立索引，多插件 DLL 占多个索引

---

## 9. 开发 Service 插件

与 Tool 唯一的区别是导出宏使用 `AX_EXPORT_SERVICE`：

```cpp
// module.cpp
#include "LoggerService.h"
#include "AxPlug/AxPluginExport.h"

AX_EXPORT_SERVICE(LoggerService, ILoggerService)
```

接口定义和实现方式完全相同，`Destroy()` 固定写法 `{ delete this; }`。

---

## 10. 核心服务 — 日志

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

## 11. 核心服务 — 图像统一

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

## 12. 驱动插件 — TCP / UDP

### 12.1 TCP 客户端

```cpp
#include "driver/ITcpClient.h"

auto* client = AxPlug::CreateTool<ITcpClient>();
client->SetTimeout(3000);
client->Connect("127.0.0.1", 8080);
client->SendString("Hello");

char buf[1024]; size_t len;
client->ReceiveString(buf, sizeof(buf), len);

client->Disconnect();
AxPlug::DestroyTool(client);
```

### 12.2 TCP 服务器

```cpp
#include "driver/ITcpServer.h"

auto* server = AxPlug::CreateTool<ITcpServer>();
server->SetMaxConnections(10);
server->Listen(8080);

// ... 接受连接、收发数据 ...

server->StopListening();
AxPlug::DestroyTool(server);
```

### 12.3 UDP 套接字

```cpp
#include "driver/IUdpSocket.h"

auto* udp = AxPlug::CreateTool<IUdpSocket>();
udp->Bind(9000);
udp->SendStringTo("127.0.0.1", 9001, "Hello UDP");

char buf[1024]; size_t len;
char host[64]; int port;
udp->ReceiveStringFrom(host, sizeof(host), port, buf, sizeof(buf), len);

udp->Unbind();
AxPlug::DestroyTool(udp);
```

---

## 13. 构建与部署

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

## 14. 常见问题

| 现象 | 解决方案 |
|------|----------|
| `CreateTool` 返回 nullptr | 检查插件 DLL 是否在 exe 目录，接口是否有 `AX_INTERFACE` 宏 |
| 链接错误：找不到 `Ax_*` 函数 | CMake 中添加 `target_link_libraries(... AxCore)` |
| Service 返回旧实例 | 先 `ReleaseService<T>(name)` 再重新 `GetService` |
| DLL 加载失败 | 确保所有模块使用相同的 MSVC 运行时 (MD/MDd) |
| 接口匹配失败 | 确认接口头文件中有 `AX_INTERFACE(类名)` 宏 |

---

*AxPlug · 使用手册 · 2026年2月*
