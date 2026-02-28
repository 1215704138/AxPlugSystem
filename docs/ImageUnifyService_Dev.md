# ImageUnifyService 图像统一服务 · 0基础开发交接指南

> 本文档面向刚接手本项目的开发者，帮助你快速理解图像统一服务的技术栈、底层实现原理、以及如何在此基础上进行维护和扩展。

---

## 1. 技术栈总览

| 技术 | 用途 | 在本系统中的位置 |
|------|------|------------------|
| **内存池 (Memory Pool)** | 64字节缓存行对齐分配，6级桶复用，消除 `malloc` 抖动 | `AlignedMemoryPool` 类 |
| **SSSE3 SIMD** | 16像素/迭代的三通道交错↔平面布局转换 (`pshufb` 指令) | `I2P_U8C3_SSSE3_Row` / `P2I_U8C3_SSSE3_Row` |
| **AVX2 SIMD** | 32像素/迭代的三通道去交错（2倍SSSE3吞吐量） | `I2P_U8C3_AVX2_Row` |
| **Non-Temporal Store (NT)** | 大图写入跳过CPU缓存，减少~33%总线带宽占用 | `I2P_U8C3_AVX2_NT_Row` / `_mm256_stream_si256` |
| **引用计数 + LRU 淘汰** | 视图缓存的生命周期管理和内存压力回收 | `ViewCacheItem::refCount` + `lastAccess` |
| **持久线程池** | 消除 `std::thread` 创建/销毁开销，多线程并行布局转换 | `StaticThreadPool` |
| **Cache Tiling 分块** | L1缓存友好的行分块处理 (64行/块) | `TILE_ROWS` 常量 |
| **软件预取** | `_mm_prefetch` 提前将下一行数据加载到L1缓存 | `PREFETCH_READ` 宏 |
| **布局预测预取** | 统计请求布局比例，>70%时提前转换 | `predictLayout()` / `prefetchView()` |
| **`std::atomic` 内存计数** | O(1) 查询内存使用量替代 O(N×M) 遍历 | `memoryUsage_` |
| **RAII 辅助类** | `ScopedFrame` / `ScopedView` 自动管理帧/视图生命周期 | `IImageUnifyService.h` |
| **条件编译适配层** | `HAS_OPENCV` / `HAS_HALCON` / `QT_CORE_LIB` 按需编译便捷函数 | `Unify.hpp` |

---

## 2. 学习路线（推荐阅读顺序）

### 2.1 必备前置知识

1. **图像基础** — 像素格式、通道数、行步长 (stride/step)、交错 vs 平面布局
   - 推荐：OpenCV 官方教程 "Mat - The Basic Image Container"
   - 重点理解：
     - **Interleaved (交错)**：`[B0,G0,R0, B1,G1,R1, ...]` — OpenCV/Qt 使用
     - **Planar (平面)**：`[B0,B1,B2,..., G0,G1,G2,..., R0,R1,R2,...]` — Halcon 使用
     - **step/stride**：每行实际占用的字节数（可能因对齐而大于 `width * bytesPerPixel`）

2. **C++ 内存管理** — `_aligned_malloc` / `posix_memalign`、缓存行对齐
   - 推荐：《What Every Programmer Should Know About Memory》(Ulrich Drepper)
   - 重点：为什么 64 字节对齐能提升性能（CPU 缓存行大小）

