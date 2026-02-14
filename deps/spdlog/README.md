# AxPlug 依赖库说明

## spdlog - Header-only 模式

本项目使用 **header-only** 模式的 spdlog 日志库。

### 特性
- ✅ **无需链接库文件** - 只需包含头文件即可使用
- ✅ **编译更快** - 避免了库文件的编译和链接过程
- ✅ **部署简单** - 减少了依赖文件数量

### 使用方式
```cpp
#include <spdlog/spdlog.h>
#include <spdlog/logger.h>

// 直接使用，无需链接
spdlog::info("Hello, world!");
```

### 项目配置
- **头文件位置**: `deps/spdlog/include/`
- **CMake 配置**: 已配置为 INTERFACE 库，自动添加 `SPDLOG_HEADER_ONLY` 宏定义
- **构建脚本**: `scripts/build_deps.bat` 已更新为 header-only 模式

### 注意事项
1. 由于是 header-only 模式，编译时间可能略有增加
2. 所有 spdlog 功能均可正常使用
3. 如需切换回编译库模式，请修改构建脚本和 CMake 配置

---
