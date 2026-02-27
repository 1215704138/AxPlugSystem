# AxPlug SDK 发布指南

本文档用于指导如何发布 AxPlug SDK，以及外部开发者如何使用 SDK 进行二次开发。

## 1. SDK 发布流程

### 1.1 生成 SDK 包

使用项目提供的自动化脚本生成 SDK：

- **Debug 版本**:
    - `scripts\build_debug_with_test.bat` (包含测试)
    - `scripts\build_debug_no_test.bat` (纯净版 SDK)
- **Release 版本**:
    - `scripts\build_release_with_test.bat` (包含测试)
    - `scripts\build_release_no_test.bat` (纯净版 SDK)


生成的 `publish` 目录即为 **SDK 包**。

### 1.2 SDK 目录结构

```text
AxPlug_SDK/ (即 publish 目录)
├── bin/                  # 运行时二进制文件 (DLLs)
│   ├── AxCore.dll        # [核心] 插件管理器
│   ├── plugins/          # (如果有子目录)
│   └── (插件DLLs)        # MathPlugin.dll, LoggerPlugin.dll 等
├── lib/                  # 链接库 (开发依赖)
│   └── AxCore.lib        # 用于链接核心库
└── include/              # 头文件 (开发依赖)
    ├── AxPlug/           # [核心] 基础接口 (AxPlug.h, IAxObject.h)
    ├── core/             # 核心服务接口 (LoggerService.h, IImageUnifyService.h)
    ├── driver/           # 驱动接口
    └── business/         # 业务接口
```

---

## 2. 开发者集成指南

外部开发者如果要开发新的插件，或在自己的程序中宿主 AxPlug，请遵循以下步骤。

- C++17 编译器 (强烈推荐 MSVC 19.3x / VS2022)
- CMake 3.15+

> [!WARNING]
> **测试环境兼容性提示**
> 本项目当前配置仅适配以下特定环境：
> - **Visual Studio 2022 (MSVC)**
> - **Halcon 20.11**
> 
> 如果您使用其他版本的编译器或 Halcon：
> 1.  需自行重新配置 `CMakeLists.txt` 中的 Halcon 路径。
> 2.  **OpenCV** 依赖可能需要重新编译以匹配您的编译器版本。

### 2.2 CMake 集成示例

在您的 `CMakeLists.txt` 中：

```cmake
cmake_minimum_required(VERSION 3.15)
project(MyNewPlugin)

# 1. 设置 SDK 路径 (指向 publish 目录)
set(AXPLUG_SDK_DIR "path/to/AxPlug_SDK")

# 2. 包含头文件
include_directories(${AXPLUG_SDK_DIR}/include)

# 3. 创建插件 DLL
add_library(MyNewPlugin SHARED src/MyPlugin.cpp)

# 4. 链接库目录
target_link_directories(MyNewPlugin PRIVATE ${AXPLUG_SDK_DIR}/lib)

# 5. 链接核心库 
# 注意：无论是开发插件 DLL 还是宿主程序(exe)都必须链接 AxCore 
# （为了保证异常状态 AxError 和 性能采样 AxProfiler 的 TLS 状态跨 DLL 一致性）。
target_link_libraries(MyNewPlugin PRIVATE AxCore)

# 6. 定义导出宏
target_compile_definitions(MyNewPlugin PRIVATE AX_PLUGIN_EXPORTS)
```

### 2.3 运行时部署

您的程序运行目录下必须包含以下文件：

1.  **AxCore.dll** (必须)
2.  **第三方依赖 DLL** (如果使用了相关功能):
    - `opencv_core*.dll`
    - `halcon.dll`
    - `Qt6Core.dll`
3.  **插件 DLL** (可选，AxCore 会自动扫描加载)

---

## 3. 第三方依赖说明

如果您的插件依赖了 OpenCV 或 Halcon，请确保在构建和运行时都能找到对应的库。

- **构建时**: 确保 `CMakeLists.txt` 能找到依赖库的头文件和导入库。
- **运行时**: 将依赖的 DLL 复制到 exe 同级目录。