3. **SIMD 入门** — SSE/SSSE3/AVX2 intrinsic 函数
   - 推荐：Intel Intrinsics Guide (https://www.intel.com/content/www/us/en/docs/intrinsics-guide/)
   - 重点：`_mm_shuffle_epi8` (pshufb)、`_mm256_loadu2_m128i`、`_mm256_stream_si256`
   - 不需要精通汇编，只需理解 "用一条指令同时处理16/32个字节" 的概念

4. **引用计数与 LRU 缓存**
   - 搜索 "LRU Cache implementation C++" 了解基本原理
   - 重点：引用计数 > 0 的项不可被淘汰

5. **线程池模式**
   - 推荐：搜索 "C++ thread pool pattern condition_variable"
   - 重点：生产者-消费者模型、`condition_variable` 的 wait/notify

### 2.2 代码阅读顺序

```
第一轮：接口层（用户视角）
  ① include/core/IImageUnifyService.h   ← 枚举/结构体/接口/RAII类/工具函数
  ② include/core/Unify.hpp              ← OpenCV/Halcon/Qt 便捷转换函数

第二轮：实现层（引擎内部）
  ③ src/.../include/ImageUnifyService.h  ← 内部类声明：内存池、视图缓存项、帧数据项、布局转换器
  ④ src/.../src/ImageUnifyService.cpp    ← 核心实现（按以下顺序阅读）:
     a. AlignedMemoryPool 实现 (内存池)
     b. StaticThreadPool 实现 (线程池)
     c. SSSE3/AVX2 SIMD 转换函数
     d. LayoutTransformer 通用模板
     e. ImageUnifyService 主服务方法

第三轮：插件注册
  ⑤ src/.../src/module.cpp              ← AX_AUTO_REGISTER_SERVICE + AX_DEFINE_PLUGIN_ENTRY
```

---

## 3. 核心机制深度解析

### 3.1 数据流概览

```
用户图像数据 (any format)
      │
      ▼
  SubmitFrame()  ──────► 拷贝数据到内存池分配的缓冲区
      │                  记录原始布局 (Interleaved/Planar)
      │                  返回 frameId
      │
      ▼
  GetView(frameId, targetLayout)
      │
      ├─ 目标布局 == 原始布局？ ──► 零拷贝：直接返回原始数据指针
      │
      └─ 目标布局 != 原始布局？ ──► 查找缓存：已有相同布局视图？
            │                           │
            ├─ 缓存命中 ──────────────► refCount++, 更新 lastAccess, 返回
            │
            └─ 缓存未命中 ──► 分配新缓冲 → SIMD布局转换 → 存入缓存 → 返回
                              │
                              └─ 超内存限制？ → performMaintenance()
                                    │
                                    ├─ 第1级: evictZeroRefViews() — LRU淘汰零引用视图
                                    └─ 第2级: FIFO帧淘汰 — 移除最老帧

  ReleaseView(frameId, viewPtr)  ──► refCount--, 计数归零后缓存可被回收
  RemoveFrame(frameId)           ──► 强制释放帧及所有视图
```

### 3.2 AlignedMemoryPool — 对齐内存池

**为什么需要内存池？**
工业视觉场景下，每秒提交 30-60 帧 1920×1080 图像（约 6MB/帧），频繁 `malloc/free` 会导致：
- 分配延迟抖动（操作系统内核锁竞争）
- 内存碎片化
- 缓存不对齐（性能下降 10-30%）

**设计**：

```
分配请求 size
    │
    ▼
BucketIndex(size) → 找到对应桶
    │
    ├─ 桶 0: ≤256KB    (工业相机小ROI)
    ├─ 桶 1: ≤1MB      (640×480 灰度图)
    ├─ 桶 2: ≤4MB      (1080p 灰度图)
    ├─ 桶 3: ≤16MB     (1080p 三通道)
    ├─ 桶 4: ≤64MB     (4K 三通道)
    └─ 桶 5: >64MB     (直接分配，不缓存)

Allocate(size):
    1. 在对应桶中查找 >= size 的空闲块
    2. 找到 → 复用（从桶中移除，返回指针）
    3. 未找到 → _aligned_malloc(64, size)  新分配

Deallocate(ptr, size):
    1. 桶 < 5 且桶内空闲块 < 4 → 放回桶中复用
    2. 否则 → _aligned_free(ptr) 直接释放
```

**防膨胀机制**：每桶最多保留 4 块空闲内存。

### 3.3 SIMD 布局转换 — 分层架构

```
                    ┌─ AVX2 + NT Store (≥32像素对齐, 大图)
                    │
U8_C3 专用路径 ─────┼─ AVX2 常规 (≥32像素)
(工业最常用)        │
                    └─ SSSE3 (≥16像素) + 标量尾部
                    
通用路径 ──────────── 模板函数 (任意格式: U8_C1/C4, Float32)
(分块 + 软件预取)       TILE_ROWS=64行/块
                        4像素循环展开
```

**SSSE3 核心原理** (以 Interleaved→Planar 为例)：
- 输入：48字节 = 16像素 × 3通道 = `[B0,G0,R0, B1,G1,R1, ...]`
- 使用 `pshufb` 指令按预定义掩码抽取：
  - 掩码1：从每个三元组中取第1个字节 → 得到 B 通道
  - 掩码2：取第2个字节 → G 通道
  - 掩码3：取第3个字节 → R 通道
- 一次处理 16 像素（SSSE3）或 32 像素（AVX2）

**AVX2 NT Store 原理**：
- 普通 `_mm256_storeu_si256`：数据写入CPU缓存再刷回内存（写分配）
- `_mm256_stream_si256`：数据绕过缓存直接写入内存（适合大块只写数据）
- 效果：减少缓存污染，带宽占用降低约 33%

### 3.4 多线程并行转换

```
StaticThreadPool::parallelFor(height, processRows)
    │
    ├─ 调用线程处理: rows [0, chunk)
    ├─ Worker 0 处理: rows [chunk, 2*chunk)
    ├─ Worker 1 处理: rows [2*chunk, 3*chunk)
    └─ ...
    
触发条件: 像素总数 >= MT_PIXEL_THRESHOLD (200,000)
          即约 450×450 以上的图像才启用多线程
```

**线程池特点**：
- 持久线程，进程启动时创建，避免每次创建/销毁开销
- 线程数 = `hardware_concurrency - 1`（留一个核给调用线程）
- 使用 `generation_` 计数器替代逐个唤醒，减少 `notify` 次数
- `submitMutex_` 防止并发 `parallelFor` 覆盖工作项

### 3.5 布局预测预取

```
统计计数:
  planarHits_++      (每次请求 Planar 布局)
  interleavedHits_++ (每次请求 Interleaved 布局)

predictLayout():
  total = planarHits_ + interleavedHits_
  if planarHits_ > total * 70%  → 预测下次请求 Planar
  if interleavedHits_ > total * 70% → 预测下次请求 Interleaved

SubmitFrame() 末尾:
  如果能预测 → prefetchView() → 提前转换并缓存
  效果: 后续 GetView() 直接缓存命中，延迟降为 0
```

---

## 4. 文件清单与职责

| 文件 | 行数 | 职责 |
|------|------|------|
| `include/core/IImageUnifyService.h` | ~286 | 公开接口 + `PixelFormat`/`MemoryLayout` 枚举 + `ImageDescriptor` + RAII类 + 工具函数 |
| `include/core/Unify.hpp` | ~154 | OpenCV/Halcon/Qt 便捷转换函数（条件编译） |
| `include/core/UnifyToCv.hpp` | ~7 | 向后兼容重定向 → Unify.hpp |
| `include/core/UnifyToHalcon.hpp` | ~7 | 向后兼容重定向 → Unify.hpp |
| `include/core/UnifyToQt.hpp` | ~7 | 向后兼容重定向 → Unify.hpp |
| `src/.../include/ImageUnifyService.h` | ~157 | 内部类：`AlignedMemoryPool`、`ViewCacheItem`、`FrameItem`、`LayoutTransformer`、主服务类 |
| `src/.../src/ImageUnifyService.cpp` | ~1124 | 全部实现：内存池、线程池、SIMD转换、主服务逻辑 |
| `src/.../src/module.cpp` | 6 | 插件注册入口 |
| `src/.../CMakeLists.txt` | 33 | 构建配置 |

---

## 5. 在当前系统中如何使用

### 5.1 获取服务实例

```cpp
#include "AxPlug/AxPlug.h"
#include "core/IImageUnifyService.h"

// 获取全局单例（Service 模式）
auto svc = AxPlug::GetService<IImageUnifyService>();
```

### 5.2 基础用法 — 手动管理

```cpp
// 1. 提交帧（服务内部会拷贝数据，调用方可自由释放 buffer）
uint64_t fid = svc->SubmitFrame(rawData, 1920, 1080, PixelFormat::U8_C3);

// 2. 获取目标布局的视图
ImageDescriptor planar = svc->GetView(fid, MemoryLayout::Planar);
// planar.dataPtr 现在指向 Planar 格式的数据

// 3. 使用完毕释放视图（引用计数-1）
svc->ReleaseView(fid, planar.dataPtr);

// 4. 不再需要此帧时移除
svc->RemoveFrame(fid);
```

### 5.3 推荐用法 — RAII 自动管理

```cpp
{
    ScopedFrame frame(svc.get(), rawData, 1920, 1080, PixelFormat::U8_C3);
    if (!frame.Ok()) { /* 错误处理 */ }

    ScopedView planar(svc.get(), frame.Id(), MemoryLayout::Planar);
    if (!planar.Ok()) { /* 错误处理 */ }

    // 使用 planar.Data() / planar.Width() / planar.Height() ...
    // Planar 布局专用通道访问:
    uint8_t* r = planar.R();
    uint8_t* g = planar.G();
    uint8_t* b = planar.B();

} // 离开作用域自动释放视图和帧
```

### 5.4 与第三方库集成

```cpp
#include "core/Unify.hpp"

// OpenCV (需定义 HAS_OPENCV 并链接 OpenCV)
ScopedView interleaved(svc.get(), fid, MemoryLayout::Interleaved);
cv::Mat mat = Unify::ToCvMat(interleaved);  // 零拷贝包装

// Halcon (需定义 HAS_HALCON 并链接 Halcon)
ScopedView planar(svc.get(), fid, MemoryLayout::Planar);
HalconCpp::HImage himg = Unify::ToHImage(planar);

// Qt (自动检测 QT_CORE_LIB)
QImage qimg = Unify::ToQImage(svc.get(), fid);
```

---

## 6. 扩展指南

### 6.1 添加新的像素格式

1. 在 `IImageUnifyService.h` 的 `PixelFormat` 枚举中添加新值
2. 更新 `ImageDescriptor` 中的 `getChannels()`、`getBytesPerPixel()` 方法
3. 更新 `ImageFormatUtils::GetPixelFormatString()`
4. 在 `ImageUnifyService.cpp` 的 `LayoutTransformer` 中添加对应的转换逻辑
5. 如果是常用格式，考虑添加 SIMD 特化路径

### 6.2 添加新的第三方库适配

在 `Unify.hpp` 中添加新的条件编译段：
```cpp
#ifdef HAS_MY_LIBRARY
#include <MyLibrary.h>

inline MyImage ToMyImage(IImageUnifyService* svc, uint64_t frameId) {
    ImageDescriptor desc = svc->GetView(frameId, MemoryLayout::Interleaved);
    if (!desc.dataPtr) return {};
    // 构造 MyImage ...
}

#endif
```

### 6.3 调整内存限制

```cpp
auto svc = AxPlug::GetService<IImageUnifyService>();
svc->SetMaxMemory(512 * 1024 * 1024);  // 设置为 512MB (默认 256MB)

// 查询当前内存使用
size_t used = svc->GetMemoryUsage();

// 手动清除所有缓存
svc->ClearCache();
```

### 6.4 优化 SIMD 路径

如果需要为新格式添加 SIMD 优化：
1. 参考 `I2P_U8C3_SSSE3_Row` 的模式编写单行处理函数
2. 使用 Intel Intrinsics Guide 查找合适的指令
3. 添加 AVX2 版本（同一掩码用 `_mm256_broadcastsi128_si256` 广播到256位）
4. 考虑添加 NT Store 版本（仅适用于大图且目标地址32字节对齐）
5. 在 `LayoutTransformer::InterleavedToPlanar` / `PlanarToInterleaved` 中添加分发逻辑

---

## 7. 维护注意事项

### 7.1 常见陷阱

| 陷阱 | 说明 | 解决方式 |
|------|------|----------|
| `GetView` 后忘记 `ReleaseView` | 引用计数永远 > 0，视图无法被 LRU 淘汰 | 使用 `ScopedView` RAII 自动管理 |
| 视图指针在 `RemoveFrame` 后仍在使用 | 帧删除后数据被回收，指针悬空 | 先释放所有视图，再移除帧 |
| `SubmitFrame` 后立即释放原始 buffer | 安全！服务内部会拷贝数据 | 无需担心 |
| 高频小图不需要多线程 | 线程池同步开销可能大于转换耗时 | 像素 < 200,000 时自动降级为单线程 |

### 7.2 性能调优参数

| 参数 | 位置 | 默认值 | 含义 |
|------|------|--------|------|
| `AlignedMemoryPool::ALIGN` | `ImageUnifyService.h` | 64 | 内存对齐字节数（CPU缓存行大小） |
| `BUCKET_COUNT` | `ImageUnifyService.h` | 6 | 内存池桶数量 |
| 每桶最大空闲块 | `ImageUnifyService.cpp` | 4 | 防止内存池膨胀 |
| `TILE_ROWS` | `ImageUnifyService.cpp` | 64 | 分块处理行数（L1缓存友好） |
| `MT_PIXEL_THRESHOLD` | `ImageUnifyService.cpp` | 200,000 | 多线程触发像素阈值 |
| `maxMemory_` | `ImageUnifyService.h` | 256MB | 服务总内存限制 |

### 7.3 调试技巧

- **内存泄漏检测**：检查 `GetMemoryUsage()` 是否持续增长 → 可能是 `ReleaseView` 遗漏
- **性能分析**：`GetView` 内置 Profiler 计时，启用后可在 chrome://tracing 查看转换耗时
- **错误信息**：调用 `svc->GetLastError()` 获取最近一次操作的错误描述
- **内存池状态**：`AlignedMemoryPool::GetPooledCount()` 查看池中空闲块数量

### 7.4 锁策略

| 锁 | 类型 | 保护的数据 | 注意事项 |
|----|------|-----------|----------|
| `framesMutex_` | `mutex` | `frames_` 哈希表 + `frameOrder_` 队列 | 帧级操作持有 |
| `FrameItem::itemMutex` | `mutex` | 单帧内的视图列表 | 细粒度锁，减少跨帧竞争 |
| `errorMutex_` | `mutex` | `lastError_` 字符串 | 与 `framesMutex_` 不嵌套 |
| `poolMutex_` | `mutex` | 内存池桶数组 | 仅在分配/回收时短暂持有 |

**无死锁保证**：所有锁都不嵌套。`framesMutex_` 和 `itemMutex` 之间通过"先释放帧锁，再加项锁"的模式避免嵌套。
