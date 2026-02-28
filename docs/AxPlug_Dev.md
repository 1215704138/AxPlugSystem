# AxPlug 插件框架 · 0基础开发交接指南

> 本文档面向刚接手本项目的开发者，帮助你快速理解插件框架的技术栈、底层实现原理、以及如何在此基础上进行维护和扩展。

---

## 1. 技术栈总览

| 技术 | 用途 | 在本系统中的位置 |
|------|------|------------------|
| **C++17** | 语言标准（`constexpr`、`if constexpr`、`std::filesystem`、`std::shared_mutex`、折叠表达式） | 全局 |
| **FNV-1a 编译期哈希** | 将接口名字符串在编译期变为 `uint64_t` typeId，实现 O(1) 类型查找 | `IAxObject.h` — `AxTypeHash()` |
| **Pimpl (Pointer to Implementation)** | ABI 隔离：`AxPluginManager` 的所有私有数据藏在 `AxPluginManagerImpl` 中，内部改动不影响外部头文件二进制布局 | `AxPluginManager.h` / `AxPluginManagerImpl.h` |
| **CRTP (Curiously Recurring Template Pattern)** | `AxPluginImpl<TImpl, TInterfaces...>` 自动实现 `Destroy()`、`OnInit()`、`OnShutdown()` | `AxPluginImpl.h` |
| **`std::shared_ptr` 自定义删除器** | `CreateTool` / `GetService` 返回带框架回收逻辑的智能指针 | `AxPlug.h` — `CreateTool<T>()` / `GetService<T>()` |
| **`std::shared_mutex` 读写锁** | 插件注册表的并发安全：读多写少场景用 `shared_lock` / `unique_lock` | `AxPluginManagerImpl::mutex_` |
| **`std::call_once`** | 服务单例的线程安全懒初始化 | `SingletonHolder::flag` |
| **DLL 动态加载** | Windows `LoadLibraryExW` / Linux `dlopen` 加载插件 DLL | `OSUtils.hpp` |
| **`static_assert` 编译期检查** | 确保模板参数继承自 `IAxObject` 且包含 `AX_INTERFACE` 宏 | `AxPlug.h` 所有模板函数 |
| **Chrome Trace Format** | 性能分析器输出 JSON，可在 chrome://tracing 可视化 | `AxProfiler.h` / `AxProfiler.cpp` |
| **跨 DLL 异常处理** | `thread_local` 错误状态存储在 AxCore.dll，所有 DLL 通过 C API 访问 | `AxException.h` / `AxCoreDll.cpp` |

---

## 2. 学习路线（推荐阅读顺序）

### 2.1 必备前置知识

1. **C++ 智能指针** — `shared_ptr` / `unique_ptr` / `weak_ptr`
   - 推荐：《C++ Primer》第12章
   - 重点：自定义删除器、引用计数原理、`enable_shared_from_this`

2. **C++ 模板基础** — 模板函数、模板类、CRTP、`static_assert`、`std::void_t` SFINAE
   - 推荐：《Effective Modern C++》条款1-4、《C++ Templates: The Complete Guide》第1-3章
   - 重点：理解 `AxPluginImpl<TImpl, TInterfaces...>` 的变参模板展开

3. **动态链接库 (DLL) 原理** — `dllexport` / `dllimport`、导出函数、`LoadLibrary` / `GetProcAddress`
   - 推荐：MSDN "Dynamic-Link Libraries" 系列文章
   - 重点：为什么不能跨 DLL 传递 `std::string`、为什么运行时库必须统一

4. **C++ 多线程** — `mutex` / `shared_mutex` / `call_once` / `atomic`
   - 推荐：《C++ Concurrency in Action》第2-3章
   - 重点：`shared_mutex` 的读写锁语义

5. **设计模式** — 单例、工厂、Pimpl、代理、观察者
   - 推荐：《设计模式》对应章节，或 refactoring.guru 网站

### 2.2 代码阅读顺序

