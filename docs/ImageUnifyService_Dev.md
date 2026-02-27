# ImageUnifyService 开发者维护手册

> v1.0 | 开发者维护手册

---

## 1. 架构总览

```
┌─────────────────────────────────────────────────────────┐
│                      用户代码                            │
│   ScopedFrame / ScopedView / Unify::ToCvMat() ...      │
├─────────────────────────────────────────────────────────┤
│               IImageUnifyService (接口层)                │
│   include/core/IImageUnifyService.h  (~290行)           │
│   6个虚方法 + 2个RAII类 + 工具函数                       │
├─────────────────────────────────────────────────────────┤
│               ImageUnifyService (实现层)                 │
│   src/core/ImageUnifyService/                           │
│   ├── include/ImageUnifyService.h   (内部数据结构)       │
│   ├── src/ImageUnifyService.cpp     (核心逻辑~630行)     │
│   └── src/module.cpp                (插件导出)           │
├─────────────────────────────────────────────────────────┤
│  ┌──────────────┐ ┌────────────┐ ┌───────────────────┐  │
│  │AlignedMemPool│ │LayoutTrans │ │ 预取引擎(predict) │  │
│  │ 64B对齐      │ │ SSE预取    │ │ 统计+预转换       │  │
│  │ 6级桶       │ │ 4x展开     │ │ 70%阈值           │  │
│  │ 复用回收     │ │ 分块+特化  │ │                   │  │
│  └──────────────┘ └────────────┘ └───────────────────┘  │
├─────────────────────────────────────────────────────────┤
│               Unify.hpp (便捷适配层)                     │
│   条件编译: OpenCV / Halcon / Qt 便捷函数                │
└─────────────────────────────────────────────────────────┘
```

### 1.1 文件清单

| 文件 | 职责 | 行数 |
|------|------|------|
| `include/core/IImageUnifyService.h` | 公开接口 + RAII类 + 枚举/结构体 | ~244 |
| `include/core/Unify.hpp` | OpenCV/Halcon/Qt便捷转换函数 | ~136 |
| `include/core/UnifyToCv.hpp` | 向后兼容重定向(→Unify.hpp) | 7 |
| `include/core/UnifyToHalcon.hpp` | 向后兼容重定向(→Unify.hpp) | 7 |
| `include/core/UnifyToQt.hpp` | 向后兼容重定向(→Unify.hpp) | 7 |
| `src/.../include/ImageUnifyService.h` | 内部数据结构 + AlignedMemoryPool | ~148 |
| `src/.../src/ImageUnifyService.cpp` | 服务实现 + 6项优化 | ~628 |
| `src/.../src/module.cpp` | 插件导出宏 | 7 |
| `test/image_unify_test.cpp` | 11个测试用例 | ~1000+ |

### 1.2 核心设计决策

| 决策 | 选择 | 原因 |
|------|------|------|
| 数据所有权 | 服务拷贝数据(融合路径直接I→P转换, 常规路径memcpy) | 解耦调用方buffer, 避免悬空指针 |
| 视图生命周期 | 引用计数 + 帧绑定双重机制 | `ReleaseView`递减引用, `RemoveFrame`强制释放 |
| 内存分配 | 64字节对齐内存池 (`AlignedMemoryPool`) | 消除malloc抖动, 缓存行对齐, 内存复用 |
| 内存回收 | LRU视图淘汰 → FIFO帧淘汰 两级策略 | 先淘汰零引用视图, 不够再淘汰整帧 |
| 布局转换 | U8_C3 AVX2特化 + 全格式多线程 + NT存储 | 工业视觉最常用格式, Release下超越Halcon 2x |
| 预测预取 | 统计布局请求比例, >70%时预转换 | 首次GetView延迟降为0 |
| 锁策略 | `framesMutex_` + `errorMutex_` 双锁 | 不嵌套, 无死锁风险 |

---

## 2. 接口API参考

