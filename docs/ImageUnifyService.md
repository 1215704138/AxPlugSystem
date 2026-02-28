# ImageUnifyService 图像统一服务 · 使用与架构手册

> 本文档为图像统一服务的**使用手册**，涵盖 API 用法、使用场景和第三方库集成。如需了解底层实现细节与维护指南，请参阅 [ImageUnifyService_DEV.md](ImageUnifyService_DEV.md)。

---

## 1. 简介

### 1.1 什么是 ImageUnifyService？

在工业视觉软件中，不同的图像处理库对图像数据的内存布局要求各不相同：

- **OpenCV**：要求像素按 `BGRBGRBGR...` 交错排列（Interleaved）
- **Halcon**：要求按 `RRR...GGG...BBB...` 平面排列（Planar）
- **Qt**：要求 `RGBRGBRGB...` 交错排列

ImageUnifyService 就是一个**图像格式统一转换中心**：你只需提交一次原始图像数据，就可以按需获取任意布局的视图，无需关心底层转换细节。

### 1.2 核心特性

| 特性 | 说明 |
|------|------|
| **极简 API** | 6 个核心方法 + 2 个 RAII 辅助类，开箱即用 |
| **零拷贝快速路径** | 请求的布局与原始布局一致时，直接返回指针，延迟 ~0.0003ms |
| **AVX2 SIMD 加速** | U8_C3 格式 Release 下完整流程超越 Halcon **93%** |
| **RAII 生命周期管理** | `ScopedFrame` / `ScopedView` 自动管理资源，零泄漏 |
| **智能内存池** | 64 字节对齐 + 6 级桶复用，消除 malloc 抖动 |
| **布局预测预取** | 统计请求模式，自动预转换，后续 GetView 零延迟 |
| **条件编译适配** | 核心无第三方依赖，OpenCV/Halcon/Qt 按需启用 |
| **格式完备** | U8/Float32 × C1/C3/C4 × Interleaved/Planar 全组合 |

---

## 2. 快速开始

### 2.1 基本用法（RAII 推荐方式）

```cpp
#include "core/IImageUnifyService.h"

auto svc = AxPlug::GetService<IImageUnifyService>();

// 提交图像帧（服务会拷贝数据，调用方可随时释放 buffer）
{
    ScopedFrame frame(svc.get(), rawData, 1920, 1080, PixelFormat::U8_C3);

    // 获取 Planar 布局视图（Halcon 格式）
    ScopedView planar(svc.get(), frame.Id(), MemoryLayout::Planar);
    auto r = planar.R();   // uint8_t* 红色通道
    auto g = planar.G();   // uint8_t* 绿色通道
    auto b = planar.B();   // uint8_t* 蓝色通道

    // 获取 Interleaved 布局视图（OpenCV 格式）
    ScopedView interleaved(svc.get(), frame.Id(), MemoryLayout::Interleaved);
    // 使用 interleaved.data / interleaved.width / interleaved.height ...

} // ScopedView 析构自动 ReleaseView，ScopedFrame 析构自动 RemoveFrame
```

### 2.2 手动管理方式

```cpp
auto svc = AxPlug::GetService<IImageUnifyService>();

uint64_t fid = svc->SubmitFrame(data, w, h, PixelFormat::U8_C3);
ImageDescriptor view = svc->GetView(fid, MemoryLayout::Planar);

// 使用 view ...

svc->ReleaseView(fid, view.data);   // 必须释放视图
svc->RemoveFrame(fid);               // 必须移除帧
```

> **强烈推荐**使用 `ScopedFrame` + `ScopedView` 的 RAII 方式，避免遗漏释放。

---

## 3. API 参考

### 3.1 核心方法

