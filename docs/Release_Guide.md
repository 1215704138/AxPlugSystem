# AxPlug SDK 发布指南

本文档用于指导如何发布 AxPlug SDK，以及外部开发者如何使用 SDK 进行二次开发。

---

## 1. SDK 发布流程

### 1.1 生成 SDK 包

使用项目提供的自动化脚本生成 SDK：

| 脚本 | 说明 |
|------|------|
| `scripts\build_debug_no_test.bat` | Debug 版 SDK（推荐开发阶段使用） |
| `scripts\build_debug_with_test.bat` | Debug 版 SDK + 测试 |
| `scripts\build_release_no_test.bat` | Release 版 SDK（推荐发布使用） |
| `scripts\build_release_with_test.bat` | Release 版 SDK + 测试 |

脚本执行三步：Configure → Build → Install。生成的 `publish/` 目录即为 **SDK 包**。

### 1.2 SDK 目录结构

```
publish/
├── bin/
│   ├── Debug/  或  Release/
│   │   ├── AxCore.dll           # [核心] 插件管理器
│   │   ├── AxCore.lib           # 链接库
│   │   ├── MathPlugin.dll       # 业务插件
│   │   ├── LoggerPlugin.dll     # 核心服务插件
│   │   ├── ImageUnifyPlugin.dll # 图像统一服务插件
│   │   ├── TcpClientPlugin.dll  # 驱动插件 (多实现)
│   │   ├── TcpServerPlugin.dll  # 驱动插件 (多实现)
│   │   └── UdpSocketPlugin.dll  # 驱动插件 (多实现)
└── include/
    ├── AxPlug/                  # 框架头文件
    │   ├── AxPlug.h             # 用户唯一入口
    │   ├── IAxObject.h          # 接口基类 + AX_INTERFACE 宏
    │   ├── AxPluginExport.h     # 插件导出宏
    │   ├── AxProfiler.h         # 性能分析器
    │   ├── AxException.h        # 异常处理
    │   └── OSUtils.hpp          # 跨平台工具
    ├── core/                    # 核心服务接口
    ├── driver/                  # 驱动接口
    └── business/                # 业务接口
```

---

## 2. 外部开发者如何将 AxPlug 接入自己的项目？（保姆级大白话指南）

当你拿到了名为 `publish/` 的 SDK 压缩包后，怎么把它融合进你自己的代码里呢？

### 2.1 避坑第一条：环境与编译选项统一（极其重要！！！）

很多新手在接入 C++ 动态库时，经常遇到莫名其妙的内存崩溃或“链接不到符号”的问题，90% 都是因为没有遵守以下铁律：
- **C++17 标准**：请确保你的编译器设置里开启了 C++17。如果用 Visual Studio，在项目属性的“C++语言标准”里选择 `/std:c++17`。
- **运行时库必须绝对一致**：C++ 编译器有一项设置名叫“运行库”。
  - 如果你是 Debug 调试编译，整个项目的所有 DLL 和 EXE，必须**全部设置为 `/MDd` (多线程调试 DLL)**。
  - 如果你是 Release 正式编译，必须**全部设置为 `/MD` (多线程 DLL)**。
  - 只要有一个文件私自用了 `/MT` 或者混用了 `/MDd` 和 `/MD`，你的程序跑起来就会直接在底层崩溃。

### 2.2 我想写个新插件给别人用，该怎么配置 CMake？

写插件最爽的一点是：**你只需要框架的头文件（也就是图纸），你不需要链接笨重的 Core DLL！**
这样写出来的插件，体积非常小，且非常独立。

看下面这个最简 CMake 配置模板。你只要照抄，改个名字就能跑：