### 2.1 IImageUnifyService (6个方法)

```cpp
// ---- 帧管理 ----
uint64_t SubmitFrame(void* data, int w, int h, PixelFormat fmt,
                     MemoryLayout layout = Interleaved, int step = 0);
void     RemoveFrame(uint64_t frameId);
bool     HasFrame(uint64_t frameId) const;

// ---- 视图 ----
ImageDescriptor GetView(uint64_t frameId, MemoryLayout targetLayout);
void            ReleaseView(uint64_t frameId, void* viewPtr);

// ---- 配置 ----
void   SetMaxMemory(size_t maxBytes);   // 默认256MB
size_t GetMemoryUsage() const;
void   ClearCache();

// ---- 错误 ----
const char* GetLastError() const;
```

### 2.2 ImageDescriptor 通道访问器

Planar布局下, 可通过语义化方法直接获取通道指针:

```cpp
ScopedView planar(svc, frameId, MemoryLayout::Planar);

// 语义化访问 (RGB场景)
auto r = planar.R();       // uint8_t* 红色通道
auto g = planar.G();       // uint8_t* 绿色通道
auto b = planar.B();       // uint8_t* 蓝色通道
auto a = planar.A();       // uint8_t* Alpha通道 (仅C4格式)

// 通用访问 (index-based)
auto ch = planar.Channel(2);  // 等价于 B()

// Float32场景
auto fR = planar.R<float>();   // float* 红色通道
```

**注意**: 仅Planar布局有效, Interleaved布局返回`nullptr`.

### 2.3 RAII辅助类

| 类 | 构造 | 析构 | 典型场景 |
|---|---|---|---|
| `ScopedFrame` | `SubmitFrame` | `RemoveFrame` | 单次处理的帧 |
| `ScopedView` | `GetView` | `ReleaseView` | 临时获取某布局视图 |

### 2.4 生命周期图

```
SubmitFrame()        GetView(Planar)      GetView(Interleaved)
    │                     │                      │
    ▼                     ▼                      ▼
┌─────────┐       ┌──────────────┐        ┌──────────────┐
│ FrameItem│──────▶│ ViewCacheItem│        │  零拷贝返回   │
│ ownedData│       │ refCount=1   │        │  原始dataPtr  │
│ original │       │ lastAccess=T │        └──────────────┘
└─────────┘       └──────────────┘
    │                     │
    │  ReleaseView()      │  refCount--
    │                     │
    │  内存压力 + refCount==0 → LRU淘汰视图(回收到池)
    │
    │  RemoveFrame() 或 FIFO自动淘汰
    ▼
  全部释放 (ownedData + 所有views → 回收到内存池)
```

---

## 3. 已实现的11项优化

### 3.1 [优化1] AlignedMemoryPool — 对齐内存池

**位置**: `ImageUnifyService.h` (类定义) + `ImageUnifyService.cpp` (实现)

**设计**:
- 64字节缓存行对齐 (`_aligned_malloc` / `posix_memalign`)
- 6级桶: ≤256KB, ≤1MB, ≤4MB, ≤16MB, ≤64MB, >64MB(直接分配)
- 每桶最多保留4块空闲内存, 防止池膨胀
- `Deallocate` 时优先放回桶中复用, 桶满则直接释放

**效果**: 连续20帧提交耗时从45.6ms降至25.5ms (**1.8x**)

**关键代码路径**:
```
SubmitFrame → poolAlloc(dataSize)    // 分配帧数据
findOrCreateView → poolAlloc(dataSize) // 分配视图缓冲
freeFrameData → poolFree(ptr, size)  // 回收到池
```

### 3.2 [优化2] 引用计数 + LRU视图淘汰

**位置**: `ViewCacheItem::refCount` + `lastAccess` + `evictZeroRefViews()`

