#pragma once
#include "core/IImageUnifyService.h"
#include "AxPlug/AxPluginExport.h"
#include <unordered_map>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <deque>
#include <chrono>
#include <algorithm>
#include <cstring>

// ============================================================================
//  [优化1] 对齐内存池 — 替换malloc, 减少分配延迟和碎片
// ============================================================================
class AlignedMemoryPool {
public:
    static constexpr size_t ALIGN = 64;  // 缓存行对齐
    static constexpr size_t BUCKET_COUNT = 6;
    // 桶: <=256KB, <=1MB, <=4MB, <=16MB, <=64MB, >64MB(直接分配)

    AlignedMemoryPool() = default;
    ~AlignedMemoryPool();

    void* Allocate(size_t size);
    void  Deallocate(void* ptr, size_t size);
    void  Clear();
    size_t GetPooledCount() const;

    AlignedMemoryPool(const AlignedMemoryPool&) = delete;
    AlignedMemoryPool& operator=(const AlignedMemoryPool&) = delete;

private:
    static size_t BucketIndex(size_t size);
    struct FreeEntry { void* ptr; size_t size; };
    std::vector<FreeEntry> buckets_[BUCKET_COUNT];
    std::mutex poolMutex_;
};

// ============================================================================
//  [优化2] 视图缓存项 — 恢复引用计数 + LRU时间戳
// ============================================================================
struct ViewCacheItem {
    MemoryLayout layout;
    void*  dataPtr;
    size_t dataSize;
    std::atomic<int> refCount{0};                          // [优化2] 引用计数
    std::chrono::steady_clock::time_point lastAccess;      // [优化2] LRU时间戳

    ViewCacheItem() : layout(MemoryLayout::Unknown), dataPtr(nullptr), dataSize(0) {
        lastAccess = std::chrono::steady_clock::now();
    }
    ~ViewCacheItem() = default;  // 内存由服务统一回收(池或aligned_free)

    ViewCacheItem(const ViewCacheItem&) = delete;
    ViewCacheItem& operator=(const ViewCacheItem&) = delete;
};

// ============================================================================
//  帧数据项
// ============================================================================
struct FrameItem {
    ImageDescriptor original;
    void*  ownedData;
    size_t ownedDataSize;
    std::vector<std::shared_ptr<ViewCacheItem>> views;
    std::mutex itemMutex;  // Fix #2: Per-item lock for fine-grained GetView

    FrameItem() : ownedData(nullptr), ownedDataSize(0) {}
    ~FrameItem() = default;  // 内存由服务统一回收

    FrameItem(const FrameItem&) = delete;
    FrameItem& operator=(const FrameItem&) = delete;
};

// ============================================================================
//  [优化3] 布局转换器 — 带软件预取 + 循环展开 + 分块优化
// ============================================================================
class LayoutTransformer {
public:
    static bool InterleavedToPlanar(const ImageDescriptor& src, void* dstPtr);
    static bool PlanarToInterleaved(const ImageDescriptor& src, void* dstPtr);
};

// ============================================================================
//  主服务实现
// ============================================================================
class ImageUnifyService : public IImageUnifyService {
public:
    ImageUnifyService();
    ~ImageUnifyService() override;

    void OnShutdown() override;

protected:
    void Destroy() override { delete this; }

    uint64_t SubmitFrame(void* data, int width, int height,
                         PixelFormat format, MemoryLayout layout, int step) override;
    void     RemoveFrame(uint64_t frameId) override;
    bool     HasFrame(uint64_t frameId) const override;

    ImageDescriptor GetView(uint64_t frameId, MemoryLayout targetLayout) override;
    void            ReleaseView(uint64_t frameId, void* viewPtr) override;

    void   SetMaxMemory(size_t maxBytes) override;
    size_t GetMemoryUsage() const override;
    void   ClearCache() override;
    const char* GetLastError() const override;

private:
    // 帧存储
    std::unordered_map<uint64_t, std::unique_ptr<FrameItem>> frames_;
    mutable std::mutex framesMutex_;
    std::deque<uint64_t> frameOrder_;
    std::atomic<uint64_t> nextFrameId_{1};

    // [优化1] 内存池
    AlignedMemoryPool pool_;

    // 内存限制
    size_t maxMemory_ = 256 * 1024 * 1024;

    // [优化07] 内存使用量原子计数器 — O(1)查询替代O(N*M)遍历
    std::atomic<size_t> memoryUsage_{0};

    // [优化5] 预取: 跟踪布局请求统计, 预测常用目标布局
    std::atomic<int> planarHits_{0};
    std::atomic<int> interleavedHits_{0};

    // 错误 (线程安全)
    mutable std::string lastError_;
    mutable std::mutex  errorMutex_;
    void setError(const std::string& msg) const {
        std::lock_guard<std::mutex> lock(errorMutex_);
        lastError_ = msg;
    }

    // Fix 2.2: Orphaned views with refCount > 0 deferred for cleanup
    std::vector<std::shared_ptr<ViewCacheItem>> orphanedViews_;

    // 内部方法
    void* poolAlloc(size_t size);
    void  poolFree(void* ptr, size_t size);
    void  freeFrameData(FrameItem& frame);
    void  cleanupOrphanedViews();

    std::shared_ptr<ViewCacheItem> findOrCreateView(FrameItem& frame,
                                                     MemoryLayout targetLayout);
    void performMaintenance();
    void evictZeroRefViews();        // [优化2] LRU淘汰零引用视图

    MemoryLayout predictLayout() const;   // [优化5] 预测下次请求布局
    void prefetchView(FrameItem& frame);  // [优化5] 预转换
};