```cmake
cmake_minimum_required(VERSION 3.15)
# 取个好听的插件工程名
project(MySuperToolPlugin)

# 告诉 CMake，你拿到的 SDK publish 文件夹放在了哪里（最好用绝对路径）
set(AXPLUG_SDK_DIR "C:/path/to/publish")

# 你的插件包含了两个文件，一个是 .cpp，一个是用于导出的 module.cpp
add_library(MySuperToolPlugin SHARED src/MyPlugin.cpp src/module.cpp)

# 【核心步骤 1】告诉编译器去哪里找 AxPlug.h 那些头文件图纸
target_include_directories(MySuperToolPlugin PRIVATE ${AXPLUG_SDK_DIR}/include)

# 【核心步骤 2】打上魔法印记，向系统声明：我是一个插件！
target_compile_definitions(MySuperToolPlugin PRIVATE AX_PLUGIN_EXPORTS)

# 【小贴士】看到没有？这里根本不用写 target_link_libraries 去链接 AxCore.lib！因为我们只要头文件！
```

而在你的 `module.cpp` 里面，只需两句话就把你的实现变成框架插件了：

```cpp
#include "MyPlugin.h"
#include "AxPlug/AxPluginExport.h"

// 像三明治一样把你要暴露出去的类包在中间
AX_BEGIN_PLUGIN_MAP()
    // 告诉框架：有人要 IMyInterface 的时候，把 MyPlugin 给他
    AX_PLUGIN_TOOL(MyPlugin, IMyInterface)
AX_END_PLUGIN_MAP()
```

### 2.3 我想写个主程序来加载一堆插件玩，该怎么配置？

跟写插件不同，写主程序（宿主 EXE）是需要真正启动大管家的，所以**必须去链接 `AxCore.lib`**。

这是宿主 EXE 的 CMake 配置模板：

```cmake
cmake_minimum_required(VERSION 3.15)
project(MyGameApp)

set(AXPLUG_SDK_DIR "C:/path/to/publish")

add_executable(MyGameApp src/main.cpp)

# 【核心步骤 1】同样需要看图纸
target_include_directories(MyGameApp PRIVATE ${AXPLUG_SDK_DIR}/include)

# 【核心步骤 2】告诉编译器，AxCore.lib 这个大管家库文件放在哪？
target_link_directories(MyGameApp PRIVATE ${AXPLUG_SDK_DIR}/bin/Release)

# 【核心步骤 3】把连带的大管家代码缝合打包进我的 EXE 里！
target_link_libraries(MyGameApp PRIVATE AxCore)
```

### 2.4 大功告成，双击运行前的最后一步检查：文件摆放！

在你的 C++ 项目编译出 `MyGameApp.exe` 之后，直接双击肯定是会报错“找不到某个 DLL”的。
正确的文件摆放结构是：把你所有的积木垒在同一个箱子里！

```text
你的游戏根目录/
  ├── MyGameApp.exe              << 你刚写好的主程序
  ├── AxCore.dll                 << 【绝对不可少】最重要的发动机引擎，从 SDK 里的 bin 复制过来
  ├── MySuperToolPlugin.dll      << 你刚刚写好的插件
  ├── AnotherDlcPlugin.dll       << 网上下下来的别人写的 DLC 插件
  └── spdlog.dll                 << (可选) 你的插件用到的第三方库文件，也一起扔在这里
```

**只要它们全在同一个文件夹亲密接触，你的主程序一启动 `AxPlug::Init()`，所有的插件便会自动苏醒，开始干活！**

## 3. 第三方依赖说明

| 依赖 | 用途 | 说明 |
|------|------|------|
| spdlog | 日志服务 (LoggerService) | 已包含在 `deps/spdlog/` |
| Boost | 网络驱动 boost 实现 | 已包含在 `deps/boost/` |
| OpenCV | 图像测试 | 仅测试需要，SDK 不依赖 |
| Halcon | 图像测试 | 仅测试需要，SDK 不依赖 |

- **构建时**: 确保 `CMakeLists.txt` 能找到依赖库的头文件和导入库
- **运行时**: 将依赖的 DLL 复制到 exe 同级目录

---

*AxPlug v1.0 · SDK 发布指南*