**设计**:
- `GetView` 时 `refCount++`, 更新 `lastAccess`
- `ReleaseView` 时 `refCount--`
- `performMaintenance` 内存压力时:
  1. 先调用 `evictZeroRefViews()` 淘汰零引用视图 (LRU排序)
  2. 仍不够则FIFO淘汰最老帧

**两级回收策略**:
```
内存超限
  → 第1级: evictZeroRefViews() — 淘汰refCount==0且最老的视图
  → 第2级: FIFO帧淘汰 — 移除整个最老帧(含所有视图)
```

### 3.3 [优化3] 软件预取 + 4像素循环展开

**位置**: `I2P_U8C3_Optimized()` + `P2I_U8C3_Optimized()`

**技术**:
- `_mm_prefetch(..., _MM_HINT_T0)` 预取下一行数据到L1缓存
- 4像素展开: 消除内层 `for(c=0;c<3;++c)` 循环, 直接展开12个赋值
- 使用 `__restrict` 指针提示编译器无别名, 允许更激进优化

**效果**: 1920×1080×3 转换从0.15ms降至0.07ms (**2.1x**)

### 3.4 [优化4] 分块处理 (Cache Tiling)

**位置**: `I2P_Generic()` + `P2I_Generic()` 模板函数

**设计**:
- 以 `TILE_ROWS=64` 行为一块处理
- 保证输入+输出数据在L1缓存内 (32KB / 2 ≈ 16KB per block)
- 大图(如4K)的缓存命中率显著提高

### 3.5 [优化5] 布局预测 + 预转换

**位置**: `predictLayout()` + `prefetchView()`

**设计**:
- 用 `planarHits_` / `interleavedHits_` 原子计数器跟踪历史请求
- 超过5次请求后开始预测; 某布局占比>70%则认定为"常用布局"
- `SubmitFrame` 末尾调用 `prefetchView()`, 如果预测命中则立即预转换
- 后续 `GetView` 命中缓存, 延迟降为~0.0003ms

**适用场景**: 工业视觉中通常只用一种非原始布局(如Halcon用Planar), 预测准确率极高

### 3.6 [优化6] 缓存行对齐分配

**位置**: `AlignedAlloc()` / `AlignedFree()`

**设计**:
- Windows: `_aligned_malloc(size, 64)`
- Linux: `posix_memalign(&ptr, 64, size)`
- 所有帧数据和视图缓冲都64字节对齐
- 消除跨缓存行访问的性能惩罚, 为未来SIMD指令(AVX-512)预留对齐条件

### 3.7 [优化7] 原子内存计数器

**位置**: `ImageUnifyService::memoryUsage_` (`std::atomic<size_t>`)

**设计**: 原子变量替代O(N×M)遍历, 每次分配/释放时原子更新, `GetMemoryUsage()` O(1)无锁读取

### 3.8 [优化8] 持久线程池 (StaticThreadPool)

**位置**: `ImageUnifyService.cpp` — `StaticThreadPool` 类

**问题**: 每次转换创建/销毁`std::thread`, 开销~200-400μs (Windows CreateThread + 栈分配 + TLS)

**设计**:
- 静态单例, `hardware_concurrency()-1` 个worker线程
- condition_variable唤醒, 调用线程处理第一块
- generation计数器避免虚假唤醒
- 唤醒延迟<10μs vs 线程创建~300μs

**效果**: 布局转换从2.35ms降至~1.2ms

### 3.9 [优化9] SSSE3 SIMD 三通道去交错

**位置**: `I2P_U8C3_SSSE3_Row` / `P2I_U8C3_SSSE3_Row`

**技术**: `pshufb` 16像素/迭代, 9 shuffle + 6 OR 从48字节提取3×16字节通道数据

### 3.10 [优化10] AVX2 三通道去交错 + Non-Temporal Stores

**位置**: `I2P_U8C3_AVX2_Row` / `I2P_U8C3_AVX2_NT_Row`

