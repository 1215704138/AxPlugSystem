# CMake 构建选项说明

AxPlug 提供了灵活的 CMake 构建选项，允许开发者根据需求定制构建过程。

## 可选测试编译 (`AXPLUG_BUILD_TESTS`)

在根目录 `CMakeLists.txt` 中，我们提供了一个选项 `AXPLUG_BUILD_TESTS`，用于控制是否编译 `test` 目录下的测试程序。

### 1. 编译包含测试（默认）
默认情况下，该选项为开启状态 (`ON`)。您可以直接运行构建命令：
```bash
cmake -B build
```
或者显式指定：
```bash
cmake -B build -DAXPLUG_BUILD_TESTS=ON
```

### 2. 编译不包含测试
如果您希望加快构建速度，或者在没有测试依赖（如 OpenCV/Qt）的环境中构建核心库，可以禁用测试：
```bash
cmake -B build -DAXPLUG_BUILD_TESTS=OFF
```
设置此选项后，CMake 将完全跳过 `test` 目录的配置。构建输出 `build/bin` 中将不会包含 `plugin_system_test.exe`、`image_unify_test.exe` 等测试可执行文件。

### 3. 验证
构建完成后，检查 `build/bin` 目录。
- **ON**: 目录中包含测试 `.exe` 文件。
- **OFF**: 目录中仅包含核心 `.dll` 和 `.lib` 文件。
