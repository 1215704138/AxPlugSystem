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

## 2. 开发者集成指南

### 2.1 环境要求

- C++17 编译器（MSVC 2019+ / VS2022 推荐）
- CMake 3.15+
- 所有模块统一使用相同 MSVC 运行时（`/MD` 或 `/MDd`）

### 2.2 开发插件 DLL

插件 DLL **仅需链接 AxInterface（header-only）**，不需要链接 AxCore：

```cmake
cmake_minimum_required(VERSION 3.15)
project(MyPlugin)

set(AXPLUG_SDK_DIR "path/to/publish")

add_library(MyPlugin SHARED src/MyPlugin.cpp src/module.cpp)
target_include_directories(MyPlugin PRIVATE ${AXPLUG_SDK_DIR}/include)
target_compile_definitions(MyPlugin PRIVATE AX_PLUGIN_EXPORTS)
```

`module.cpp` 示例：

```cpp
#include "MyPlugin.h"
#include "AxPlug/AxPluginExport.h"

AX_BEGIN_PLUGIN_MAP()
    AX_PLUGIN_TOOL(MyPlugin, IMyInterface)
AX_END_PLUGIN_MAP()
```

### 2.3 开发宿主程序 (exe)

宿主程序需要链接 AxCore：

```cmake
cmake_minimum_required(VERSION 3.15)
project(MyApp)

set(AXPLUG_SDK_DIR "path/to/publish")

add_executable(MyApp src/main.cpp)
target_include_directories(MyApp PRIVATE ${AXPLUG_SDK_DIR}/include)
target_link_directories(MyApp PRIVATE ${AXPLUG_SDK_DIR}/bin/Release)
target_link_libraries(MyApp PRIVATE AxCore)
```

### 2.4 运行时部署

程序运行目录下必须包含：

1. **AxCore.dll**（必须）
2. **插件 DLL**（AxCore 会自动扫描 exe 同目录下的所有 DLL）
3. **第三方依赖 DLL**（按需）：如 `spdlog.dll`、`opencv_core*.dll` 等

---

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
