# CMake 构建选项说明 v1.0

AxPlug 提供了灵活的 CMake 构建选项，允许开发者根据需求定制构建过程。

## 构建选项一览

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `AXPLUG_BUILD_TESTS` | `ON` | 是否编译 `test` 目录下的测试程序 |
| `CMAKE_BUILD_TYPE` | `Debug` | 构建类型（Debug / Release） |
| `CMAKE_INSTALL_PREFIX` | `${PROJECT_ROOT}/publish` | 安装/发布目录 |

## 可选测试编译 (`AXPLUG_BUILD_TESTS`)

控制是否编译 `test` 目录下的测试程序。

### 1. 编译包含测试（默认）

```bash
cmake -S . -B build
# 或显式指定
cmake -S . -B build -DAXPLUG_BUILD_TESTS=ON
```

### 2. 编译不包含测试

如果希望加快构建速度，或者在没有测试依赖（如 OpenCV）的环境中构建核心库：

```bash
cmake -S . -B build -DAXPLUG_BUILD_TESTS=OFF
```

设置后 CMake 将跳过 `test` 目录，输出中不含测试可执行文件。

### 3. 验证

构建完成后检查 `build/bin` 目录：
- **ON**: 包含测试 `.exe` 文件
- **OFF**: 仅包含核心 `.dll` 和 `.lib` 文件

## 构建脚本

`scripts/` 目录下提供了一键构建脚本：

| 脚本 | 说明 |
|------|------|
| `build_debug_no_test.bat` | Debug 构建（不含测试），输出到 `publish/` |
| `build_debug_with_test.bat` | Debug 构建（含测试），输出到 `publish/` |
| `build_release_no_test.bat` | Release 构建（不含测试），输出到 `publish/` |
| `build_release_with_test.bat` | Release 构建（含测试），输出到 `publish/` |

每个脚本执行三步：Configure → Build → Install（发布到 `publish/`）。

## 编译器与运行时要求

- **编译器**: MSVC 2019+（C++17）
- **运行时**: 所有模块统一使用 `MultiThreadedDLL` (`/MD`) 或 `MultiThreadedDebugDLL` (`/MDd`)
- **编码**: UTF-8 (`/utf-8`)

## 插件编译定义

插件 DLL 需添加 `AX_PLUGIN_EXPORTS` 编译定义：

```cmake
target_compile_definitions(MyPlugin PRIVATE AX_PLUGIN_EXPORTS)
```

也可使用根 CMakeLists.txt 中的 `setup_plugin_target()` 函数：

```cmake
setup_plugin_target(MyPlugin)  # 等同于上面的定义
```