```
第一轮：接口层（理解"合同"）
  ① include/AxPlug/IAxObject.h          ← 基类 + AX_INTERFACE 宏 + FNV-1a 哈希
  ② include/AxPlug/AxPluginExport.h     ← AxPluginInfo 结构体 + 插件导出宏
  ③ include/AxPlug/AxPluginImpl.h       ← CRTP 基类，自动实现 Destroy/OnInit/OnShutdown
  ④ include/AxPlug/AxAutoRegister.h     ← 自动注册机制 (替代手写 PLUGIN_MAP)

第二轮：用户 API 层（理解"怎么用"）
  ⑤ include/AxPlug/AxPlug.h             ← Init / CreateTool / GetService / Query / EventBus 便捷 API

第三轮：核心引擎（理解"怎么跑"）
  ⑥ src/AxCore/AxPluginManager.h        ← 管理器公开接口
  ⑦ src/AxCore/AxPluginManagerImpl.h    ← Pimpl 内部数据结构
  ⑧ src/AxCore/AxPluginManager.cpp      ← 核心逻辑：加载DLL、创建对象、单例管理
  ⑨ src/AxCore/AxCoreDll.cpp            ← C API 导出层（桥接 C++ Manager 到 extern "C"）

第四轮：辅助系统
  ⑩ include/AxPlug/AxException.h        ← 跨DLL异常处理
  ⑪ include/AxPlug/AxProfiler.h         ← 性能分析器
  ⑫ include/AxPlug/OSUtils.hpp          ← 跨平台DLL加载/路径工具
```

---

## 3. 核心机制深度解析

### 3.1 类型系统 — FNV-1a 编译期哈希

**问题**：插件通过接口名查找，字符串比较 O(N) 且开销大。

**解决方案**：`AX_INTERFACE(IMath)` 宏展开为：
```cpp
static constexpr const char* ax_interface_name = "IMath";
static constexpr uint64_t ax_type_id = AxTypeHash("IMath");  // 编译期计算
```

运行时所有查找都基于 `uint64_t` 比较（O(1) 哈希表查找），彻底消除字符串开销。

### 3.2 插件加载流程

```
AxPlug::Init(pluginDir)
  │
  ├─ AxPluginManager::Init()           设置 DLL 搜索路径
  │     └─ 发布 EVENT_SYSTEM_INIT
  │
  └─ AxPluginManager::LoadPlugins()    扫描目录下所有 .dll
        │
        └─ 对每个 DLL：LoadOnePlugin()
              │
              ├─ LoadLibraryExW(path)                    加载 DLL 到进程空间
              ├─ GetProcAddress("GetAxPlugins")           找到入口函数
              ├─ GetAxPlugins(&count)                     获取 AxPluginInfo 数组
              ├─ 遍历 info 数组：
              │   ├─ 检查 ABI 版本兼容性
              │   ├─ 注册到 registry_ (typeId → index)
              │   ├─ 注册到 namedImplRegistry_ (typeId+name → index)
              │   └─ 注册到 nameToTypeId_ (string → typeId)
              └─ 发布 EVENT_PLUGIN_LOADED
```

### 3.3 对象创建流程 — Tool

```
用户调用: auto math = AxPlug::CreateTool<IMath>();
  │
  ├─ static_assert 编译期检查：T 继承自 IAxObject 且有 ax_type_id
  ├─ Ax_CreateObjectById(IMath::ax_type_id)
  │     └─ AxPluginManager::CreateObjectById()
  │           ├─ shared_lock(mutex_)                 读锁
  │           ├─ registry_.find(typeId)              O(1) 查找
  │           ├─ pluginInfo.createFunc()             调用工厂函数 new CMath()
  │           └─ return IAxObject*
  │
  └─ 包装为 shared_ptr<IMath>(obj, [](IMath* p) { Ax_ReleaseObject(p); })
     └─ 自定义删除器：离开作用域时自动调用 Destroy()
```

### 3.4 对象创建流程 — Service (单例)

