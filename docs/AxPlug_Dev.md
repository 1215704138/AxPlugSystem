# AxPlug 插件框架 · 开发与维护手册 v1.0

本文档面向框架本身的开发者和维护者，描述 AxPlug 的内部架构、源码结构、构建系统和扩展方法。

---

## 目录

1. [源码结构](#1-源码结构)
2. [AxCore.dll — 框架核心](#2-axcoredll--框架核心)
3. [公共头文件体系](#3-公共头文件体系)
4. [插件加载流程](#4-插件加载流程)
5. [对象生命周期管理](#5-对象生命周期管理)
6. [线程安全机制](#6-线程安全机制)
7. [构建系统](#7-构建系统)
8. [命名绑定与多插件 DLL](#8-命名绑定与多插件-dll)
9. [新增插件检查清单](#9-新增插件检查清单)
10. [跨平台支持](#10-跨平台支持)
11. [调试与排障](#11-调试与排障)
12. [扩展框架的注意事项](#12-扩展框架的注意事项)

---

## 1. 源码结构

```
AxPlugSystem/
├── include/                         公共头文件（对外暴露）
│   ├── AxPlug/
│   │   ├── AxPlug.h                 用户唯一入口头文件（inline 模板 API）
│   │   ├── IAxObject.h              基类 + AX_INTERFACE 宏 + FNV-1a typeId + AxPtr<T>
│   │   ├── AxPluginExport.h         导出宏 AX_BEGIN_PLUGIN_MAP / AX_PLUGIN_TOOL / AX_PLUGIN_SERVICE
│   │   ├── AxProfiler.h             内置性能分析器（Chrome trace.json，流式写入）
│   │   ├── AxException.h            跨 DLL 异常处理 + 错误 C API（路由到 AxCore thread_local）
│   │   └── OSUtils.hpp              跨平台动态库工具（header-only）
│   ├── business/                    业务接口
│   ├── core/                        核心服务接口
│   └── driver/                      驱动接口
│
├── src/
│   ├── AxCore/                      框架核心 DLL（唯一的框架运行时）
│   │   ├── AxPluginManager.h        插件管理器声明（内部头文件，不对外）
│   │   ├── AxPluginManager.cpp      插件管理器实现
│   │   ├── AxCoreDll.cpp            C 导出函数桥接层
│   │   ├── AxCoreDll.def            导出符号定义
│   │   └── CMakeLists.txt
│   ├── business/                    业务插件实现
│   ├── core/                        核心服务插件实现
│   └── driver/                      驱动插件实现
│
├── test/                            测试程序
├── deps/                            第三方依赖（spdlog, boost）
├── scripts/                         构建脚本
├── docs/                            文档
└── CMakeLists.txt                   根构建文件
```

**关键设计：**
- `include/AxPlug/` 下的头文件是框架的全部公共接口
- `src/AxCore/AxPluginManager.h` 是**内部头文件**，不对外暴露
- 所有用户侧 API 都在 `AxPlug.h` 中以 inline 模板实现，直接调用 C 导出函数

---

## 2. AxCore.dll — 框架核心

### 2.1 职责

AxCore.dll 是框架的唯一运行时组件，负责：
- 扫描目录并加载插件 DLL
- 维护插件注册表（`interfaceName → 模块索引`）
- 维护 Service 单例缓存（`(interfaceName, serviceName) → IAxObject*`）
- 在退出时销毁所有托管对象并卸载 DLL

### 2.2 C 导出函数

```
AxCoreDll.def:
    # Core API (向后兼容)
    Ax_Init                  初始化（设置 DLL 搜索路径）
    Ax_LoadPlugins           扫描目录加载插件（带重复目录保护）
    Ax_CreateObject          按接口名创建新实例（Tool）
    Ax_GetSingleton          按接口名+服务名获取/创建单例（Service）
    Ax_ReleaseSingleton      释放命名单例
    Ax_ReleaseObject         释放对象（调用 Destroy）
    Ax_GetPluginCount        查询已加载插件数量
    Ax_GetPluginInterfaceName 查询插件接口名
    Ax_GetPluginFileName     查询插件文件名（thread_local snapshot）
    Ax_GetPluginType         查询插件类型（Tool=0, Service=1）
    Ax_IsPluginLoaded        查询插件是否加载成功
    # TypeId Fast Path API
    Ax_CreateObjectById      按 typeId 创建新实例（O(1) 热路径）
    Ax_CreateObjectByIdNamed 按 typeId+implName 创建命名实现实例
    Ax_GetSingletonById      按 typeId+服务名获取/创建单例（O(1) 热路径）
    Ax_ReleaseSingletonById  按 typeId 释放命名单例
    # Profiler API
    Ax_ProfilerBeginSession  启动性能分析会话
    Ax_ProfilerEndSession    结束分析会话并输出 trace.json
    Ax_ProfilerWriteProfile  写入单条 profile 结果
    Ax_ProfilerIsActive      查询分析是否活动
    # Error Handling API (跨 DLL thread_local 存储在 AxCore 中)
    Ax_SetError              设置错误状态（code + message + source）
    Ax_GetErrorCode          获取错误码
    Ax_GetLastError          获取错误消息
    Ax_GetErrorSource        获取错误来源
    Ax_HasErrorState         查询是否有错误
    Ax_ClearLastError        清除错误状态
```

这些 C 函数是 AxCore.dll 的全部 ABI 表面。用户侧的 `AxPlug::` 命名空间 API 是 `AxPlug.h` 中的 inline 模板，编译进调用方，通过上述 C 函数与 AxCore.dll 通信。

**跨 DLL 错误处理**：错误状态存储在 AxCore.dll 的 `thread_local` 变量中。`AxErrorState` 类的所有方法（`Set/Get/Clear/HasError`）通过 C API 路由到 AxCore，确保插件 DLL 中设置的错误在宿主程序中可见。

### 2.3 AxPluginManager

**单例模式**，通过 `Instance()` 返回静态局部变量指针。

核心数据结构：

```cpp
class AxPluginManager {
    // typeId (uint64_t FNV-1a) 替代 string 键，O(1) 哈希查找
    std::unordered_map<uint64_t, int> registry_;                    // typeId → 扁平索引（默认实现）
    std::map<std::pair<uint64_t, std::string>, int> namedImplRegistry_;  // (typeId, implName) → 扁平索引
    std::unordered_map<std::string, uint64_t> nameToTypeId_;        // 接口名 → typeId (兼容层)
    std::vector<PluginModule> modules_;                             // 所有已加载模块
    std::vector<PluginEntry> allPlugins_;                           // 扁平插件列表
    // std::call_once 管理 Service 单例初始化
    std::unordered_map<uint64_t, SingletonHolder> defaultSingletonHolders_;
    std::map<std::pair<uint64_t, std::string>, SingletonHolder> namedSingletonHolders_;
    // LIFO 关闭栈：逆序调用 OnShutdown + Destroy
    std::vector<std::shared_ptr<IAxObject>> shutdownStack_;
    // shared_mutex 读写锁
    mutable std::shared_mutex mutex_;
    // 重复目录保护
    std::vector<std::string> scannedDirs_;
};
```

```cpp
struct PluginModule {
    std::string filePath;                // DLL 完整路径
    std::string fileName;                // DLL 文件名
    std::vector<AxPluginInfo> plugins;   // 一个或多个插件信息
    AxPlug::LibraryHandle handle;        // DLL 句柄
    bool isLoaded;                       // 是否加载成功
    std::string errorMessage;            // 错误信息
};

struct AxPluginInfo {
    const char* interfaceName;           // 接口名
    uint64_t typeId;                     // FNV-1a hash
    AxPluginType type;                   // Tool 或 Service（底层类型为 int）
    IAxObject* (*createFunc)();          // 工厂函数
    const char* implName;                // 命名实现标签，如 "boost"，默认为 ""
    uint32_t abiVersion;                 // ABI 版本号
};
```

### 2.4 AxCoreDll.cpp

纯桥接层，每个导出函数直接转发到 `AxPluginManager::Instance()->Xxx()`。DllMain 为空实现。

---

## 3. 公共头文件体系

### 3.1 IAxObject.h

```cpp
#define AX_INTERFACE(InterfaceName) \
public: \
    static constexpr const char* ax_interface_name = #InterfaceName; \
    static constexpr uint64_t ax_type_id = AxTypeHash(#InterfaceName); \
private:

class IAxObject {
public:
    virtual ~IAxObject() = default;
    virtual void OnInit() {}       // 系统完成实例化后调用
    virtual void OnShutdown() {}   // 管理器析构前调用
protected:
    virtual void Destroy() = 0;    // 仅框架内部调用
    friend class AxPluginManager;
};
```

- `AX_INTERFACE` 同时生成 `ax_interface_name` (string) 和 `ax_type_id` (uint64_t FNV-1a hash)
- `Destroy()` 由 `AxPluginManager::ReleaseObject` 调用，用户侧不可见
- `OnInit()` / `OnShutdown()` 为 Service 插件提供生命周期钩子

### 3.2 AxPluginExport.h

定义插件导出宏（声明式，支持单插件和多插件 DLL）：

```cpp
// AX_BEGIN_PLUGIN_MAP() / AX_END_PLUGIN_MAP() 展开为：
extern "C" AX_PLUGIN_EXPORT const AxPluginInfo* GetAxPlugins(int* count) {
    static const AxPluginInfo plugins[] = {
        { InterfaceType::ax_interface_name, InterfaceType::ax_type_id, AxPluginType::Tool, []() -> IAxObject* { return new TClass(); }, "", AX_PLUGIN_ABI_VERSION },
        // ...
    };
    if (count) *count = sizeof(plugins) / sizeof(plugins[0]);
    return plugins;
}
```

宏包括：`AX_PLUGIN_TOOL`、`AX_PLUGIN_TOOL_NAMED`、`AX_PLUGIN_SERVICE`、`AX_PLUGIN_SERVICE_NAMED`。

### 3.3 AxPlug.h

用户唯一入口。包含：
- C 导出函数声明（`extern "C"` + `AX_CORE_API`）
- `AxPluginQueryInfo` 查询结构体
- `AxPlug::` 命名空间内的 inline 模板函数
- `AX_HOST_INIT()` 便捷宏

**重要**：所有模板函数（`CreateTool<T>`, `GetService<T>`, `ReleaseService<T>`）都通过 `T::ax_type_id` 取得编译期 typeId（FNV-1a hash），再调用对应 typeId C 导出函数（O(1) 查找）。

### 3.4 OSUtils.hpp

Header-only 跨平台工具类，提供：
- `LoadLibrary` / `UnloadLibrary` / `GetSymbol` — 动态库操作
- `GetCurrentModulePath` / `NormalizePath` — 路径处理
- `SetLibrarySearchPath` — Windows DLL 搜索路径
- `LibraryRAII` — RAII 动态库管理器

---

## 4. 插件加载流程

```
AxPlug::Init(dir)
  │
  ├─ Ax_Init(dir)
  │    └─ AxPluginManager::Init()
  │         └─ OSUtils::SetLibrarySearchPath(dir)
  │
  └─ Ax_LoadPlugins(dir)
       └─ AxPluginManager::LoadPlugins(dir)
            │
            ├─ 重复目录检查（scannedDirs_，已扫描则跳过）
            ├─ 非递归遍历目录，寻找 *.dll（跳过 AxCore.dll）
            │
            └─ 对每个 DLL → LoadOnePlugin(path)
                 │
                 ├─ 路径规范化 + shared_lock 去重检查
                 ├─ OSUtils::LoadLibrary(path)         加载 DLL（锁外）
                 ├─ GetAxPlugins                         获取插件信息
                 ├─ unique_lock TOCTOU 重检
                 └─ registerPlugin → registry_ + namedImplRegistry_
```

**查找复杂度**：`O(1)` 哈希表查找（`registry_` 为 `unordered_map`，`namedImplRegistry_` 为 `std::map` 但插件数通常 < 20）。

---

## 5. 对象生命周期管理

### 5.1 Tool（智能指针模式，v3 推荐）

```
CreateTool<T>()
  → Ax_CreateObjectById(T::ax_type_id)     ← typeId O(1) 快速查找
  → AxPluginManager::CreateObjectById()
  → pluginInfo.createFunc()                ← 在插件 DLL 中 new 对象
  → 包装为 AxPtr<T> (shared_ptr + 自定义 deleter)
  → 返回 AxPtr<T>

// 自动释放（RAII）：AxPtr 离开作用域时自动调用 deleter → Ax_ReleaseObject → Destroy()
// 显式释放：AxPlug::DestroyTool(axptr) → ptr.reset() → 触发 deleter
```

### 5.2 Tool（原始指针模式，向后兼容）

```
CreateToolRaw<T>()
  → Ax_CreateObjectById(T::ax_type_id)
  → 返回 T* 原始指针
  
DestroyTool(ptr)
  → Ax_ReleaseObject(ptr)
  → obj->Destroy()               ← 调用插件实现的 delete this
```

**关键**：对象在插件 DLL 中 `new`，也在插件 DLL 中 `delete`，避免跨模块堆操作。

### 5.3 Service

```
GetService<T>(name)
  → Ax_GetSingletonById(T::ax_type_id, name)
  → AxPluginManager::GetSingletonById()
  → [Fast Path] shared_lock 查找 SingletonHolder
  → 命中且已初始化则直接返回 instance
  → [Slow Path] unique_lock 创建 SingletonHolder
  → std::call_once: CreateObjectByIdInternal() + 推入 shutdownStack_
  → 调用 OnInit() 钩子
  → 返回 instance

ReleaseService<T>(name)
  → Ax_ReleaseSingletonById(T::ax_type_id, name)
  → unique_lock → 检查外部引用 externalRefs
  → 若存在引用则 defer 析构 (pendingRelease = true)
  → 若无引用则从 shutdownStack_ 移除 → erase holder

### 5.4 SIOF 与静态生命周期安全

框架为每一个通过 `GetService` 获取的 Service 返回了一个绑定自定义删除器的 `shared_ptr`。如果用户由于设计遗留将该 `shared_ptr` 封存在全局静态变量中（如 Meyer's 单体），当进程退出时极易发生静态对象析构顺序灾难（SIOF）：即 `AxPluginManager` 先于静态 `shared_ptr` 析构，导致删除器中调用的 `Ax_ReleaseSingletonRef` 发生毁灭性的 Access Violation Use-After-Free。

**解决方案：**
在 `AxCoreDll.cpp` 中定义一个**文件全局作用域**的 `std::atomic<bool> g_shuttingDown`，该变量生命周期必然长于函数内部静态局部变量形式的 Meyer's 单例。`AxPluginManager` 在析构的 `ReleaseAllSingletons` 阶段，会将其置为 true。同时，所有暴露给外部的 Service `shared_ptr` 删除器中，都前置了 `if (!Ax_IsShuttingDown())` 的条件检查。如此，即便单例管理器已熔毁散架，外围被绑架的残留 `shared_ptr` 也能凭借该哨兵静默自燃并安全回收，杜绝段错误。
```

### 5.4 框架退出时

`AxPluginManager` 析构函数调用 `ReleaseAllSingletons()`：
1. 拷贝 `shutdownStack_` 并释放锁（防止 OnShutdown 中死锁）
2. 逆序调用所有单例的 `OnShutdown()`
3. 逆序重置 `shared_ptr` 触发 `Destroy()`
4. 清空模块列表和注册表（DLL 不显式卸载，OS 在进程退出时回收）

---

## 6. 线程安全机制

- `AxPluginManager` 使用 `std::shared_mutex` 读写锁
- **读操作**（`GetSingleton` 命中缓存、`GetPluginCount` 等）使用 `std::shared_lock`，多线程可并发读
- **写操作**（`LoadOnePlugin`、首次创建 Service、`ReleaseSingleton`）使用 `std::unique_lock`
- **双检锁**（Double-Check Locking）：`GetSingletonById` 先 shared_lock 检查，未命中再 unique_lock 创建
- `GetPluginFileName` 使用 `thread_local` snapshot 避免 shared_lock 下的 mutable 成员竞态
- 插件加载在 `Init()` 阶段一次性完成，`LoadPlugins` 带重复目录保护
- 各插件自身的线程安全由插件实现者负责（除核心网络组件与设施外，这些驱动已完全由 `io_mutex_` 等进行了覆盖）
- **Wait-On-Exit 死锁彻底规避**：对于驻留后台运转的基础服务线程（如 `StaticThreadPool` / 日志落盘等），均采用了极其周密的生命周期收割法：通过主动钩入用户安全期的 `OnShutdown` 函数执行 `join()` 回收；而针对静态生命周期大限及 DLL 卸载时的被动虚析构，一律实行 `detach` 兜底放弃。彻底根除 Windows 在 ExitProcess 及 FreeLibrary 时获取 loader lock 导致的连环死锁绝症。
- 所有 C API 入口自动包裹 `AxExceptionGuard`，异常不会跨 DLL 传播

---

## 7. 构建系统

### 7.1 根 CMakeLists.txt 关键配置

```cmake
cmake_minimum_required(VERSION 3.15)
project(AxPlug VERSION 1.0.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 确保所有模块使用相同的运行时库
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDebugDLL")
endif()
```

### 7.2 AxInterface — header-only 目标

```cmake
add_library(AxInterface INTERFACE)
target_include_directories(AxInterface INTERFACE ${CMAKE_SOURCE_DIR}/include)
```

所有插件 DLL 链接 `AxInterface` 获取公共头文件路径。

### 7.3 AxCore — 框架 DLL

```cmake
add_library(AxCore SHARED AxPluginManager.cpp AxCoreDll.cpp)
target_compile_definitions(AxCore PRIVATE AX_CORE_EXPORTS)
target_link_libraries(AxCore PUBLIC AxInterface)
# Windows: 使用 .def 文件控制导出符号
set_target_properties(AxCore PROPERTIES LINK_FLAGS "/DEF:...AxCoreDll.def")
```

### 7.4 插件 DLL 模板

```cmake
add_library(XxxPlugin SHARED src/Xxx.cpp src/module.cpp)
target_compile_definitions(XxxPlugin PRIVATE AX_PLUGIN_EXPORTS)
target_link_libraries(XxxPlugin PRIVATE AxInterface)
target_include_directories(XxxPlugin PRIVATE include ${CMAKE_SOURCE_DIR}/include)
set_target_properties(XxxPlugin PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
```

**注意**：插件仅链接 `AxInterface`（header-only），不链接 `AxCore`。插件不依赖 AxCore.dll。

### 7.5 测试程序

```cmake
add_executable(MyTest test.cpp)
target_link_libraries(MyTest PRIVATE AxCore)
```

测试/宿主程序链接 `AxCore`（获取导入库 AxCore.lib）。

### 7.6 自动化构建脚本

项目提供了自动化脚本来简化 SDK 的生成与发布：

- `scripts/build_publish_debug.bat`：构建 Debug 版本并安装到 `publish/`
- `scripts/build_publish_release.bat`：构建 Release 版本并安装到 `publish/`
- `scripts/build_test.bat`：构建测试程序（依赖 `publish/` 中的 SDK）

**发布目录结构 (`publish/`)**：

```
publish/
├── include/           # 公共头文件
├── lib/               # 导入库 (.lib)
└── bin/               # 动态库 (.dll)
```

---

## 8. 命名绑定与多插件 DLL

### 8.1 设计目标

- **命名绑定**：同一接口支持多个实现，通过 `implName` 区分
- **向后兼容**：现有单插件 DLL 无需修改
- **声明式**：使用清晰的宏语法，避免手写数组
- **扁平索引**：查询 API 每个插件独立索引，多插件 DLL 占多个索引

### 8.2 核心改动

#### 8.2.1 AxPluginExport.h 类型和宏

```cpp
enum class AxPluginType : int { Tool, Service };  // 显式底层类型确保 ABI 安全

struct AxPluginInfo {
    const char* interfaceName;
    uint64_t typeId;
    AxPluginType type;
    IAxObject* (*createFunc)();
    const char* implName;         // 命名实现标签，"" 为默认
};

using GetAxPluginsFunc = const AxPluginInfo*(*)(int*);

// 声明式宏（含命名变体）
#define AX_PLUGIN_TOOL(TClass, InterfaceType) ...
#define AX_PLUGIN_TOOL_NAMED(TClass, InterfaceType, ImplName) ...
#define AX_PLUGIN_SERVICE(TClass, InterfaceType) ...
#define AX_PLUGIN_SERVICE_NAMED(TClass, InterfaceType, ImplName) ...
```

#### 8.2.2 AxPluginManager 注册逻辑

```cpp
// registerPlugin lambda 处理命名绑定
auto registerPlugin = [&](const AxPluginInfo& info, int moduleIndex, int pluginIndex) {
    std::string implName = (info.implName && info.implName[0] != '\0') ? info.implName : "";
    int flatIndex = static_cast<int>(allPlugins_.size());
    // Named impl registry: (typeId, implName) -> flatIndex
    auto namedKey = std::make_pair(info.typeId, implName);
    if (!namedImplRegistry_.insert({namedKey, flatIndex}).second) return;  // 重复跳过
    allPlugins_.push_back({moduleIndex, pluginIndex});
    registry_.insert({info.typeId, flatIndex});  // 第一个注册的成为默认
    nameToTypeId_[info.interfaceName] = info.typeId;
};
```

#### 8.2.3 LoadOnePlugin 流程

1. 锁外规范化路径 + shared_lock 去重检查
2. 锁外加载 DLL + 解析 `GetAxPlugins` 入口点
3. unique_lock TOCTOU 重检查
4. 调用 `GetAxPlugins` 获取插件信息数组
5. 通过 `registerPlugin` lambda 注册到 `registry_` 和 `namedImplRegistry_`（含 ABI 版本检查）

### 8.3 查询 API 扁平化

查询 API 现在返回**每个插件**的独立索引：

```cpp
// 之前：每个模块一个索引
// 现在：每个插件一个索引
int count = AxPlug::GetPluginCount();  // 6 个插件 = 6 个索引
for (int i = 0; i < count; i++) {
    auto info = AxPlug::GetPluginInfo(i);
    // 可能显示：
    // i=0: IMath (MathSuite.dll)
    // i=1: ICalculator (MathSuite.dll)  // 同一 DLL，不同索引
    // i=2: ILoggerService (LoggerPlugin.dll)
}
```

### 8.4 查询 API 扁平化

查询 API 返回每个插件的独立索引（多插件 DLL 占多个索引）：

```cpp
int count = AxPlug::GetPluginCount();
for (int i = 0; i < count; i++) {
    auto info = AxPlug::GetPluginInfo(i);
    // i=0: IMath (MathPlugin.dll)
    // i=1: ITcpClient (TcpClientPlugin.dll)
    // i=2: ITcpClient/boost (TcpClientPlugin.dll)  // 同 DLL，不同索引
}
```

### 8.5 实现细节

- 多插件宏展开为静态数组，编译期确定，零运行时开销
- 入口点不存在时记录错误信息并卸载 DLL
- 重复 (typeId, implName) 对会被跳过，第一个注册的成为默认
- ABI 版本不匹配的插件会被跳过并记录错误

---

## 9. 新增插件检查清单

添加一个新插件需要：

1. **定义接口**
   - `include/<category>/IXxx.h`
   - 继承 `IAxObject`，添加 `AX_INTERFACE(IXxx)`

2. **实现类**
   - `src/<category>/XxxPlugin/include/Xxx.h`
   - 继承接口，实现所有纯虚函数
   - 添加 `void Destroy() override { delete this; }`（protected）

3. **导出**
   - `src/<category>/XxxPlugin/src/module.cpp`
   - 使用 `AX_BEGIN_PLUGIN_MAP()` + `AX_PLUGIN_TOOL(Xxx, IXxx)` + `AX_END_PLUGIN_MAP()`

4. **CMakeLists.txt**
   - 链接 `AxInterface`（不是 AxCore）
   - 定义 `AX_PLUGIN_EXPORTS`
   - 输出到 `${CMAKE_BINARY_DIR}/bin`

5. **注册到根 CMakeLists.txt**
   - `add_subdirectory(src/<category>/XxxPlugin)`

6. **验证**
   - 构建后检查 bin 目录下有对应 DLL
   - 运行测试，`GetPluginCount()` 应增加
   - `GetPluginInfo(i)` 显示正确的接口名和类型

---

## 10. 跨平台支持

### 10.1 当前状态

- **Windows**：完整支持（MSVC 2019+）
- **Linux**：架构支持（OSUtils.hpp 已有 Linux 分支），未经测试

### 10.2 平台差异隔离

所有平台差异通过 `OSUtils.hpp` 隔离：

| 操作 | Windows | Linux |
|------|---------|-------|
| 加载库 | `LoadLibraryW` | `dlopen` |
| 卸载库 | `FreeLibrary` | `dlclose` |
| 获取符号 | `GetProcAddress` | `dlsym` |
| 库扩展名 | `.dll` | `.so` |
| 搜索路径 | `SetDllDirectoryW` | `LD_LIBRARY_PATH` |
| 导出控制 | `__declspec(dllexport)` | `__attribute__((visibility))` |

### 10.3 移植注意事项

- 确保 `AX_PLUGIN_EXPORT` 宏在目标平台正确展开
- Linux 下需要 `-fPIC` 编译选项
- Linux 下需要设置 `RPATH` 或 `LD_LIBRARY_PATH`

---

## 11. 调试与排障

### 11.1 插件加载失败

每个加载失败的 DLL 仍会记录在 `modules_` 中，`isLoaded = false`，`errorMessage` 包含失败原因。可通过查询 API 检查：

```cpp
for (int i = 0; i < AxPlug::GetPluginCount(); i++) {
    auto info = AxPlug::GetPluginInfo(i);
    if (!info.isLoaded) {
        printf("FAIL: %s\n", info.fileName);
    }
}
```

### 11.2 常见问题

| 问题 | 排查方向 |
|------|----------|
| DLL 加载失败 | 运行时库不匹配（MD/MDd），缺少依赖 DLL |
| 入口点找不到 | 缺少 `AX_EXPORT_TOOL` / `AX_EXPORT_SERVICE`，或未定义 `AX_PLUGIN_EXPORTS` |
| 接口匹配失败 | 接口头文件缺少 `AX_INTERFACE(类名)` 宏 |
| Service 返回旧数据 | 前次实例未 Release，单例缓存仍在 |
| 跨 DLL 崩溃 | 检查是否在错误的模块中 delete 对象，确保 `Destroy()` 实现为 `{ delete this; }` |
| 析构顺序崩溃 | 插件对象引用了已销毁的其他对象，需在析构中做空值检查 |

### 11.3 增加日志

`AxPluginManager::CreateObject` 在找不到接口时会输出到 `std::cerr`。如需更多日志，可在以下位置添加输出：
- `LoadOnePlugin` — 加载每个 DLL 时
- `GetSingleton` — 创建/命中单例缓存时
- `ReleaseSingleton` — 释放单例时

---

## 12. 扩展框架的注意事项

### 12.1 添加新的 C 导出函数

1. 在 `AxPluginManager` 中添加方法
2. 在 `AxCoreDll.cpp` 中添加桥接函数
3. 在 `AxCoreDll.def` 中添加导出符号
4. 在 `AxPlug.h` 中添加 `extern "C"` 声明和 inline 封装

### 12.2 修改 AxPluginInfo 结构

`AxPluginInfo` 是插件 DLL 与 AxCore 之间的 ABI 契约。修改此结构会导致所有插件 DLL 不兼容，需要全量重编译。

### 12.3 接口命名规范

| 类别 | 前缀 | 示例 |
|------|------|------|
| 业务工具 | `I` + 业务名 | `IMath`, `IImageProcessor` |
| 核心服务 | `I` + 服务名 + `Service` | `ILoggerService`, `IImageUnifyService` |
| 驱动接口 | `I` + 协议名 | `ITcpClient`, `ITcpServer`, `IUdpSocket` |

### 12.4 关键约束

- **每个 DLL 可导出一个或多个插件**（通过 `GetAxPlugins` 入口点）
- **插件 DLL 不链接 AxCore**，只链接 `AxInterface`（header-only）
- **对象必须在创建它的 DLL 中销毁**（`Destroy()` 中 `delete this`）
- **不要在接口中使用 STL 容器作为参数或返回值**（跨 DLL ABI 不安全）
- **所有模块必须使用相同的 MSVC 运行时**（`/MD` 或 `/MDd`）

---

*AxPlug v1.0 · 开发与维护手册*
