# CMake 构建选项说明 (新手配置避坑指南)

AxPlug 提供了灵活的 CMake 构建选项。如果你是第一次接触 C++ 的 CMake 工具，可以把 CMake 想象成一个“包工头”，而下面的选项就是你给包工头下达的具体施工要求。

## 构建选项一览（你必须了解的三大开关）

| 选项名称 | 默认值 | 这是干嘛的？（小白解释） |
|------|--------|------|
| `AXPLUG_BUILD_TESTS` | `ON` (开启) | **要不要编译附带的测试程序？** 默认开启。如果你只是想原封不动地用框架，不需要看它是怎么被测试的，可以改成 `OFF` 以成倍加快编译速度。 |
| `CMAKE_BUILD_TYPE` | `Debug` (调试版) | **你想要怎么样的房屋质量？** <br> - `Debug`：毛坯房，慢但能在出错时告诉你哪行砖砌歪了（方便找 Bug）。<br> - `Release`：精装写字楼，极速狂飙，但出错时很难查原因（最终发给客户的版本）。 |
| `CMAKE_INSTALL_PREFIX` | `${PROJECT_ROOT}/publish` | **建好的房子放到哪里去？** 默认是在项目最外层的 `publish` 文件夹。你提取出的 SDK 就在这。 |

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

## 编译器与运行时要求（极其容易白给的地方）

新手在编译或使用别人给的 `.dll` 时，最容易在这一步遭遇“闪退”或“找不到函数”的制裁。
请在你自己工程的 Visual Studio 设置里（或者你自己的 CMake 里）核对以下要求：

- **编译器最低要求**: 必须是 MSVC 2019 或以上（支持 C++17）。
- **运行库设定 (Runtime Library)**:
  - 规定动作：所有插件、主程序、所有你用到的第三方库文件，必须在同一频道！
  - **Release 版本** 请务必设置为 `多线程 DLL (/MD)` (`MultiThreadedDLL`)
  - **Debug 版本** 请务必设置为 `多线程调试 DLL (/MDd)` (`MultiThreadedDebugDLL`)
  - *绝对禁止* 将某一个文件偷偷编成 `/MT`，否则你的程序必死无疑！
- **编码格式**: 为了防止中文乱码，统一使用 UTF-8 编码 (`/utf-8` 编译参数)。

## 插件独有的魔法身份贴纸（编译定义）

插件 DLL 需添加 `AX_PLUGIN_EXPORTS` 编译定义：

```cmake
target_compile_definitions(MyPlugin PRIVATE AX_PLUGIN_EXPORTS)
```

也可使用根 CMakeLists.txt 中的 `setup_plugin_target()` 函数：

```cmake
setup_plugin_target(MyPlugin)  # 等同于上面的定义
```