| 方法 | 说明 |
|------|------|
| `SubmitFrame(data, w, h, fmt, layout, step)` | 提交图像帧，返回唯一 `frameId`。服务内部拷贝数据 |
| `RemoveFrame(frameId)` | 移除帧及其所有缓存视图 |
| `HasFrame(frameId)` | 查询帧是否存在 |
| `GetView(frameId, targetLayout)` | 获取指定布局的视图（缓存命中则零拷贝） |
| `ReleaseView(frameId, viewPtr)` | 释放视图引用计数 |
| `SetMaxMemory(maxBytes)` | 设置内存上限（默认 256MB），超限自动 LRU 淘汰 |
| `GetMemoryUsage()` | O(1) 查询当前内存占用 |
| `ClearCache()` | 清空所有帧和视图缓存 |
| `GetLastError()` | 获取最近一次错误消息 |

### 3.2 像素格式 (PixelFormat)

| 枚举值 | 说明 |
|--------|------|
| `U8_C1` | 8位单通道灰度 |
| `U8_C3` | 8位三通道彩色（工业视觉最常用） |
| `U8_C4` | 8位四通道（含 Alpha） |
| `Float32_C1` | 32位浮点单通道 |
| `Float32_C3` | 32位浮点三通道 |
| `Float32_C4` | 32位浮点四通道 |

### 3.3 内存布局 (MemoryLayout)

| 枚举值 | 说明 | 典型用户 |
|--------|------|----------|
| `Interleaved` | `RGBRGBRGB...` 交错排列 | OpenCV, Qt |
| `Planar` | `RRR...GGG...BBB...` 平面排列 | Halcon |

### 3.4 ImageDescriptor 通道访问器

Planar 布局下可通过语义化方法直接获取通道指针：

```cpp
ScopedView planar(svc, frameId, MemoryLayout::Planar);
auto r = planar.R();             // uint8_t* 红色通道
auto g = planar.G();             // uint8_t* 绿色通道
auto b = planar.B();             // uint8_t* 蓝色通道
auto a = planar.A();             // uint8_t* Alpha 通道（仅 C4）
auto ch = planar.Channel(2);     // 通用索引访问，等价于 B()
auto fR = planar.R<float>();     // Float32 场景
```

> **注意**：通道访问器仅 Planar 布局有效，Interleaved 布局返回 `nullptr`。

### 3.5 RAII 辅助类

| 类 | 构造时调用 | 析构时调用 | 典型场景 |
|----|-----------|-----------|----------|
| `ScopedFrame` | `SubmitFrame` | `RemoveFrame` | 单次处理的帧 |
| `ScopedView` | `GetView` | `ReleaseView` | 临时获取某布局视图 |

### 3.6 生命周期

```
SubmitFrame()        GetView(Planar)      GetView(Interleaved)
    │                     │                      │
    ▼                     ▼                      ▼
┌─────────┐       ┌──────────────┐        ┌──────────────┐
│ FrameItem│──────▶│ ViewCacheItem│        │  零拷贝返回   │
│ ownedData│       │ refCount=1   │        │  原始dataPtr  │
└─────────┘       └──────────────┘        └──────────────┘
    │                     │
    │  ReleaseView()      │  refCount--
    │  内存压力 + refCount==0 → LRU淘汰视图
    │  RemoveFrame() 或 FIFO自动淘汰
    ▼
  全部释放 → 回收到内存池
```

---

## 4. 第三方库集成 (Unify.hpp)

通过 `#include "core/Unify.hpp"` 可获得零配置的第三方库转换函数。每个函数都是**无状态自由函数**，通过条件编译按需启用。

### 4.1 函数一览

| 函数 | 请求布局 | 返回类型 | 说明 |
|------|---------|---------|------|
| `Unify::ToCvMat(view)` | Interleaved | `cv::Mat` | **★推荐**：从 ScopedView 一行转换 |
| `Unify::ToCvMat(desc)` | Interleaved | `cv::Mat` | 从 ImageDescriptor 转换 |
| `Unify::ToCvMat(svc, fid)` | Interleaved | `cv::Mat` | 兼容旧接口 |
| `Unify::SubmitCvMat(svc, mat)` | — | `ScopedFrame` | cv::Mat → 提交到服务 |
| `Unify::ToHImage(view)` | Planar | `HImage` | **★推荐**：从 ScopedView 一行转换 |
| `Unify::ToHImage(desc)` | Planar | `HImage` | 从 ImageDescriptor 转换 |
| `Unify::ToHImage(svc, fid)` | Planar | `HImage` | 兼容旧接口 |
| `Unify::ToQImage(svc, fid)` | Interleaved | `QImage` | 适合 UI 绘制 |