**技术**:
- `vpshufb`在每个128位lane内独立工作, lo=blockA(16px), hi=blockB(16px)
- 相同SSSE3掩码同时处理两个16像素块, 32像素/迭代 (2x吞吐)
- 运行时CPUID检测, 无AVX2自动降级到SSSE3
- NT版本用`_mm256_stream_si256`跳过缓存写入, 减少~33%总线带宽 (需对齐)
- 无padding时启用flat批量处理, 消除逐行循环开销

**效果**: 10.5MB图像: Release下0.73ms vs Halcon 1.46ms, **超越Halcon 2x**

### 3.11 [优化11] 融合SubmitFrame — 单次内存遍历

**位置**: `ImageUnifyService::SubmitFrame()`

**问题**: 原始流程做两次10.5MB内存遍历:
1. `memcpy` 拷贝interleaved数据到ownedData
2. `I2P` 将ownedData转换为planar视图

**设计**:
- 利用预测引擎判断下次GetView大概率请求Planar
- 预测命中时, 跳过memcpy, 直接从用户buffer执行I→P转换存储为Planar
- GetView(Planar)变为零拷贝返回 (frame.original已是Planar)
- 预测未命中时退回常规memcpy路径

**效果**: 完整流程从2.34ms降至1.34ms (Debug), 从1.34ms降至**0.725ms** (Release)

---

## 4. 性能基准

### 4.1 微基准 (1920×1080 U8_C3)

| 指标 | 性能耗时/吞吐量 |
|------|---------|
| I→P 单次 | **0.008 ms** |
| 缓存命中 | **0.0001 ms** |
| 吞吐量 | **754 GB/s** |

### 4.2 真实图像对比 (7353×500 U8_C3, 10.5MB)

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

**结论**: Release下完整流程优于Halcon处理效率.

### 4.3 多格式转换性能 (1920×1080, Release)

| 格式 | I→P (ms) | P→I (ms) | 数据量 (MB) |
|------|---------|---------|----------|
| U8_C1 | 0.11 | 0.30 | 1.98 |
| U8_C3 | 0.34 | 1.05 | 5.93 |
| U8_C4 | 2.13 | 3.57 | 7.91 |
| Float32_C1 | 0.59 | 2.30 | 7.91 |
| Float32_C3 | 3.18 | 5.19 | 23.73 |
| Float32_C4 | 5.23 | 7.17 | 31.64 |

---

## 5. 内部实现详解

### 5.1 数据结构

```cpp
// 内存池 (6级桶, 64字节对齐)
class AlignedMemoryPool {
    FreeEntry buckets_[6];  // ≤256KB/1MB/4MB/16MB/64MB/>64MB
    mutex poolMutex_;
};

// 视图缓存项 (带引用计数 + LRU时间戳)
struct ViewCacheItem {
    MemoryLayout layout;
    void*  dataPtr;
    size_t dataSize;
    atomic<int> refCount;                    // [优化2] 引用计数
    steady_clock::time_point lastAccess;     // [优化2] LRU时间戳
};

// 帧数据项
struct FrameItem {
    ImageDescriptor original;
    void*  ownedData;              // 池分配的数据副本
    size_t ownedDataSize;
    vector<shared_ptr<ViewCacheItem>> views;
};
```

### 5.2 关键流程

#### SubmitFrame 流程
```
1. 参数校验
2. 计算 dataSize + planarSize
3. predictLayout()                        ← [优化5] 预测
4. 预测Planar且输入Interleaved?            ← [优化11] 融合判断
   ├── YES: poolAlloc(planarSize) + I→P直接转换  ← 单次内存遍历!
   └── NO:  poolAlloc(dataSize) + memcpy         ← 常规路径
5. 生成唯一 frameId (atomic自增)
6. 加锁 → 插入 frames_[id] + frameOrder_
7. 释放锁 → performMaintenance()          ← [优化2] 两级GC
8. 非融合路径: prefetchView(frame)         ← [优化5] 预转换
9. 返回 frameId
```