```
用户调用: auto logger = AxPlug::GetService<ILoggerService>("app");
  │
  ├─ Ax_AcquireSingletonById(typeId, "app")
  │     └─ AxPluginManager::AcquireSingletonById()
  │           ├─ unique_lock(mutex_)
  │           ├─ 查找或创建 SingletonHolder
  │           ├─ std::call_once(holder.flag, [&] {
  │           │     holder.instance = shared_ptr(createFunc());
  │           │     holder.instance->OnInit();
  │           │     shutdownStack_.push_back(holder.instance);
  │           │  })
  │           ├─ holder.externalRefs++                    引用计数+1
  │           └─ return holder.instance.get()
  │
  └─ 包装为 shared_ptr<T>(obj, [typeId, name](T*) {
         if (!Ax_IsShuttingDown()) Ax_ReleaseSingletonRef(typeId, name);
     })
     └─ 自定义删除器：减少外部引用计数，防止 UAF
```

### 3.5 Pimpl ABI 隔离

```
外部可见:                              内部隐藏:
┌─────────────────────┐               ┌──────────────────────────────┐
│ AxPluginManager.h   │               │ AxPluginManagerImpl.h        │
│                     │               │                              │
│  unique_ptr<Impl>   │──────────────►│  registry_ (unordered_map)   │
│  pimpl_;            │               │  modules_ (deque)            │
│                     │               │  singletonHolders_ (map)     │
│  公开方法声明       │               │  shutdownStack_ (vector)     │
└─────────────────────┘               │  shared_mutex mutex_         │
                                      │  defaultEventBus_            │
                                      └──────────────────────────────┘
```

**好处**：修改 `AxPluginManagerImpl` 中的成员变量（增删字段、改类型），外部代码无需重新编译，因为外部只看到一个 `unique_ptr<AxPluginManagerImpl>` 指针。

### 3.6 自动注册机制

传统方式需要手写 `module.cpp`：
```cpp
AX_BEGIN_PLUGIN_MAP()
    AX_PLUGIN_TOOL(CMath, IMath)
AX_END_PLUGIN_MAP()
```

自动注册方式只需在实现 `.cpp` 中写一行：
```cpp
AX_AUTO_REGISTER_TOOL(CMath, IMath)
```

**原理**：
1. `AX_AUTO_REGISTER_TOOL` 创建一个**静态全局变量** `AutoRegistrar`
2. `AutoRegistrar` 的构造函数在 DLL 加载时（静态初始化阶段）自动执行
3. 构造函数将一个 lambda 推入 `GetAutoRegList()` 全局列表
4. `AX_DEFINE_PLUGIN_ENTRY()` 生成 `GetAxPlugins` C 导出函数，遍历列表收集所有注册信息

**每个 DLL 只需一个 `module.cpp`**：
```cpp
#include "AxPlug/AxAutoRegister.h"
AX_DEFINE_PLUGIN_ENTRY()
```

### 3.7 关机流程

```
进程退出 → AxPluginManager::~AxPluginManager()
  │
  ├─ DefaultEventBus::Shutdown()          停止异步事件线程
  ├─ 发布 EVENT_SYSTEM_SHUTDOWN
  ├─ ReleaseAllSingletons()               按创建逆序调用 OnShutdown() + Destroy()
  │     └─ shutdownStack_ 逆序遍历
  ├─ modules_.clear()                     清理模块记录（DLL 不卸载，由 OS 回收）
  └─ registry_.clear()                    清理注册表
```

**DLL 不卸载的原因**：Tool 对象可能还有外部持有的原始指针指向 DLL 代码段，调用 `FreeLibrary` 会导致虚函数表失效崩溃。

---

## 4. 文件清单与职责

### 4.1 公开头文件 (`include/`)

