# AxPlug 插件框架 · 使用与架构手册

> 本文档为插件框架的**使用手册**，涵盖框架概念、API 用法和插件开发。如需了解底层实现细节与维护指南，请参阅 [AxPlug_DEV.md](AxPlug_DEV.md)。

## 目录

1. [框架简介](#1-框架简介)
2. [核心概念](#2-核心概念)
3. [快速开始](#3-快速开始)
4. [使用 Tool（工具插件）](#4-使用-tool工具插件)
5. [使用 Service（服务插件）](#5-使用-service服务插件)
6. [命名绑定（接口→多实现）](#6-命名绑定接口多实现)
7. [查询已加载插件](#7-查询已加载插件)
8. [开发插件](#8-开发插件)
9. [核心服务](#9-核心服务)
10. [驱动插件 — TCP / UDP](#10-驱动插件--tcp--udp)
11. [高级特性](#11-高级特性)
12. [API 参考](#12-api-参考)
13. [使用注意事项与常见问题](#13-使用注意事项与常见问题)

---

## 1. 框架简介

无论你是 C++ 老手，还是刚刚接触 C++ 甚至编程的初学者，本指南都将带你无痛掌握 AxPlug 框架。

### 1.1 什么是插件框架？（小白通俗解释）

想象一下你的台式电脑：主板是整个电脑的核心（**宿主程序 Main EXE**），而显卡、声卡、U盘则是外接设备（**插件 DLL**）。
当你需要玩大型游戏时，你可以买一块新显卡**插**入主板，电脑瞬间就拥有了强大的图形处理能力，而你**不需要把整台电脑拆飞或者把主板退回原厂重新制造**。

**在软件开发中，AxPlug 完成的正是这样的工作：**
它允许我们将一个庞大的软件，拆分成无数个独立的 `.dll` (动态链接库) 文件。主程序在运行时，会自动去“扫描”并“插”上这些 DLL 文件。
- **好处 1**：你可以不停机、不重新编译整个几 GB 的源代码，就为软件增加新功能。
- **好处 2**：不同的开发者可以各自开发各自的 DLL 插件，互不干扰，最后大家把编好的文件丢到同一个文件夹里，软件就能自动组装并跑起来！

### 1.2 AxPlug 的核心大招 (特性一览)

为了让零基础的开发者也能写出不易崩溃、工业级的企业程序，AxPlug 预埋了超多“防呆”和“黑科技”特性：

- **极简接入**：对于使用者，只需在代码里写上一句 `#include "AxPlug/AxPlug.h"`，你就可以召唤任何插件了！根本不需要学习复杂的驱动配置。
- **傻瓜式内存管理 (自动打扫垃圾)**：C++ 新手最怕什么？内存泄漏（new 了之后忘记 delete 导致电脑内存占满卡死）。AxPlug 提供了强大的**智能指针** (`AxPtr<T>`)。你尽管去创建使用，一旦该变量离开了生效大括号 `{ }`，框架会自动帮你释放内存，绝不泄漏。
- **分类清晰的业务模型 (Tool 与 Service)**：
  - **Tool（工具型）**：就像买铅笔，你每调一次创建接口，就会发给你一支全新的、独立的笔。
  - **Service（服务型）**：就像公司里的前台饮水机。无论多少个部门来提申请，框架永远只返回同一台饮水机（单例模式），确保大家喝的是同一个桶里的水（数据共享状态一致）。
- **坚不可摧的异常防线**：如果某个新手写的插件内部发生了不可挽回的代码崩溃抛错，AxPlug 拥有一套跨模块的异常保护网，它会尽量拦住错误并保存为错误码，而不会让你的主程序跟着陪葬“闪退”。
- **性能探测器 (Profiler)**：内置了极为先进的执行时间监视器。代码运行卡顿？一行命令就能帮你生成 Chrome 分析文件，让你知道时间全耗在了哪个函数的哪一行。
- **严格的防呆编译检查**：如果你在代码里不小心乱转对象类型（比如把“狗”强转成了“猫”去调用），AxPlug 独家的 `static_assert` （编译期断言）会在你点击“编译”的瞬间直接报错拦截，不把隐患留到运行期。

---

## 2. 核心概念

要流畅地使用框架，你需要理解以下三个核心概念：

### 2.1 两种插件形态：Tool 与 Service

框架把所有插件提供的功能，严格划分成了两种生命周期。下表列出了它们的区别：

| 类型特点 | 通俗比喻 | 每次获取拿到的东西 | 如何释放内存？ | 推荐使用场景 |
|------|----------|--------|----------|----------|
| **Tool** | **“铅笔”** | 每次调用都会得到一个**全新独立**的对象实例。你拿到的和别人拿到的不是同一个。 | 利用智能指针，当大括号 `}` 结束会自动销毁释放。 | 比如：一个独立的数学计算器、一个独立的图形绘制笔刷。 |
| **Service** | **“饮水机”** | 无论你在哪里调用多少次，拿到的**永远是同一个**对象实例（全局单例）。 | 由框架自己接管，在整个程序最终关闭退出时，框架才会统一释放它。 | 比如：负责记录全服运行状态的 Logger 日志服务，或管理全局网络的网关服务。 |

> **提示**：除了极少数对极致性能有极度苛刻要求的底层场景外，请**永远**使用只能指针版本获取 Tool（`CreateTool`），而绝对不要使用手动管理的原始指针版（`CreateToolRaw`），这能帮你躲掉 99% 的内存刺客报错。

### 2.2 契约精神：接口暴露与类型系统

在 AxPlug 世界里，主程序和插件之间，或者插件和插件之间，是互相**绝对不认识**对方长什么样子的。它们只能通过一份叫做**“接口 (Interface)”**的合同来交流。

**步骤 1：定义一份接口合同**
所有的合同必须继承自框架的一把手基类 `IAxObject`，并且在里面必须写上一句“天地盖戳”的话：`AX_INTERFACE(你的接口名字)`。
这句话是个宏魔法，它会在编译器就把你的名字变成一串唯一的数字指纹（64位长），极大地加快了以后的运行找寻速度。

```cpp
// ==== IMath.h ==== (这是一份公开的合同，任何想要使用数学功能的都看这份合同)

#include "AxPlug/IAxObject.h"

// 继承 IAxObject 表明我是 AxPlug 大家庭的合法接口
class IMath : public IAxObject {
    
    // 强制声明，让框架打下唯一的类型指纹烙印！这句少写编译直接报错！
    AX_INTERFACE(IMath)

public:
    // 定义我们能干什么（纯虚函数），至于怎么干的，我们不在这里写。
    virtual int Add(int a, int b) = 0;
    virtual int Sub(int a, int b) = 0;
};
```

**步骤 2：警惕跨模块（跨 DLL）传参的“剧毒”**

当你在定义如上面 `Add` 函数的参数时，**请务必注意**！
因为各个插件的 DLL 可能是由不同的部门用不同的编译器版本（哪怕同是 VS，版本号不同也算）编译出来的，所以：
> 🚫 **极度危险行为**：在接口参数里传递 C++ 标准库复杂类型，如 `std::vector` 或 `std::string` 或 `std::map`。因为不同编译器对它们的底层排版布局可能不同，一传递就会直接导致**内存错乱甚至内存访问违规（Access Violation）闪退！**

> ✅ **十分推荐的安全做法**：
> 1. 大量使用纯 C 语言的基础类型（`int`, `float`, `double`, `bool`, `const char*`等）。
> 2. 如果一定要传递复杂的大片数据块，请传递“指针 + 长度”的组合形式。
> 3. 如果函数要返回一个对象给别人，**必须**返回由框架派发的智能指针（如同 `AxPtr`）。

### 2.3 宏观架构：我们是如何组装的？

让我们用一张图来看看整个大系统的样貌：

```text
======================= 【第一层：宿主】 =======================
宿主程序 (Main.exe)
  │  1. 包含了头文件 #include "AxPlug/AxPlug.h"
  │  2. 程序一运行就大喊一声：AxPlug::Init() ！(启动系统引擎)
  │  3. 想要一把新计算器： AxPlug::CreateTool<IMath>()
  │  4. 找全局打杂记事员： AxPlug::GetService<ILoggerService>("app")
  │
======================= 【第二层：总管】 =======================
  ├── AxCore.dll              ← 神奇的“收发室总管” (插件管理器核心)
  │                             它的任务是扫描所有的文件夹，把下面零散
  │                             的插件默默登记造册，并响应上层的索要请求。
  │
======================= 【第三层：打工人】 =======================
  ├── MathPlugin.dll          ← Tool 插件（这就是某团队写的具体的实现逻辑封装包）
  ├── LoggerPlugin.dll        ← Service 插件（全局唯一的记录员在这里待命）
  ├── TcpClientPlugin.dll     ← 多插件 DLL (一个 DLL 里甚至可以打包好几个类型的插件)
  └── ...
```
通过上述结构，我们的 `Main.exe` 就可以写得非常精简轻量了。所有的杂活重活，全部分摊到了底层的各个独立包 (`.dll`) 里面去干。

---

## 3. 快速开始（手把手保姆级教程）

### 3.1 前期准备（你需要什么环境）
在开始之前，我们需要配置好生产车间的机床：
- [x] **C++17标准支持**：确保你的编译器不要太古老（至少支持到 C++17）。
- [x] **CMake 3.15 及以上版本**：这是用来帮你把代码组装变成工程文件（比如 VS 的 .sln）的自动化构建指挥官。
- [x] **Visual Studio 2019 或 2022（Windows 环境）**：安装时请务必确保勾选了“使用 C++ 的桌面开发”组件包。

### 3.2 第一行代码：跑起你的首个插件

让我们来看看如何在主程序（你的 `main.cpp`）里优雅地调用别人的插件（假设我们想要用数学计算器插件做加法，并打印一段很酷的日志）：

```cpp
// 引入总司令：这是框架为你暴露的唯一的大门钥匙。
// 哪怕后续工程里有成百上千个插件，你也永远只需要引入这一个框架头文件！
#include "AxPlug/AxPlug.h"

// 引入你想用的那些“合同”（业务接口声明，告诉你有啥可以用）
#include "business/IMath.h"
#include "core/LoggerService.h"

int main() {
    // 【步骤 1】点火启动系统引擎！
    // 这一魔法步骤会让 AxCore.dll 去偷偷扫描你当前 exe 所在目录下的所有 .dll 文件，并把它们全部登记造册。
    AxPlug::Init();

    // 【步骤 2】向系统讨要一个“全职记录员”（也就是获取 Service 插件）
    // 这里的传入的字符串 "main" 是给这个记事员起的名字。
    // 下次不论你在代码的哪个角落，只要你再喊一声 "main"，拿到的绝对是这一位大叔，这就是服务(Service)单例的魅力。
    auto* logger = AxPlug::GetService<ILoggerService>("main");
    logger->Info("恭喜你，AxPlug 插件框架被成功唤醒啦！");

    // 我们加一个大括号，是故意划定一个生存作用域，体现自动管理的威力
    {
        // 【步骤 3】向系统借一把“计算器”（也就是获取 Tool 插件）
        // 注意：math 是一个智能指针。你可以把它想象成带了自爆芯片的高级定时器。
        auto math = AxPlug::CreateTool<IMath>();
        
        // 痛快地使用你的计算器插件做事吧
        int result = math->Add(10, 20);
        logger->InfoFormat("计算完毕: 10 + 20 的结果是 = %d", result);
        
    } // 【超级重点】看！大括号在这里结束了！
      // math 变量离开了它的存活区域。就在这一瞬间，AxPlug 框架在后台悄无声息帮你完美释放了这把计算器。
      // 作为新手的你，再也不用提心吊胆去猜到底什么地方该写 delete，再也不怕内存爆炸了！安全无痛！

    // 【步骤 4】程序临近尾声，我们不再需要记录员了，出于好习惯，我们把他送走
    AxPlug::ReleaseService<ILoggerService>("main");
    
    // 进程结束！剩下的任何未解绑的残骸，AxCore 会在后台进行最后的“地毯式安全清理”，杜绝一切隐患。
    return 0; 
}
```

### 3.3 怎么把上述文字变成能跑的程序包？（构建篇）

代码写好了，我们需要让编译器把它们转化为机器能懂的最终文件。我们为你提供了两种路线：

**路线一: 一键傻瓜式脚本构建（强烈推荐！点一下就出锅）**

在工程的主目录的 `scripts` 文件夹下，我们为你准备好了一键批处理文件 (`.bat`)。直接双击运行它，或者在命令行里敲入：
```bash
# 如果你想找 Bug（编译较慢，但会包含所有调试探测器信息）
scripts\build_debug_no_test.bat

# 如果你要发给客户正式使用（极速编译，性能火力全开，体积更小）
scripts\build_release_no_test.bat
```
静静等待片刻后，所有的产物会自动装配好，躺在新建好的名为 `publish` 的文件夹里。拿着这个文件夹，你就可以直接当成果物发布了！这是完整的 SDK（软件开发包）。

**路线二: 极客级纯纯的手工 CMake 编译（适合想弄懂底层逻辑的高手）**

打开命令行（CMD/PowerShell），在项目最顶层的目录下依次输入这神秘的老三样咒语：

```bash
# 第一步：让 CMake 担任“包工头”，阅读图纸帮你生成 VS2022 的原生“工地建筑底座”（工程文件）
cmake -S . -B build -G "Visual Studio 17 2022" -A x64

# 第二步：命令底座开始盖楼！疯狂干活，真正把代码铸造出所有的 DLL 和 EXE 可执行文件（这里选择 Debug 调试风格）
cmake --build build --config Debug

# 第三步：把散乱在各个角落刚造好的核心积木，小心翼翼地提取出来，打包封箱装进名叫 publish 的礼盒里
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

本节详细介绍插件开发所需的**全部宏、基类和结构体**，覆盖从接口定义到 DLL 导出的完整流程。

### 8.1 宏与基类速查表

| 宏 / 类 | 头文件 | 用途 |
|---------|--------|------|
| `AX_INTERFACE(Name)` | `IAxObject.h` | **接口定义宏** — 注入编译期类型名和类型 ID |
| `AxPluginImpl<TImpl, TInterfaces...>` | `AxPluginImpl.h` | **CRTP 基类** — 自动实现 `Destroy()`、`OnInit()`、`OnShutdown()` |
| `AX_AUTO_REGISTER_TOOL(TClass, IType)` | `AxAutoRegister.h` | **★推荐** — 自动注册 Tool 插件 |
| `AX_AUTO_REGISTER_TOOL_NAMED(TClass, IType, Name)` | `AxAutoRegister.h` | **★推荐** — 自动注册命名 Tool 插件 |
| `AX_AUTO_REGISTER_SERVICE(TClass, IType)` | `AxAutoRegister.h` | **★推荐** — 自动注册 Service 插件 |
| `AX_AUTO_REGISTER_SERVICE_NAMED(TClass, IType, Name)` | `AxAutoRegister.h` | **★推荐** — 自动注册命名 Service 插件 |
| `AX_DEFINE_PLUGIN_ENTRY()` | `AxAutoRegister.h` | **★推荐** — 生成 DLL 导出入口函数（每个 DLL 写一次） |
| `AX_BEGIN_PLUGIN_MAP()` | `AxPluginExport.h` | 手动插件表开始（旧方式） |
| `AX_PLUGIN_TOOL(TClass, IType)` | `AxPluginExport.h` | 手动注册 Tool（旧方式） |
| `AX_PLUGIN_TOOL_NAMED(TClass, IType, Name)` | `AxPluginExport.h` | 手动注册命名 Tool（旧方式） |
| `AX_PLUGIN_SERVICE(TClass, IType)` | `AxPluginExport.h` | 手动注册 Service（旧方式） |
| `AX_PLUGIN_SERVICE_NAMED(TClass, IType, Name)` | `AxPluginExport.h` | 手动注册命名 Service（旧方式） |
| `AX_END_PLUGIN_MAP()` | `AxPluginExport.h` | 手动插件表结束（旧方式） |
| `AX_PLUGIN_EXPORT` | `AxPluginExport.h` | DLL 导出/导入控制（`__declspec(dllexport/dllimport)`） |
| `AX_PLUGIN_ABI_VERSION` | `AxPluginExport.h` | ABI 版本号（当前为 1），破坏性变更时递增 |
| `AxPluginInfo` | `AxPluginExport.h` | 插件描述结构体（接口名、类型 ID、创建函数等） |
| `AxPtr<T>` | `IAxObject.h` | 智能指针别名 (`std::shared_ptr<T>`) |

---

### 8.2 第一步：定义接口 — `AX_INTERFACE`

每个插件接口必须继承 `IAxObject`，并在类体内使用 `AX_INTERFACE(接口名)` 宏。

```cpp
// include/business/IMath.h
#pragma once
#include "AxPlug/IAxObject.h"

class IMath : public IAxObject {
    AX_INTERFACE(IMath)         // ← 必须！注入 ax_interface_name 和 ax_type_id
public:
    virtual int Add(int a, int b) = 0;
    virtual int Sub(int a, int b) = 0;
};
```

**`AX_INTERFACE(Name)` 展开后注入两个静态成员：**

| 成员 | 类型 | 说明 |
|------|------|------|
| `ax_interface_name` | `const char*` | 编译期字符串字面量，值为 `"IMath"` |
| `ax_type_id` | `uint64_t` | 编译期 FNV-1a 哈希值，用于 O(1) 接口匹配 |

> **缺少此宏** 会导致 `CreateTool<T>()` / `GetService<T>()` 编译失败，并提示找不到 `ax_type_id`。

---

### 8.3 第二步：实现类 — `AxPluginImpl`（推荐）

使用 CRTP 基类 `AxPluginImpl` 可自动获得 `Destroy()`、`OnInit()`、`OnShutdown()` 的默认实现，**消除样板代码**。

```cpp
// src/business/MathPlugin/include/MathPlugin.h
#pragma once
#include "business/IMath.h"
#include "AxPlug/AxPluginImpl.h"

// ★ 推荐方式：继承 AxPluginImpl，无需手写 Destroy()
class CMath : public AxPluginImpl<CMath, IMath> {
public:
    int Add(int a, int b) override { return a + b; }
    int Sub(int a, int b) override { return a - b; }
    // Destroy() 已由 AxPluginImpl 自动实现
    // OnInit() / OnShutdown() 有空默认实现，按需重写
};
```

**多接口实现：**

```cpp
class CAdvancedMath : public AxPluginImpl<CAdvancedMath, IMath, IAdvancedMath> {
public:
    int Add(int a, int b) override { return a + b; }
    double Sqrt(double x) override { return std::sqrt(x); }
};
```

**`AxPluginImpl` 内置的编译期安全检查：**
- 所有 `TInterfaces` 必须继承自 `IAxObject`，否则 `static_assert` 编译报错
- 至少需要一个接口参数

**不使用 AxPluginImpl 的手动方式（不推荐但支持）：**

```cpp
class CMath : public IMath {
public:
    int Add(int a, int b) override { return a + b; }
    int Sub(int a, int b) override { return a - b; }
protected:
    void Destroy() override { delete this; }  // ← 必须手动实现
};
```

---

### 8.4 第三步：导出插件

框架提供两种导出方式：**自动注册（推荐）** 和 **手动插件表（旧方式）**。

#### 方式一：自动注册宏（★推荐）

自动注册无需手写插件映射表，只需在实现文件中放置注册宏，并在一个 `module.cpp` 中生成入口点。

**单插件 DLL：**

```cpp
// src/business/MathPlugin/src/MathPlugin.cpp
#include "MathPlugin.h"
#include "AxPlug/AxAutoRegister.h"

AX_AUTO_REGISTER_TOOL(CMath, IMath)    // ← 在实现文件中就地注册
```

```cpp
// src/business/MathPlugin/src/module.cpp (每个 DLL 一份)
#include "AxPlug/AxAutoRegister.h"

AX_DEFINE_PLUGIN_ENTRY()               // ← 生成 GetAxPlugins 导出函数
```

**多插件 DLL（含命名绑定）：**

```cpp
// TcpServer.cpp
#include "AxPlug/AxAutoRegister.h"
AX_AUTO_REGISTER_TOOL(TcpServer, ITcpServer)

// BoostTcpServer.cpp
#include "AxPlug/AxAutoRegister.h"
AX_AUTO_REGISTER_TOOL_NAMED(BoostTcpServer, ITcpServer, "boost")

// LoggerService.cpp
#include "AxPlug/AxAutoRegister.h"
AX_AUTO_REGISTER_SERVICE(LoggerServiceImpl, ILoggerService)

// module.cpp (整个 DLL 仅需一份)
#include "AxPlug/AxAutoRegister.h"
AX_DEFINE_PLUGIN_ENTRY()
```

**自动注册宏详解：**

| 宏 | 参数 | 说明 |
|-----|------|------|
| `AX_AUTO_REGISTER_TOOL(TClass, IType)` | 实现类, 接口类 | 注册为默认 Tool 插件 |
| `AX_AUTO_REGISTER_TOOL_NAMED(TClass, IType, Name)` | 实现类, 接口类, 名称字符串 | 注册为命名 Tool 插件 |
| `AX_AUTO_REGISTER_SERVICE(TClass, IType)` | 实现类, 接口类 | 注册为默认 Service 插件 |
| `AX_AUTO_REGISTER_SERVICE_NAMED(TClass, IType, Name)` | 实现类, 接口类, 名称字符串 | 注册为命名 Service 插件 |
| `AX_DEFINE_PLUGIN_ENTRY()` | 无 | 生成 `GetAxPlugins` C 导出函数，收集所有自动注册项 |

**工作原理**：每个 `AX_AUTO_REGISTER_*` 宏创建一个静态 `AutoRegistrar` 对象，在 DLL 加载时通过静态初始化将注册 lambda 推入 Meyers 单例列表。`AX_DEFINE_PLUGIN_ENTRY()` 生成的 `GetAxPlugins` 函数遍历该列表，收集所有 `AxPluginInfo` 返回给框架。

#### 方式二：手动插件表（旧方式，兼容保留）

```cpp
// module.cpp
#include "MathPlugin.h"
#include "AxPlug/AxPluginExport.h"

AX_BEGIN_PLUGIN_MAP()
    AX_PLUGIN_TOOL(CMath, IMath)
    AX_PLUGIN_SERVICE(LoggerServiceImpl, ILoggerService)
    AX_PLUGIN_TOOL_NAMED(BoostTcpServer, ITcpServer, "boost")
AX_END_PLUGIN_MAP()
```

| 宏 | 说明 |
|-----|------|
| `AX_BEGIN_PLUGIN_MAP()` | 开始插件表，生成 `GetAxPlugins` 函数头 |
| `AX_PLUGIN_TOOL(TClass, IType)` | 声明一个默认 Tool 项 |
| `AX_PLUGIN_TOOL_NAMED(TClass, IType, Name)` | 声明一个命名 Tool 项 |
| `AX_PLUGIN_SERVICE(TClass, IType)` | 声明一个默认 Service 项 |
| `AX_PLUGIN_SERVICE_NAMED(TClass, IType, Name)` | 声明一个命名 Service 项 |
| `AX_END_PLUGIN_MAP()` | 结束插件表，关闭函数体 |

> **推荐使用自动注册方式**：无需在 `module.cpp` 中手动列出所有插件，新增插件只需在实现文件添加一行注册宏即可。

---

### 8.5 AxPluginInfo — 插件描述结构体

每个导出宏最终都会生成一个 `AxPluginInfo` 实例，框架据此加载和管理插件：

```cpp
struct AxPluginInfo {
    const char* interfaceName;      // 接口名，如 "IMath"
    uint64_t    typeId;             // FNV-1a 哈希（由 AX_INTERFACE 生成）
    AxPluginType type;              // Tool 或 Service
    IAxObject* (*createFunc)();     // 对象创建工厂函数
    const char* implName;           // 实现名，如 "boost"，默认为 ""
    uint32_t    abiVersion;         // ABI 版本号（AX_PLUGIN_ABI_VERSION）
};
```

| 字段 | 由谁填充 | 说明 |
|------|---------|------|
| `interfaceName` | `InterfaceType::ax_interface_name` | 来自 `AX_INTERFACE` 宏 |
| `typeId` | `InterfaceType::ax_type_id` | 编译期 FNV-1a 哈希 |
| `type` | 导出宏决定 | `AxPluginType::Tool` 或 `AxPluginType::Service` |
| `createFunc` | 导出宏生成 lambda | `[]() -> IAxObject* { return new TClass(); }` |
| `implName` | 开发者指定 | 命名绑定标识，默认空字符串 |
| `abiVersion` | `AX_PLUGIN_ABI_VERSION` | 当前值为 1，框架加载时校验兼容性 |

---

### 8.6 AX_PLUGIN_EXPORT — DLL 导出控制

```cpp
// 在 CMakeLists.txt 中定义 AX_PLUGIN_EXPORTS：
target_compile_definitions(MathPlugin PRIVATE AX_PLUGIN_EXPORTS)
```

| 条件 | 展开为 |
|------|--------|
| Windows + `AX_PLUGIN_EXPORTS` 已定义 | `__declspec(dllexport)` |
| Windows + 未定义 | `__declspec(dllimport)` |
| Linux + `AX_PLUGIN_EXPORTS` 已定义 | `__attribute__((visibility("default")))` |
| Linux + 未定义 | （空） |

> 插件 DLL 需要定义 `AX_PLUGIN_EXPORTS`，宿主程序不定义。

---

### 8.7 CMakeLists.txt

```cmake
add_library(MathPlugin SHARED src/MathPlugin.cpp src/module.cpp)
target_include_directories(MathPlugin PRIVATE include ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(MathPlugin PRIVATE AxInterface)
target_compile_definitions(MathPlugin PRIVATE AX_PLUGIN_EXPORTS)
```

> `AxInterface` 是仅包含头文件的 interface library，提供 `include/AxPlug/` 和 `include/core/` 路径。

---

### 8.8 Service 插件的生命周期钩子

Service 插件可重写 `OnInit()` 和 `OnShutdown()`：

```cpp
class MyService : public AxPluginImpl<MyService, IMyService> {
public:
    void OnInit() override {
        // 初始化逻辑 — 可安全调用其他 Service
        auto logger = AxPlug::GetService<ILoggerService>();
    }
    void OnShutdown() override {
        // 清理逻辑 — 框架退出时逆序调用
    }
};
```

| 钩子 | 调用时机 | 说明 |
|------|---------|------|
| `OnInit()` | 首次 `GetService` 创建单例后立即调用 | 可在此获取其他服务引用 |
| `OnShutdown()` | 框架析构时，按创建逆序调用 | 用于释放资源、停止线程等 |

---

### 8.9 完整插件开发检查清单

1. ✅ 接口类继承 `IAxObject`，类体内有 `AX_INTERFACE(接口名)`
2. ✅ 实现类继承 `AxPluginImpl<实现类, 接口类>`（或手动实现 `Destroy()`）
3. ✅ 实现文件中有 `AX_AUTO_REGISTER_TOOL` 或 `AX_AUTO_REGISTER_SERVICE`
4. ✅ DLL 中有且仅有一个 `module.cpp` 包含 `AX_DEFINE_PLUGIN_ENTRY()`
5. ✅ CMakeLists.txt 中定义了 `AX_PLUGIN_EXPORTS`
6. ✅ 链接了 `AxInterface`
7. ✅ 接口虚函数参数不使用 STL 容器（跨 DLL ABI 不安全）
8. ✅ 所有模块使用相同 MSVC 运行时（`/MD` 或 `/MDd`）

---

## 9. 核心服务

### 9.1 日志服务 (ILoggerService)

```cpp
#include "core/LoggerService.h"

auto logger = AxPlug::GetService<ILoggerService>("app");

logger->Info("消息");
logger->Warn("警告");
logger->Error("错误");
logger->Debug("调试");

logger->SetLevel(LogLevel::Debug);
logger->EnableConsoleOutput(true);
logger->SetLogFile("app.log");
```

**日志级别**：`Trace` → `Debug` → `Info` → `Warn` → `Error` → `Critical`

### 9.2 图像统一服务 (IImageUnifyService)

```cpp
#include "core/IImageUnifyService.h"

auto svc = AxPlug::GetService<IImageUnifyService>();

// 推荐：RAII 方式
{
    ScopedFrame frame(svc.get(), data, 1920, 1080, PixelFormat::U8_C3);
    ScopedView planar(svc.get(), frame.Id(), MemoryLayout::Planar);
    // 使用 planar.R() / planar.G() / planar.B() ...
} // 自动释放
```

详见 [ImageUnifyService.md](ImageUnifyService.md) 和 [ImageUnifyService_DEV.md](ImageUnifyService_DEV.md)。

### 9.3 事件总线 (IEventBus)

框架内置轻量级事件总线，支持同步/异步派发、跨进程 UDP 多播。

详见 [EventBus.md](EventBus.md) 和 [EventBus_DEV.md](EventBus_DEV.md)。

---

## 10. 驱动插件 — TCP / UDP

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

## 11. 高级特性

### 11.1 智能指针 (AxPtr)

```cpp
{
    AxPtr<IMath> math = AxPlug::CreateTool<IMath>();
    AxPtr<IMath> copy = math;   // use_count = 2
    copy.reset();               // use_count = 1
} // 自动调用 Destroy()
```

### 11.2 内置 Profiler

```cpp
AxPlug::ProfilerBegin("MyApp", "trace.json");

void MyFunction() {
    AX_PROFILE_FUNCTION();
    // ...
}

AxPlug::ProfilerEnd();
// 在 chrome://tracing 中打开 trace.json 可视化
```

### 11.3 异常处理与错误码

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

### 11.4 编译期安全检查

框架在编译期自动校验模板参数的合法性：
```cpp
AxPlug::CreateTool<int>();    // 编译错误：T 必须继承自 IAxObject
AxPlug::CreateTool<Bad>();    // 编译错误：缺少 AX_INTERFACE 宏定义
```

---

## 12. API 参考

所有 `AxPlug` 命名空间 API 均为**线程安全**。

### 12.1 初始化

| 函数 | 说明 |
|------|------|
| `void Init(const char* pluginDir = "")` | 初始化框架并加载指定目录下所有插件 DLL。为空则扫描 exe 所在目录 |

### 12.2 Tool API

| 函数 | 说明 |
|------|------|
| `shared_ptr<T> CreateTool<T>()` | 创建默认实现的 Tool 实例（智能指针，自动释放） |
| `shared_ptr<T> CreateTool<T>(implName)` | 创建命名实现的 Tool 实例 |
| `T* CreateToolRaw<T>()` | 创建默认实现的 Tool 裸指针（需手动 `DestroyTool`） |
| `T* CreateToolRaw<T>(implName)` | 创建命名实现的 Tool 裸指针 |
| `void DestroyTool(shared_ptr<T>&)` | 显式释放智能指针管理的 Tool |
| `void DestroyTool(IAxObject*)` | 释放裸指针 Tool |

### 12.3 Service API

| 函数 | 说明 |
|------|------|
| `shared_ptr<T> GetService<T>(name = "")` | 获取命名单例（懒初始化，自动 UAF/SIOF 保护） |
| `void ReleaseService<T>(name = "")` | 释放单例引用（引用计数归零时析构） |
| `pair<shared_ptr<T>, AxInstanceError> TryGetService<T>(name)` | noexcept 版 GetService，适用于析构路径 |
| `T* GetServiceRaw<T>(name = "")` | 获取裸指针（兼容模式，不推荐新代码使用） |

### 12.4 查询 API

| 函数 | 说明 |
|------|------|
| `int GetPluginCount()` | 已注册插件总数 |
| `AxPluginQueryInfo GetPluginInfo(int index)` | 按索引查询插件信息（fileName, interfaceName, isTool, isLoaded） |
| `vector<AxPluginQueryInfo> FindImplementations<T>()` | 查找接口 T 的所有可用实现 |

### 12.5 Profiler & Error API

| 函数 | 说明 |
|------|------|
| `void ProfilerBegin(name, filepath)` | 启动性能分析，输出 Chrome Trace JSON |
| `void ProfilerEnd()` | 停止分析并刷盘 |
| `const char* GetLastError()` | 获取当前线程最近一次框架错误消息 |
| `bool HasError()` | 是否有未处理错误 |
| `void ClearLastError()` | 清除错误状态 |

### 12.6 Event Bus API

| 函数 | 说明 |
|------|------|
| `IEventBus* GetEventBus()` | 获取全局事件总线 |
| `void SetEventBus(IEventBus*)` | 替换全局事件总线（高级：用于网络总线夺舍） |
| `void Publish(eventId, payload, mode)` | 发布事件 |
| `EventConnectionPtr Subscribe(eventId, callback, sender)` | 订阅事件 |
| `void SetExceptionHandler(handler)` | 设置事件回调全局异常处理器 |

---

## 13. 使用注意事项与常见问题

### 13.1 注意事项

- **禁止**直接 `delete` 插件对象，必须通过 `DestroyTool` 或智能指针释放
- `AX_PROFILE_SCOPE(name)` 的 `name` 必须是**静态字符串**
- 在 `OnShutdown()` 中**避免**调用 `GetService()`，应在 `OnInit()` 时缓存引用
- 网络插件的 socket 对象应在 DLL 卸载前释放
- 接口虚函数参数**不要**使用 `std::string` / `std::vector`（跨 DLL ABI 不安全）
- 所有模块必须使用相同的 MSVC 运行时（`/MD` 或 `/MDd`）

### 13.2 常见问题

| 现象 | 解决方案 |
|------|----------|
| `CreateTool` 返回 nullptr | 检查插件 DLL 是否在 exe 目录，接口是否有 `AX_INTERFACE` 宏，检查 `AxPlug::GetLastError()` |
| 命名绑定返回 nullptr | 检查 `implName` 拼写，确认插件使用了 `AX_PLUGIN_TOOL_NAMED` 宏 |
| 链接错误：找不到 `Ax_*` 函数 | 宿主程序 CMake 中添加 `target_link_libraries(... AxCore)` |
| Service 返回旧实例 | 先 `ReleaseService<T>(name)` 再重新 `GetService` |
| DLL 加载失败 | 确保所有模块使用相同的 MSVC 运行时 (MD/MDd)，检查依赖 DLL 是否齐全 |
| 接口匹配失败 | 确认接口头文件中有 `AX_INTERFACE(类名)` 宏 |
| Profiler 无输出 | 确认调用了 `ProfilerBegin()` 和 `ProfilerEnd()` |
| 跨 DLL 崩溃 | 检查接口参数是否使用了 STL 容器，确保 `Destroy()` 实现为 `{ delete this; }` |

---

> 框架底层架构、源码结构、线程安全机制等开发维护细节，请参阅 [AxPlug_DEV.md](AxPlug_DEV.md)。

*AxPlug v1.0 · 使用手册*