### 4.2 启用条件编译

| 宏定义 | 库 | CMake 示例 |
|--------|-----|-----------|
| `HAS_OPENCV` | OpenCV | `target_compile_definitions(YourApp PRIVATE HAS_OPENCV)` |
| `HAS_HALCON` | Halcon | `target_compile_definitions(YourApp PRIVATE HAS_HALCON)` |
| `QT_CORE_LIB` | Qt | Qt CMake 模块自动定义 |

### 4.3 使用示例

```cpp
#include "core/Unify.hpp"

auto svc = AxPlug::GetService<IImageUnifyService>();
ScopedFrame frame(svc.get(), data, w, h, PixelFormat::U8_C3);

// ★ 一行转 Halcon HImage
ScopedView planar(svc.get(), frame.Id(), MemoryLayout::Planar);
auto himg = Unify::ToHImage(planar);

// ★ 一行转 OpenCV cv::Mat
ScopedView interleaved(svc.get(), frame.Id(), MemoryLayout::Interleaved);
cv::Mat mat = Unify::ToCvMat(interleaved);

// ★ 从 cv::Mat 提交到服务
auto frame2 = Unify::SubmitCvMat(svc.get(), mat);

// 如需长期持有数据（跨 ScopedView 生命周期），深拷贝：
cv::Mat safeCopy = Unify::ToCvMat(interleaved).clone();
```

> **注意**：`Unify::To*` 返回的对象**引用服务内部内存**，`ScopedView` 析构后指针失效。

---

## 5. 使用场景

### 5.1 工业相机采集 + 多库处理

```cpp
void OnCameraFrame(void* rawBGR, int w, int h) {
    auto svc = AxPlug::GetService<IImageUnifyService>();
    ScopedFrame frame(svc.get(), rawBGR, w, h, PixelFormat::U8_C3);

    // Halcon 检测
    ScopedView planar(svc.get(), frame.Id(), MemoryLayout::Planar);
    HImage himg = Unify::ToHImage(planar);
    // ... Halcon 处理 ...

    // OpenCV 标注
    ScopedView inter(svc.get(), frame.Id(), MemoryLayout::Interleaved);
    cv::Mat mat = Unify::ToCvMat(inter);
    // ... OpenCV 绘制 ...
}
```

### 5.2 内存控制

```cpp
auto svc = AxPlug::GetService<IImageUnifyService>();

// 限制最大内存为 512MB
svc->SetMaxMemory(512 * 1024 * 1024);

// 监控内存
size_t usage = svc->GetMemoryUsage();

// 手动清空缓存
svc->ClearCache();
```

### 5.3 错误处理

```cpp
uint64_t fid = svc->SubmitFrame(nullptr, 0, 0, PixelFormat::U8_C3);
if (fid == 0) {
    std::cerr << "提交失败: " << svc->GetLastError() << std::endl;
}

ImageDescriptor view = svc->GetView(fid, MemoryLayout::Planar);
if (view.data == nullptr) {
    std::cerr << "获取视图失败: " << svc->GetLastError() << std::endl;
}
```

---

## 6. 性能参考

### 6.1 真实图像对比 (7353×500 U8_C3, 10.5MB)

#### Release 构建

| 方法 | 单次耗时 | 吞吐量 | vs Halcon |
|------|---------|--------|----------|
| **插件完整流程 (Submit+GetView)** | **0.776 ms** | **13,549 MB/s** | **1.93x ✓** |
| Halcon GenImageInterleaved | 1.496 ms | 7,031 MB/s | 1.00x |
| 常规标量搬运 | 3.616 ms | 2,909 MB/s | — |