| 文件 | 职责 |
|------|------|
| `AxPlug/AxPlug.h` | **用户唯一入口**：Init / CreateTool / GetService / DestroyTool / ReleaseService / Query / Profiler / EventBus 便捷 API |
| `AxPlug/IAxObject.h` | 接口基类 + `AX_INTERFACE` 宏 + FNV-1a 哈希函数 + `AxPtr<T>` 别名 |
| `AxPlug/AxPluginExport.h` | `AxPluginInfo` 结构体 + `AX_PLUGIN_TOOL` / `AX_PLUGIN_SERVICE` 导出宏 + ABI 版本 |
| `AxPlug/AxPluginImpl.h` | CRTP 模板基类，自动实现 `Destroy()` / `OnInit()` / `OnShutdown()` |
| `AxPlug/AxAutoRegister.h` | 自动注册机制：`AX_AUTO_REGISTER_TOOL` / `AX_DEFINE_PLUGIN_ENTRY` |
| `AxPlug/AxEventBus.h` | 事件总线接口（详见 EventBus_DEV.md） |
| `AxPlug/AxException.h` | 跨DLL异常处理：`AxErrorState` / `AxExceptionGuard` / 错误码 |
| `AxPlug/AxProfiler.h` | 性能分析器：RAII 计时器 + Chrome Trace 输出 |
| `AxPlug/OSUtils.hpp` | 跨平台工具：DLL 加载、路径处理、字符编码转换 |
| `AxPlug/WinsockInit.hpp` | Windows Winsock2 RAII 初始化 |

### 4.2 核心引擎 (`src/AxCore/`)

| 文件 | 职责 |
|------|------|
| `AxPluginManager.h` | 管理器公开接口：Init / LoadPlugins / CreateObject / GetSingleton / EventBus |
| `AxPluginManagerImpl.h` | Pimpl 内部数据结构：注册表、模块列表、单例缓存、关机栈 |
| `AxPluginManager.cpp` | 核心逻辑实现（~670行）：DLL 扫描加载、对象工厂、单例生命周期、引用计数 |
| `AxCoreDll.cpp` | C API 导出层：将 C++ AxPluginManager 方法桥接为 `extern "C"` 函数 |
| `AxCoreDll.def` | DLL 导出符号定义文件 |
| `DefaultEventBus.h/cpp` | 默认事件总线实现（详见 EventBus_DEV.md） |
| `AxProfiler.cpp` | 性能分析器实现：JSON 输出、文件写入 |

---

## 5. 在当前系统中如何使用

### 5.1 开发一个新的 Tool 插件（完整步骤）

**第一步：定义接口**（放在 `include/business/` 或 `include/driver/`）
```cpp
// include/business/ICalculator.h
#pragma once
#include "AxPlug/IAxObject.h"

class ICalculator : public IAxObject {
    AX_INTERFACE(ICalculator)
public:
    virtual double Calculate(double a, double b) = 0;
};
```

**第二步：编写实现**（放在 `src/business/CalculatorPlugin/`）
```cpp
// src/business/CalculatorPlugin/Calculator.h
#pragma once
#include "AxPlug/AxPluginImpl.h"
#include "business/ICalculator.h"

class CCalculator : public AxPluginImpl<CCalculator, ICalculator> {
public:
    double Calculate(double a, double b) override { return a + b; }
};
```

**第三步：注册插件**
```cpp
// src/business/CalculatorPlugin/module.cpp
#include "Calculator.h"
#include "AxPlug/AxAutoRegister.h"

AX_AUTO_REGISTER_TOOL(CCalculator, ICalculator)
AX_DEFINE_PLUGIN_ENTRY()
```

**第四步：编写 CMakeLists.txt**
```cmake
add_library(CalculatorPlugin SHARED Calculator.h Calculator.cpp module.cpp)
setup_plugin_target(CalculatorPlugin)
target_link_libraries(CalculatorPlugin PRIVATE AxInterface)
```

**第五步：在根 CMakeLists.txt 中添加**
```cmake
add_subdirectory(src/business/CalculatorPlugin)
```

### 5.2 开发一个新的 Service 插件

与 Tool 唯一的区别是注册宏：
```cpp
AX_AUTO_REGISTER_SERVICE(CMyService, IMyService)
```