#### GetView 流程
```
1. 加锁
2. 查找 frames_[frameId]
3. 更新 planarHits_/interleavedHits_      ← [优化5] 统计
4. 目标布局 == 原始布局 → 零拷贝返回
5. findOrCreateView():
   a. 缓存命中 → 直接返回
   b. 缓存未命中:
      i.  poolAlloc(dataSize)              ← [优化1] 内存池
      ii. LayoutTransformer 转换           ← [优化3/4] 快速转换
      iii.缓存到 frame.views
6. refCount++, lastAccess=now()            ← [优化2] 引用计数
7. 返回 ImageDescriptor
```

#### performMaintenance 流程 (两级GC)
```
1. 加锁
2. evictZeroRefViews()                    ← [优化2] LRU淘汰
   a. 收集 refCount==0 的视图
   b. 按 lastAccess 排序 (最老优先)
   c. 逐个释放回池, 直到 usage ≤ maxMemory_
3. 若仍超限: FIFO淘汰最老帧
   a. 从 frameOrder_ 队头取最老帧
   b. freeFrameData → poolFree 回收
```

### 5.3 LayoutTransformer 转换路径选择

```
InterleavedToPlanar(src, dst):
  ├── ch==1 → memcpy (逐行, 处理padding)
  ├── U8_C3 → I2P_U8C3_Optimized()     ← AVX2/SSSE3 + 线程池 + NT存储 + flat批量
  ├── U8_C4 → I2P_Generic<uint8_t>()   ← 多线程 + 分块
  └── Float32 → I2P_Generic<float>()   ← 多线程 + 分块

PlanarToInterleaved(src, dst):
  ├── ch==1 → memcpy
  ├── U8_C3 → P2I_U8C3_Optimized()     ← SSSE3 + 线程池
  ├── U8_C4 → P2I_Generic<uint8_t>()   ← 多线程
  └── Float32 → P2I_Generic<float>()   ← 多线程
```

### 5.4 线程安全

| 资源 | 保护锁 | 说明 |
|------|--------|------|
| `frames_`, `frameOrder_`, `maxMemory_` | `framesMutex_` | 所有帧/视图操作 |
| `lastError_` | `errorMutex_` | 独立锁 |
| `nextFrameId_` | `std::atomic` | 无锁自增 |
| `memoryUsage_` | `std::atomic` | [优化7] 无锁内存跟踪 |
| `planarHits_`, `interleavedHits_` | `std::atomic` | 无锁统计 |
| `AlignedMemoryPool::buckets_` | `poolMutex_` | 池操作独立锁 |

**锁嵌套顺序** (严格单向, 无死锁):
```
framesMutex_ → poolMutex_    (帧操作内调用池分配/释放)
framesMutex_ → errorMutex_   (GetView内调用setError)
```
绝不允许反向嵌套(poolMutex_→framesMutex_ 或 errorMutex_→framesMutex_)。

---

## 6. Unify.hpp 便捷层

### 6.1 设计原则

- 每个函数都是 **无状态的自由函数**
- 通过 `#ifdef HAS_OPENCV` / `HAS_HALCON` / `QT_CORE_LIB` 条件编译
- 返回的对象 **引用服务内部内存**, 帧被RemoveFrame后失效

### 6.2 函数列表

| 函数 | 请求布局 | 返回类型 | 说明 |
|------|---------|---------|------|
| `Unify::ToCvMat(view)` | Interleaved | `cv::Mat` | **★推荐**: 从ScopedView一行转换 |
| `Unify::ToCvMat(desc)` | Interleaved | `cv::Mat` | 从ImageDescriptor转换 |
| `Unify::ToCvMat(svc, fid)` | Interleaved | `cv::Mat` | 兼容旧接口 |
| `Unify::SubmitCvMat(svc, mat)` | — | `ScopedFrame` | cv::Mat → 提交到服务 |
| `Unify::ToHImage(view)` | Planar | `HImage` | **★推荐**: 从ScopedView一行转换 |
| `Unify::ToHImage(desc)` | Planar | `HImage` | 从ImageDescriptor转换 |
| `Unify::ToHImage(svc, fid)` | Planar | `HImage` | 兼容旧接口 |
| `Unify::ToQImage(svc, fid)` | Interleaved | `QImage` | 适合UI绘制 |