#### Debug 构建

| 方法 | 单次耗时 | 吞吐量 | vs Halcon |
|------|---------|--------|----------|
| **插件完整流程** | **1.29 ms** | **8,173 MB/s** | **1.22x ✓** |
| Halcon | 1.57 ms | 6,690 MB/s | 1.00x |
| 常规标量搬运 | 14.74 ms | 713 MB/s | — |

### 6.2 多格式转换性能 (1920×1080, Release)

| 格式 | I→P (ms) | P→I (ms) | 数据量 (MB) |
|------|---------|---------|----------|
| U8_C1 | 0.11 | 0.30 | 1.98 |
| U8_C3 | 0.34 | 1.05 | 5.93 |
| U8_C4 | 2.13 | 3.57 | 7.91 |
| Float32_C1 | 0.59 | 2.30 | 7.91 |
| Float32_C3 | 3.18 | 5.19 | 23.73 |
| Float32_C4 | 5.23 | 7.17 | 31.64 |

### 6.3 关键性能指标

| 指标 | 数值 |
|------|------|
| 缓存命中延迟 | ~0.0001 ms |
| 零拷贝路径 | ~0.0003 ms |
| 连续 20 帧采集（内存池复用 vs malloc） | 1.8x 加速 |

---

## 7. 常见问题

### Q1: GetView 返回空的 ImageDescriptor

1. 检查 `svc->GetLastError()` 获取错误信息
2. 调用 `svc->HasFrame(frameId)` 确认帧存在
3. 帧可能被自动淘汰 — 增大 `SetMaxMemory` 或用 `ScopedFrame` 延长生命周期
4. 确认 `SubmitFrame` 返回值不为 0

### Q2: 内存持续增长

- 内存池会保留已释放的块供复用，这是**预期行为**
- 真正的内存用量看 `GetMemoryUsage()`，而非进程 RSS
- 用 `ClearCache()` 可清空所有帧和视图缓存

### Q3: 多线程访问崩溃

- 所有接口方法都是**线程安全**的
- `GetView` 返回的 `dataPtr` 指向服务内部内存 — 线程 A 持有指针时，线程 B 不应 `RemoveFrame`
- **建议**：用 `ScopedFrame` + `ScopedView` 管理生命周期，或 `.clone()` 深拷贝数据

### Q4: 预取不生效

- 需要至少 5 次 `GetView` 调用后才开始预测
- 某布局请求比例需超过 70% 才会触发预转换
- 如果原始布局就是预测布局，预取会跳过（零拷贝已是最优）

---

## 8. 构建与集成

### 8.1 依赖

| 依赖 | 必需 | 说明 |
|------|------|------|
| C++17 | 是 | 结构化绑定、if constexpr 等 |
| AxPlug 框架 | 是 | 插件加载机制 |
| OpenCV | 否 | `Unify::ToCvMat` 需要，通过 `HAS_OPENCV` 启用 |
| Halcon | 否 | `Unify::ToHImage` 需要，通过 `HAS_HALCON` 启用 |
| Qt | 否 | `Unify::ToQImage` 需要，通过 `QT_CORE_LIB` 启用 |

### 8.2 集成到项目

```cmake
target_link_libraries(YourApp PRIVATE AxCore)

# 如需 OpenCV 适配
target_compile_definitions(YourApp PRIVATE HAS_OPENCV)
target_link_libraries(YourApp PRIVATE ${OpenCV_LIBS})

# 如需 Halcon 适配
target_compile_definitions(YourApp PRIVATE HAS_HALCON)
target_link_libraries(YourApp PRIVATE ${HALCON_LIBS})
```

---

## 9. 延伸阅读

> 内部实现细节（内存池设计、SIMD 优化、线程安全机制、扩展指南等）请参阅 [ImageUnifyService_DEV.md](ImageUnifyService_DEV.md)。