Service 会被框架以单例模式管理，通过 `AxPlug::GetService<IMyService>()` 获取。

### 5.3 同一接口多实现（命名绑定）

```cpp
// 注册时指定名称
AX_AUTO_REGISTER_TOOL_NAMED(BoostTcpServer, ITcpServer, "boost")
AX_AUTO_REGISTER_TOOL_NAMED(AsioTcpServer, ITcpServer, "asio")

// 使用时指定名称
auto server = AxPlug::CreateTool<ITcpServer>("boost");
```

---

## 6. 扩展指南

### 6.1 添加新的接口分类目录

当前接口按职责分为三类：
- `include/core/` — 框架核心服务接口（日志、图像、网络事件总线）
- `include/driver/` — 驱动/通信接口（TCP、UDP）
- `include/business/` — 业务逻辑接口（数学计算等）

新增接口时，选择对应目录放置头文件，并在根 `CMakeLists.txt` 的 `AxInterface` 中确认 include 路径已覆盖。

### 6.2 扩展 Profiler

Profiler 使用 RAII 宏自动计时：
```cpp
void MyFunction() {
    AX_PROFILE_FUNCTION();  // 自动记录 MyFunction 的耗时
    // ...
    {
        AX_PROFILE_SCOPE("HeavyComputation");  // 记录指定名称的代码块耗时
        // ...
    }
}
```

输出的 `trace.json` 可在 Chrome 浏览器地址栏输入 `chrome://tracing` 加载查看。

### 6.3 扩展异常处理

在插件代码中使用 `AxExceptionGuard` 包裹跨模块调用：
```cpp
IAxObject* obj = AxExceptionGuard::SafeCallPtr([&]() {
    return someRiskyOperation();
}, "MyPlugin::Init");

if (!obj && AxErrorState::HasError()) {
    printf("Error: %s\n", AxErrorState::GetErrorMessage());
}
```

---

## 7. 维护注意事项

### 7.1 ABI 兼容性红线

| 禁止操作 | 原因 |
|----------|------|
| 修改 `IAxObject` 虚函数表（增删虚函数） | 所有已编译插件的 vtable 偏移将失效 |
| 修改 `AxPluginInfo` 结构体布局 | `GetAxPlugins` 返回的数组会内存错乱 |
| 在接口虚函数参数中使用 `std::string` / `std::vector` | 不同编译器/版本的内存布局不同 |
| 混用 `/MD` 和 `/MT` 运行时库 | 堆管理器不同，跨 DLL `delete` 会崩溃 |

**如果必须破坏 ABI**：递增 `AX_PLUGIN_ABI_VERSION`（当前为 1），加载时检查不匹配的插件。

### 7.2 锁策略

| 锁 | 类型 | 保护的数据 | 注意事项 |
|----|------|-----------|----------|
| `AxPluginManagerImpl::mutex_` | `shared_mutex` | 注册表、模块列表、单例缓存 | 读用 `shared_lock`，写用 `unique_lock` |
| `SingletonHolder::flag` | `once_flag` | 单例初始化 | 保证只执行一次，无需额外加锁 |
| `DefaultEventBus::subscriberMutex_` | `mutex` | 订阅列表 (COW) | 持有时间极短（仅拷贝 shared_ptr） |
| `DefaultEventBus::queueMutex_` | `mutex` | 异步事件队列 | 与 `queueCV_` 配合使用 |

**死锁规避**：所有锁都不嵌套。框架内部保证任何路径最多只持有一个锁。

### 7.3 测试

测试代码位于 `test/` 目录，构建时通过 `-DAXPLUG_BUILD_TESTS=ON` 启用。主要测试：
- 插件加载与创建
- Tool / Service 生命周期
- 事件总线订阅/发布/异步
- 图像统一服务
- 网络通信驱动

运行测试：
```bash
scripts\build_debug_with_test.bat
test\build\bin\plugin_system_test.exe
```