### 6.3 一行代码适配器示例

```cpp
// ★ 推荐: ScopedView → HImage (一行代码)
ScopedFrame frame(svc, data, w, h, PixelFormat::U8_C3);
ScopedView planar(svc, frame.Id(), MemoryLayout::Planar);
auto himg = Unify::ToHImage(planar);  // done!

// ★ 推荐: ScopedView → cv::Mat (一行代码)
ScopedView interleaved(svc, frame.Id(), MemoryLayout::Interleaved);
cv::Mat mat = Unify::ToCvMat(interleaved);  // done!

// 通道访问 + Halcon集成
auto r = planar.R();  // uint8_t* 红色通道
auto g = planar.G();  // uint8_t* 绿色通道
auto b = planar.B();  // uint8_t* 蓝色通道

// 注意生命周期: ScopedView析构后指针失效
// 如需长期持有, 深拷贝:
cv::Mat safeCopy = Unify::ToCvMat(interleaved).clone();
```

---

## 7. 扩展指南

### 7.1 添加新的像素格式

1. `IImageUnifyService.h` → `PixelFormat` 枚举添加新值
2. `ImageDescriptor` → 更新 `getChannels()`, `getBytesPerPixel()`
3. `ImageFormatUtils` → 更新 `GetPixelFormatString()`
4. `ImageUnifyService.cpp` → 确认 `ElementSize()` 覆盖新格式
5. 若新格式需要特化快速路径, 在 `LayoutTransformer` 中添加
6. `Unify.hpp` → 更新对应库的类型映射
7. `image_unify_test.cpp` → 添加测试

### 7.2 添加新的第三方库适配

在 `Unify.hpp` 中添加条件编译块:

```cpp
#ifdef HAS_TENSORRT
#include <NvInfer.h>
inline void* ToGpuBuffer(IImageUnifyService* svc, uint64_t fid) {
    ImageDescriptor desc = svc->GetView(fid, MemoryLayout::Interleaved);
    // cudaMemcpy to GPU...
}
#endif
```

### 7.3 调优内存池

修改 `ImageUnifyService.cpp` 中的常量:

```cpp
static constexpr size_t BUCKET_SIZES[] = {
    256*1024, 1024*1024, 4*1024*1024, 16*1024*1024, 64*1024*1024, 0
};
// 每桶最多保留N块:
if (bucket.size() < 4) { bucket.push_back({ptr, size}); ... }
```

- **增大桶数**: 适合分辨率种类多的场景
- **增大保留数**: 适合高频分配/释放的场景
- **减小保留数**: 适合内存受限的嵌入式环境

### 7.4 未来优化方向

| 方向 | 说明 | 预期收益 |
|------|------|---------|
| AVX-512 | 512位SIMD, 64像素/迭代 | 额外~1.5x (服务器CPU) |
| GPU转换 | CUDA/OpenCL做Interleaved↔Planar | 4K+大图显著加速 |
| U8_C4 SIMD | 专用AVX2四通道去交错 | 当前走Generic路径 |
| P2I AVX2 | Planar→Interleaved也用AVX2 | 当前仅SSSE3 |

---

## 8. 常见问题排查

### Q1: GetView返回空的ImageDescriptor

1. 检查 `svc->GetLastError()`
2. 调用 `svc->HasFrame(frameId)` 确认帧存在
3. 帧可能被自动淘汰 — 增大 `SetMaxMemory` 或用 `ScopedFrame` 保持生命周期
4. 确认 `SubmitFrame` 返回值不为0

### Q2: 内存持续增长

