# AxPlug 插件框架

AxPlug 是一个基于 C++17 的高性能、轻量级插件框架，支持动态加载 DLL 插件并通过类型安全的模板 API 进行调用。

## 📚 文档索引

- **[快速入门 & 使用手册](docs/AxPlug.md)**：适合使用者，包含框架简介、核心概念、快速上手指南。
- **[开发与维护手册](docs/AxPlug_Dev.md)**：适合框架开发者，包含内部架构、源码结构、构建系统详解。
- **[ImageUnifyService 开发手册](docs/ImageUnifyService_Dev.md)**：核心图像服务插件的详细设计与优化说明。
- **[发布与集成指南](docs/Release_Guide.md)**：包含 SDK 生成、目录结构及开发者集成说明。


## 🚀 快速构建

本项目使用 CMake 构建，并提供了自动化脚本简化流程 (Windows + MSVC)。

### 1. 生成 SDK (发布包)

运行以下脚本，将在 `publish/` 目录下生成头文件、库文件和 DLL：

- **Debug 版本**: `scripts\build_publish_debug.bat`
- **Release 版本**: `scripts\build_publish_release.bat`

### 2. 运行测试

在生成 SDK 后，可以独立构建并运行测试程序：

- **构建测试**: `scripts\build_test.bat`
- **运行 Demo**: 
  - `test\build\bin\plugin_system_test.exe`
  - `test\build\bin\logger_test.exe`

## 📂 目录结构

```
AxPlug/
├── include/           # 公共接口头文件
├── src/               # 源代码 (AxCore, 插件实现)
├── test/              # 测试程序 (依赖 publish/)
├── scripts/           # 自动化构建脚本
├── docs/              # 详细文档
├── publish/           # (自动生成) SDK 发布目录
└── build/             # (自动生成) 中间构建目录
```

## 🛠️ 环境要求

- Windows 10/11
- Visual Studio 2022 (MSVC)
- CMake 3.15+
