# AxPlug 插件框架

AxPlug 是一个现代化的工业级 C++17 插件框架，支持动态加载 DLL 插件并通过类型安全的模板 API 进行调用。框架提供 Tool（多实例工具）和 Service（全局单例服务）两种插件类型，内置事件总线、性能分析器、跨 DLL 异常处理，以及高性能图像格式统一服务。

---

## 📚 文档索引

### 使用手册（面向框架使用者）

| 文档 | 说明 |
|------|------|
| **[AxPlug.md](docs/AxPlug.md)** | **核心必读** — 框架使用指南、API 参考、插件开发流程 |
| **[EventBus.md](docs/EventBus.md)** | 事件总线使用说明：订阅/发布、同步/异步、网络事件 |
| **[ImageUnifyService.md](docs/ImageUnifyService.md)** | 图像统一服务使用说明：API、第三方库集成、性能参考 |

### 开发者手册（面向框架维护者 / 新手交接）

| 文档 | 说明 |
|------|------|
| **[AxPlug_DEV.md](docs/AxPlug_Dev.md)** | 框架底层架构、源码结构、线程安全、构建系统详解 |
| **[EventBus_DEV.md](docs/EventBus_DEV.md)** | 事件总线内部实现：COW 机制、MPSC 队列、锁策略 |
| **[ImageUnifyService_DEV.md](docs/ImageUnifyService_DEV.md)** | 图像服务内部实现：内存池、SIMD 优化、布局预测 |

### 其他

| 文档 | 说明 |
|------|------|
| **[Release_Guide.md](docs/Release_Guide.md)** | SDK 发布流程、目录结构、外部项目集成指南 |
| **[build_options.md](docs/build_options.md)** | CMake 构建选项说明（测试开关、编译配置等） |

---

## 🚀 快速构建

本项目使用 CMake 构建，提供自动化脚本简化流程（Windows + MSVC）。

### 1. 构建框架

```bash
# Debug 构建（不含测试）
scripts\build_debug_no_test.bat

# Release 构建（不含测试）
scripts\build_release_no_test.bat
```

### 2. 构建并运行测试

```bash
# Debug 构建 + 测试
scripts\build_debug_with_test.bat

# Release 构建 + 测试
scripts\build_release_with_test.bat
```

### 3. 手动 CMake 构建

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
cmake --install build --config Release --prefix publish
```

---

## 📂 目录结构

```
AxPlugSystem/
├── include/              # 公共接口头文件
│   ├── AxPlug/           #   框架核心接口 (AxPlug.h, IAxObject.h, AxEventBus.h ...)
│   └── core/             #   内置服务接口 (IImageUnifyService.h, LoggerService.h ...)
├── src/                  # 源代码
│   ├── AxCore/           #   框架核心实现 (AxPluginManager, DefaultEventBus ...)
│   └── core/             #   内置插件实现 (LoggerService, ImageUnifyService, NetworkEventBus)
├── test/                 # 测试程序
├── scripts/              # 自动化构建脚本
├── deps/                 # 第三方依赖 (OpenCV 等)
├── docs/                 # 详细文档
├── build/                # (自动生成) 中间构建目录
└── publish/              # (自动生成) SDK 发布目录
```

---

## 🛠️ 环境要求

| 要求 | 最低版本 |
|------|---------|
| 操作系统 | Windows 10 / 11 |
| 编译器 | Visual Studio 2019+（推荐 2022） |
| CMake | 3.15+ |
| C++ 标准 | C++17 |
| 运行时 | `/MD`（Release）或 `/MDd`（Debug） |

---

## ⚡ 快速体验

```cpp
#include <AxPlug/AxPlug.h>
#include "core/IImageUnifyService.h"

int main() {
    AxPlug::Init("plugins");

    // 获取图像统一服务
    auto imgSvc = AxPlug::GetService<IImageUnifyService>();

    // 使用事件总线
    auto conn = AxPlug::Subscribe("my_event", [](const AxPlug::AxEvent& e) {
        // 处理事件 ...
    });
    AxPlug::Publish("my_event", AxPlug::AxEvent{});

    return 0;
}
```

---

## 📄 License

详见 [LICENSE](LICENSE) 文件。