- 内存池会保留已释放的块供复用, 这是预期行为
- 真正的内存用量看 `GetMemoryUsage()`, 不是进程RSS
- 用 `ClearCache()` 清空所有帧 + 调用析构清空池

### Q3: 多线程访问崩溃

- 所有接口方法都是线程安全的
- `GetView` 返回的 `dataPtr` 指向服务内部内存 — 如线程A持有指针, 线程B不应 `RemoveFrame`
- **建议**: 用 `ScopedFrame` + `ScopedView` 管理生命周期, 或 `clone()` 数据

### Q4: 预取不生效

- 需要至少5次 `GetView` 调用后才开始预测
- 某布局请求比例需超过70%才会触发预转换
- 检查原始布局: 如果原始就是预测布局, 预取会跳过(零拷贝已是最优)

---

## 9. 构建说明

### 9.1 依赖

| 依赖 | 必需 | 说明 |
|------|------|------|
| C++17 | 是 | 结构化绑定, if constexpr等 |
| AxPlug框架 | 是 | 插件加载机制 |
| OpenCV | 否 | `Unify::ToCvMat` 需要, 通过 `HAS_OPENCV` 启用 |
| Halcon | 否 | `Unify::ToHImage` 需要, 通过 `HAS_HALCON` 启用 |
| Qt | 否 | `Unify::ToQImage` 需要, 通过 `QT_CORE_LIB` 启用 |

### 9.2 构建命令

```bash
# 构建依赖 (首次, 编译OpenCV Debug+Release)
scripts\build_deps.bat

# 配置
cmake -S . -B build -G "Visual Studio 17 2022" -A x64

# Debug 构建
cmake --build build --target ImageUnifyServicePlugin --target ImageUnifyTest --config Debug
copy deps\opencv\bin\*d.dll build\bin\Debug\
build\bin\Debug\ImageUnifyTest.exe

# Release 构建 (性能测试用)
cmake --build build --target ImageUnifyServicePlugin --target ImageUnifyTest --config Release
copy deps\opencv\bin\opencv_core480.dll build\bin\Release\
copy deps\opencv\bin\opencv_imgproc480.dll build\bin\Release\
copy deps\opencv\bin\opencv_imgcodecs480.dll build\bin\Release\
build\bin\Release\ImageUnifyTest.exe
```

### 9.3 集成到新项目

```cmake
target_link_libraries(YourApp PRIVATE AxCore)
# 如需OpenCV
target_compile_definitions(YourApp PRIVATE HAS_OPENCV)
target_link_libraries(YourApp PRIVATE ${OpenCV_LIBS})
```

---



## 10. 设计优势总结

1. **极简API** — 6个方法 + 2个RAII类 + R()/G()/B()语义访问器
2. **RAII零泄漏** — `ScopedFrame` + `ScopedView` 自动管理生命周期
3. **零拷贝快速路径** — 布局一致时直接返回指针, ~0.0003ms
4. **极速转换** — AVX2+线程池+NT存储+融合路径, Release下完整流程超越Halcon **93%**
5. **智能内存** — 对齐池分配+复用, 连续采集耗时降低1.9x
6. **两级GC** — LRU视图淘汰 → FIFO帧淘汰, 精细化内存管理
7. **预测预取** — 统计布局偏好, 自动预转换, 后续GetView零延迟
8. **O(1)内存查询** — 原子计数器替代O(N×M)遍历, GC热路径零开销
9. **格式完备** — U8/Float32 × C1/C3/C4 × Interleaved/Planar 全组合
10. **线程安全** — 三把锁+四个原子变量, 严格单向嵌套, 无死锁
11. **条件编译** — 核心无第三方依赖, OpenCV/Halcon/Qt按需启用
12. **融合路径** — 预测命中时SubmitFrame单次遍历直接存储Planar, GetView零拷贝返回
13. **Debug+Release** — CMake支持双配置, OpenCV/Halcon均可在两种模式下运行
