#include "../include/ImageUnifyService.h"
#include <iostream>
#include <cstring>
#include <string>
#include <thread>
#include <functional>
#include <condition_variable>

// [优化3] 软件预取指令
#if defined(_MSC_VER)
#include <intrin.h>
#define PREFETCH_READ(addr)  _mm_prefetch(reinterpret_cast<const char*>(addr), _MM_HINT_T0)
#elif defined(__GNUC__) || defined(__clang__)
#define PREFETCH_READ(addr)  __builtin_prefetch(addr, 0, 3)
#else
#define PREFETCH_READ(addr)  ((void)0)
#endif

// [优化6] 对齐内存分配
#ifdef _WIN32
#include <malloc.h>
static void* AlignedAlloc(size_t alignment, size_t size) { return _aligned_malloc(size, alignment); }
static void  AlignedFree(void* ptr) { _aligned_free(ptr); }
#else
#include <cstdlib>
static void* AlignedAlloc(size_t alignment, size_t size) {
    void* ptr = nullptr;
    posix_memalign(&ptr, alignment, size);
    return ptr;
}
static void AlignedFree(void* ptr) { free(ptr); }
#endif

// ============================================================================
//  [优化1] AlignedMemoryPool 实现
// ============================================================================

static constexpr size_t BUCKET_SIZES[] = {
    256 * 1024,        // 0: <=256KB
    1024 * 1024,       // 1: <=1MB
    4 * 1024 * 1024,   // 2: <=4MB
    16 * 1024 * 1024,  // 3: <=16MB
    64 * 1024 * 1024,  // 4: <=64MB
    0                  // 5: >64MB (直接分配, 不缓存)
};

size_t AlignedMemoryPool::BucketIndex(size_t size) {
    for (size_t i = 0; i < BUCKET_COUNT - 1; ++i) {
        if (size <= BUCKET_SIZES[i]) return i;
    }
    return BUCKET_COUNT - 1;
}

AlignedMemoryPool::~AlignedMemoryPool() { Clear(); }

void* AlignedMemoryPool::Allocate(size_t size) {
    size_t idx = BucketIndex(size);

    // 尝试从池中复用
    if (idx < BUCKET_COUNT - 1) {
        std::lock_guard<std::mutex> lock(poolMutex_);
        auto& bucket = buckets_[idx];
        for (auto it = bucket.begin(); it != bucket.end(); ++it) {
            if (it->size >= size) {
                void* ptr = it->ptr;
                bucket.erase(it);
                return ptr;
            }
        }
    }

    // 池中没有, 新分配(对齐到缓存行)
    return AlignedAlloc(ALIGN, size);
}

void AlignedMemoryPool::Deallocate(void* ptr, size_t size) {
    if (!ptr) return;
    size_t idx = BucketIndex(size);

    // 小块: 放回池中复用; 大块(>64MB): 直接释放
    if (idx < BUCKET_COUNT - 1) {
        std::lock_guard<std::mutex> lock(poolMutex_);
        auto& bucket = buckets_[idx];
        // 每个桶最多保留4块, 防止池膨胀
        if (bucket.size() < 4) {
            bucket.push_back({ptr, size});
            return;
        }
    }
    AlignedFree(ptr);
}

void AlignedMemoryPool::Clear() {
    std::lock_guard<std::mutex> lock(poolMutex_);
    for (size_t i = 0; i < BUCKET_COUNT; ++i) {
        for (auto& e : buckets_[i]) AlignedFree(e.ptr);
        buckets_[i].clear();
    }
}

size_t AlignedMemoryPool::GetPooledCount() const {
    size_t count = 0;
    for (size_t i = 0; i < BUCKET_COUNT; ++i)
        count += buckets_[i].size();
    return count;
}

// ============================================================================
//  [优化3] LayoutTransformer 实现 — 软件预取 + 循环展开 + 分块
// ============================================================================

static bool IsFloat32(PixelFormat fmt) {
    return fmt == PixelFormat::Float32_C1 || fmt == PixelFormat::Float32_C3 || fmt == PixelFormat::Float32_C4;
}

static int ElementSize(PixelFormat fmt) {
    return IsFloat32(fmt) ? 4 : 1;
}

// [优化4] 分块大小 — L1缓存友好
static constexpr int TILE_ROWS = 64;

// ============================================================================
//  [优化8] 持久线程池 — 消除std::thread创建/销毁开销
// ============================================================================
static constexpr int MT_PIXEL_THRESHOLD = 200000;

class StaticThreadPool {
    struct WorkItem {
        std::function<void(int, int)> func;
        int y0 = 0, y1 = 0;
    };
    std::vector<std::thread>  workers_;
    std::vector<WorkItem>     items_;
    std::vector<int>          workerGen_;
    std::mutex                mutex_;
    std::mutex                submitMutex_;  // Fix 2.1: Prevent concurrent parallelFor overwrites
    std::condition_variable   startCv_;
    std::condition_variable   doneCv_;
    std::atomic<int>          pending_{0};
    int                       generation_ = 0;
    bool                      stop_ = false;

public:
    static StaticThreadPool& Instance() {
        static StaticThreadPool pool(
            std::max(0, static_cast<int>(std::thread::hardware_concurrency()) - 1));
        return pool;
    }

    int totalThreads() const { return static_cast<int>(workers_.size()) + 1; }

    void parallelFor(int h, const std::function<void(int, int)>& func) {
        int n = static_cast<int>(workers_.size());
        if (n <= 0 || h <= 0) { func(0, h); return; }

        // Fix 2.1: Serialize concurrent parallelFor calls to prevent item overwrite
        std::lock_guard<std::mutex> submitLock(submitMutex_);

        int total = n + 1;  // workers + calling thread
        int chunk = (h + total - 1) / total;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (int i = 0; i < n; ++i) {
                int y0 = (i + 1) * chunk;
                int y1 = std::min(y0 + chunk, h);
                items_[i] = {func, y0, y1};
            }
            pending_.store(n, std::memory_order_release);
            generation_++;
        }
        startCv_.notify_all();

        // 调用线程处理第一块
        func(0, std::min(chunk, h));

        // 等待所有worker完成
        std::unique_lock<std::mutex> lock(mutex_);
        doneCv_.wait(lock, [this] { return pending_.load(std::memory_order_acquire) == 0; });
    }

private:
    explicit StaticThreadPool(int n)
        : items_(std::max(n, 0)), workerGen_(std::max(n, 0), 0)
    {
        for (int i = 0; i < n; ++i) {
            workers_.emplace_back([this, i] {
                while (true) {
                    WorkItem item;
                    {
                        std::unique_lock<std::mutex> lock(mutex_);
                        startCv_.wait(lock, [this, i] {
                            return stop_ || generation_ > workerGen_[i];
                        });
                        if (stop_) return;
                        workerGen_[i] = generation_;
                        item = items_[i];
                    }
                    if (item.y0 < item.y1) item.func(item.y0, item.y1);
                    if (pending_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                        doneCv_.notify_one();
                    }
                }
            });
        }
    }

    ~StaticThreadPool() {
        { std::lock_guard<std::mutex> lock(mutex_); stop_ = true; }
        startCv_.notify_all();
        for (auto& w : workers_) w.join();
    }

    StaticThreadPool(const StaticThreadPool&) = delete;
    StaticThreadPool& operator=(const StaticThreadPool&) = delete;
};

static void ParallelForRows(int h, const std::function<void(int, int)>& func) {
    if (h <= 0) return;
    StaticThreadPool::Instance().parallelFor(h, func);
}

// ============================================================================
//  [优化9] SSSE3 SIMD 三通道去交错 — 16像素/迭代
//
//  核心思路: 加载48字节(=16像素×3通道), 用 pshufb 按通道索引抽取,
//           OR合并三组结果, 得到 3×16字节 的三个独立通道.
// ============================================================================
#if defined(_MSC_VER) || defined(__SSSE3__)
#include <immintrin.h>  // SSSE3 + AVX2

// ---- Interleaved → Planar (SSSE3, 单行) ----
static void I2P_U8C3_SSSE3_Row(const uint8_t* __restrict src,
                                uint8_t* __restrict d0,
                                uint8_t* __restrict d1,
                                uint8_t* __restrict d2,
                                int w)
{
    // 48 bytes = 16 pixels: v0[0..15] v1[16..31] v2[32..47]
    //   pixel  0: ch0=v0[0],  ch1=v0[1],  ch2=v0[2]
    //   pixel  5: ch0=v0[15], ch1=v1[0],  ch2=v1[1]
    //   pixel 10: ch0=v1[14], ch1=v1[15], ch2=v2[0]
    //   pixel 15: ch0=v2[13], ch1=v2[14], ch2=v2[15]

    // Channel 0: extract 1st byte of each triplet
    const __m128i m0_c0 = _mm_setr_epi8( 0, 3, 6, 9,12,15,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1);
    const __m128i m1_c0 = _mm_setr_epi8(-1,-1,-1,-1,-1,-1, 2, 5, 8,11,14,-1,-1,-1,-1,-1);
    const __m128i m2_c0 = _mm_setr_epi8(-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, 1, 4, 7,10,13);
    // Channel 1: extract 2nd byte of each triplet
    const __m128i m0_c1 = _mm_setr_epi8( 1, 4, 7,10,13,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1);
    const __m128i m1_c1 = _mm_setr_epi8(-1,-1,-1,-1,-1, 0, 3, 6, 9,12,15,-1,-1,-1,-1,-1);
    const __m128i m2_c1 = _mm_setr_epi8(-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, 2, 5, 8,11,14);
    // Channel 2: extract 3rd byte of each triplet
    const __m128i m0_c2 = _mm_setr_epi8( 2, 5, 8,11,14,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1);
    const __m128i m1_c2 = _mm_setr_epi8(-1,-1,-1,-1,-1, 1, 4, 7,10,13,-1,-1,-1,-1,-1,-1);
    const __m128i m2_c2 = _mm_setr_epi8(-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, 0, 3, 6, 9,12,15);

    int x = 0;
    for (; x + 15 < w; x += 16) {
        const uint8_t* p = src + x * 3;
        __m128i v0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p));
        __m128i v1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p + 16));
        __m128i v2 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p + 32));

        __m128i c0 = _mm_or_si128(_mm_or_si128(
            _mm_shuffle_epi8(v0, m0_c0),
            _mm_shuffle_epi8(v1, m1_c0)),
            _mm_shuffle_epi8(v2, m2_c0));

        __m128i c1 = _mm_or_si128(_mm_or_si128(
            _mm_shuffle_epi8(v0, m0_c1),
            _mm_shuffle_epi8(v1, m1_c1)),
            _mm_shuffle_epi8(v2, m2_c1));

        __m128i c2 = _mm_or_si128(_mm_or_si128(
            _mm_shuffle_epi8(v0, m0_c2),
            _mm_shuffle_epi8(v1, m1_c2)),
            _mm_shuffle_epi8(v2, m2_c2));

        _mm_storeu_si128(reinterpret_cast<__m128i*>(d0 + x), c0);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(d1 + x), c1);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(d2 + x), c2);
    }
    // 标量尾部
    for (; x < w; ++x) {
        d0[x] = src[x * 3];
        d1[x] = src[x * 3 + 1];
        d2[x] = src[x * 3 + 2];
    }
}

// ---- Planar → Interleaved (SSSE3, 单行) ----
//  与去交错完全对称的 9-shuffle + 6-OR 方案
//  输入: s0[0..15]=ch0, s1[0..15]=ch1, s2[0..15]=ch2 (16像素)
//  输出: dst[0..47] = [ch0_0,ch1_0,ch2_0, ch0_1,ch1_1,ch2_1, ...]
static void P2I_U8C3_SSSE3_Row(const uint8_t* __restrict s0,
                                const uint8_t* __restrict s1,
                                const uint8_t* __restrict s2,
                                uint8_t* __restrict dst,
                                int w)
{
    // out0: pixels 0-5 (16 bytes: 5*3+1)
    //   c0 at positions 0,3,6,9,12,15 from c0[0..5]
    //   c1 at positions 1,4,7,10,13   from c1[0..4]
    //   c2 at positions 2,5,8,11,14   from c2[0..4]
    const __m128i mc0_o0 = _mm_setr_epi8( 0,-1,-1, 1,-1,-1, 2,-1,-1, 3,-1,-1, 4,-1,-1, 5);
    const __m128i mc1_o0 = _mm_setr_epi8(-1, 0,-1,-1, 1,-1,-1, 2,-1,-1, 3,-1,-1, 4,-1,-1);
    const __m128i mc2_o0 = _mm_setr_epi8(-1,-1, 0,-1,-1, 1,-1,-1, 2,-1,-1, 3,-1,-1, 4,-1);

    // out1: pixels 5-10 (16 bytes)
    //   c1 at positions 0,3,6,9,12,15 from c1[5..10]
    //   c2 at positions 1,4,7,10,13   from c2[5..9]
    //   c0 at positions 2,5,8,11,14   from c0[6..10]
    const __m128i mc0_o1 = _mm_setr_epi8(-1,-1, 6,-1,-1, 7,-1,-1, 8,-1,-1, 9,-1,-1,10,-1);
    const __m128i mc1_o1 = _mm_setr_epi8( 5,-1,-1, 6,-1,-1, 7,-1,-1, 8,-1,-1, 9,-1,-1,10);
    const __m128i mc2_o1 = _mm_setr_epi8(-1, 5,-1,-1, 6,-1,-1, 7,-1,-1, 8,-1,-1, 9,-1,-1);

    // out2: pixels 10-15 (16 bytes)
    //   c2 at positions 0,3,6,9,12,15 from c2[10..15]
    //   c0 at positions 1,4,7,10,13   from c0[11..15]
    //   c1 at positions 2,5,8,11,14   from c1[11..15]
    const __m128i mc0_o2 = _mm_setr_epi8(-1,11,-1,-1,12,-1,-1,13,-1,-1,14,-1,-1,15,-1,-1);
    const __m128i mc1_o2 = _mm_setr_epi8(-1,-1,11,-1,-1,12,-1,-1,13,-1,-1,14,-1,-1,15,-1);
    const __m128i mc2_o2 = _mm_setr_epi8(10,-1,-1,11,-1,-1,12,-1,-1,13,-1,-1,14,-1,-1,15);

    int x = 0;
    for (; x + 15 < w; x += 16) {
        __m128i c0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s0 + x));
        __m128i c1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s1 + x));
        __m128i c2 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s2 + x));

        __m128i out0 = _mm_or_si128(_mm_or_si128(
            _mm_shuffle_epi8(c0, mc0_o0),
            _mm_shuffle_epi8(c1, mc1_o0)),
            _mm_shuffle_epi8(c2, mc2_o0));

        __m128i out1 = _mm_or_si128(_mm_or_si128(
            _mm_shuffle_epi8(c0, mc0_o1),
            _mm_shuffle_epi8(c1, mc1_o1)),
            _mm_shuffle_epi8(c2, mc2_o1));

        __m128i out2 = _mm_or_si128(_mm_or_si128(
            _mm_shuffle_epi8(c0, mc0_o2),
            _mm_shuffle_epi8(c1, mc1_o2)),
            _mm_shuffle_epi8(c2, mc2_o2));

        uint8_t* o = dst + x * 3;
        _mm_storeu_si128(reinterpret_cast<__m128i*>(o),      out0);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(o + 16), out1);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(o + 32), out2);
    }
    // 标量尾部
    for (; x < w; ++x) {
        dst[x * 3]     = s0[x];
        dst[x * 3 + 1] = s1[x];
        dst[x * 3 + 2] = s2[x];
    }
}

#define HAS_SSSE3_I2P 1

// ============================================================================
//  [优化10] AVX2 三通道去交错 — 32像素/迭代 (2x SSSE3吞吐量)
//
//  原理: vpshufb 在每个128位lane内独立工作, 因此:
//    lo lane = block A (16像素), hi lane = block B (16像素)
//    使用相同的SSSE3掩码同时处理两个16像素块
//    256位存储自然得到连续的32字节通道数据
// ============================================================================
#ifdef _MSC_VER
static bool DetectAVX2() {
    int info[4] = {0};
    __cpuidex(info, 7, 0);
    return (info[1] & (1 << 5)) != 0;
}
static const bool g_hasAVX2 = DetectAVX2();
#else
static const bool g_hasAVX2 = false;
#endif

static void I2P_U8C3_AVX2_Row(const uint8_t* __restrict src,
                               uint8_t* __restrict d0,
                               uint8_t* __restrict d1,
                               uint8_t* __restrict d2,
                               int w)
{
    const __m256i m0_c0 = _mm256_broadcastsi128_si256(
        _mm_setr_epi8( 0, 3, 6, 9,12,15,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1));
    const __m256i m1_c0 = _mm256_broadcastsi128_si256(
        _mm_setr_epi8(-1,-1,-1,-1,-1,-1, 2, 5, 8,11,14,-1,-1,-1,-1,-1));
    const __m256i m2_c0 = _mm256_broadcastsi128_si256(
        _mm_setr_epi8(-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, 1, 4, 7,10,13));
    const __m256i m0_c1 = _mm256_broadcastsi128_si256(
        _mm_setr_epi8( 1, 4, 7,10,13,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1));
    const __m256i m1_c1 = _mm256_broadcastsi128_si256(
        _mm_setr_epi8(-1,-1,-1,-1,-1, 0, 3, 6, 9,12,15,-1,-1,-1,-1,-1));
    const __m256i m2_c1 = _mm256_broadcastsi128_si256(
        _mm_setr_epi8(-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, 2, 5, 8,11,14));
    const __m256i m0_c2 = _mm256_broadcastsi128_si256(
        _mm_setr_epi8( 2, 5, 8,11,14,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1));
    const __m256i m1_c2 = _mm256_broadcastsi128_si256(
        _mm_setr_epi8(-1,-1,-1,-1,-1, 1, 4, 7,10,13,-1,-1,-1,-1,-1,-1));
    const __m256i m2_c2 = _mm256_broadcastsi128_si256(
        _mm_setr_epi8(-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, 0, 3, 6, 9,12,15));

    int x = 0;
    for (; x + 31 < w; x += 32) {
        const uint8_t* pA = src + x * 3;       // block A: pixels x..x+15
        const uint8_t* pB = pA + 48;            // block B: pixels x+16..x+31

        // lo lane = block A, hi lane = block B
        __m256i v0 = _mm256_loadu2_m128i(
            (const __m128i*)(pB),      (const __m128i*)(pA));
        __m256i v1 = _mm256_loadu2_m128i(
            (const __m128i*)(pB + 16), (const __m128i*)(pA + 16));
        __m256i v2 = _mm256_loadu2_m128i(
            (const __m128i*)(pB + 32), (const __m128i*)(pA + 32));

        __m256i c0 = _mm256_or_si256(_mm256_or_si256(
            _mm256_shuffle_epi8(v0, m0_c0),
            _mm256_shuffle_epi8(v1, m1_c0)),
            _mm256_shuffle_epi8(v2, m2_c0));

        __m256i c1 = _mm256_or_si256(_mm256_or_si256(
            _mm256_shuffle_epi8(v0, m0_c1),
            _mm256_shuffle_epi8(v1, m1_c1)),
            _mm256_shuffle_epi8(v2, m2_c1));

        __m256i c2_r = _mm256_or_si256(_mm256_or_si256(
            _mm256_shuffle_epi8(v0, m0_c2),
            _mm256_shuffle_epi8(v1, m1_c2)),
            _mm256_shuffle_epi8(v2, m2_c2));

        _mm256_storeu_si256((__m256i*)(d0 + x), c0);
        _mm256_storeu_si256((__m256i*)(d1 + x), c1);
        _mm256_storeu_si256((__m256i*)(d2 + x), c2_r);
    }
    // SSSE3 + 标量尾部
    if (x < w) I2P_U8C3_SSSE3_Row(src + x * 3, d0 + x, d1 + x, d2 + x, w - x);
}

// ---- AVX2 + Non-Temporal Stores (NT) — 跳过缓存写入, 减少~33%总线带宽占用 ----
static void I2P_U8C3_AVX2_NT_Row(const uint8_t* __restrict src,
                                  uint8_t* __restrict d0,
                                  uint8_t* __restrict d1,
                                  uint8_t* __restrict d2,
                                  int w)
{
    const __m256i m0_c0 = _mm256_broadcastsi128_si256(
        _mm_setr_epi8( 0, 3, 6, 9,12,15,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1));
    const __m256i m1_c0 = _mm256_broadcastsi128_si256(
        _mm_setr_epi8(-1,-1,-1,-1,-1,-1, 2, 5, 8,11,14,-1,-1,-1,-1,-1));
    const __m256i m2_c0 = _mm256_broadcastsi128_si256(
        _mm_setr_epi8(-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, 1, 4, 7,10,13));
    const __m256i m0_c1 = _mm256_broadcastsi128_si256(
        _mm_setr_epi8( 1, 4, 7,10,13,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1));
    const __m256i m1_c1 = _mm256_broadcastsi128_si256(
        _mm_setr_epi8(-1,-1,-1,-1,-1, 0, 3, 6, 9,12,15,-1,-1,-1,-1,-1));
    const __m256i m2_c1 = _mm256_broadcastsi128_si256(
        _mm_setr_epi8(-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, 2, 5, 8,11,14));
    const __m256i m0_c2 = _mm256_broadcastsi128_si256(
        _mm_setr_epi8( 2, 5, 8,11,14,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1));
    const __m256i m1_c2 = _mm256_broadcastsi128_si256(
        _mm_setr_epi8(-1,-1,-1,-1,-1, 1, 4, 7,10,13,-1,-1,-1,-1,-1,-1));
    const __m256i m2_c2 = _mm256_broadcastsi128_si256(
        _mm_setr_epi8(-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, 0, 3, 6, 9,12,15));

    int x = 0;
    for (; x + 31 < w; x += 32) {
        const uint8_t* pA = src + x * 3;
        const uint8_t* pB = pA + 48;

        __m256i v0 = _mm256_loadu2_m128i(
            (const __m128i*)(pB),      (const __m128i*)(pA));
        __m256i v1 = _mm256_loadu2_m128i(
            (const __m128i*)(pB + 16), (const __m128i*)(pA + 16));
        __m256i v2 = _mm256_loadu2_m128i(
            (const __m128i*)(pB + 32), (const __m128i*)(pA + 32));

        __m256i c0 = _mm256_or_si256(_mm256_or_si256(
            _mm256_shuffle_epi8(v0, m0_c0),
            _mm256_shuffle_epi8(v1, m1_c0)),
            _mm256_shuffle_epi8(v2, m2_c0));
        __m256i c1 = _mm256_or_si256(_mm256_or_si256(
            _mm256_shuffle_epi8(v0, m0_c1),
            _mm256_shuffle_epi8(v1, m1_c1)),
            _mm256_shuffle_epi8(v2, m2_c1));
        __m256i c2_r = _mm256_or_si256(_mm256_or_si256(
            _mm256_shuffle_epi8(v0, m0_c2),
            _mm256_shuffle_epi8(v1, m1_c2)),
            _mm256_shuffle_epi8(v2, m2_c2));

        _mm256_stream_si256((__m256i*)(d0 + x), c0);
        _mm256_stream_si256((__m256i*)(d1 + x), c1);
        _mm256_stream_si256((__m256i*)(d2 + x), c2_r);
    }
    if (x < w) I2P_U8C3_SSSE3_Row(src + x * 3, d0 + x, d1 + x, d2 + x, w - x);
    _mm_sfence();
}

#else
#define HAS_SSSE3_I2P 0
static const bool g_hasAVX2 = false;
#endif

// ---- U8 三通道 Interleaved → Planar (SIMD + 线程池 + NT存储) ----
static void I2P_U8C3_Optimized(const uint8_t* __restrict src, uint8_t* __restrict dst,
                                int srcStep, int w, int h) {
    const int pixels = w * h;
    uint8_t* d0 = dst;
    uint8_t* d1 = dst + pixels;
    uint8_t* d2 = dst + pixels * 2;

#if HAS_SSSE3_I2P
    // 检查是否可以使用NT存储 (32字节对齐 + 大图)
    bool useNT = g_hasAVX2 && (pixels >= MT_PIXEL_THRESHOLD) &&
                 (reinterpret_cast<uintptr_t>(d0) % 32 == 0) &&
                 (reinterpret_cast<uintptr_t>(d1) % 32 == 0) &&
                 (reinterpret_cast<uintptr_t>(d2) % 32 == 0) &&
                 (w % 32 == 0);  // 确保每行存储对齐

    // 无padding时尝试批量处理 (src连续, 消除逐行开销)
    bool flat = (srcStep == w * 3);
#endif

    auto processRows = [&](int yStart, int yEnd) {
#if HAS_SSSE3_I2P
        if (flat && g_hasAVX2) {
            int pixStart = yStart * w;
            int pixEnd   = yEnd * w;
            const uint8_t* s = src + pixStart * 3;
            if (useNT)
                I2P_U8C3_AVX2_NT_Row(s, d0 + pixStart, d1 + pixStart, d2 + pixStart, pixEnd - pixStart);
            else
                I2P_U8C3_AVX2_Row(s, d0 + pixStart, d1 + pixStart, d2 + pixStart, pixEnd - pixStart);
            return;
        }
        for (int y = yStart; y < yEnd; ++y) {
            const uint8_t* row = src + y * srcStep;
            uint8_t* r0 = d0 + y * w;
            uint8_t* r1 = d1 + y * w;
            uint8_t* r2 = d2 + y * w;
            if (g_hasAVX2)
                I2P_U8C3_AVX2_Row(row, r0, r1, r2, w);
            else
                I2P_U8C3_SSSE3_Row(row, r0, r1, r2, w);
        }
#else
        for (int y = yStart; y < yEnd; ++y) {
            const uint8_t* row = src + y * srcStep;
            uint8_t* r0 = d0 + y * w;
            uint8_t* r1 = d1 + y * w;
            uint8_t* r2 = d2 + y * w;
            for (int x = 0; x < w; ++x) {
                r0[x] = row[x * 3];
                r1[x] = row[x * 3 + 1];
                r2[x] = row[x * 3 + 2];
            }
        }
#endif
    };

    if (pixels >= MT_PIXEL_THRESHOLD) {
        ParallelForRows(h, processRows);
    } else {
        processRows(0, h);
    }
}

// ---- U8 三通道 Planar → Interleaved (SIMD + 线程池) ----
static void P2I_U8C3_Optimized(const uint8_t* __restrict src, uint8_t* __restrict dst,
                                int w, int h) {
    const int pixels = w * h;
    const uint8_t* s0 = src;
    const uint8_t* s1 = src + pixels;
    const uint8_t* s2 = src + pixels * 2;

    auto processRows = [&](int yStart, int yEnd) {
        for (int y = yStart; y < yEnd; ++y) {
            const uint8_t* r0 = s0 + y * w;
            const uint8_t* r1 = s1 + y * w;
            const uint8_t* r2 = s2 + y * w;
            uint8_t* row = dst + y * w * 3;
#if HAS_SSSE3_I2P
            P2I_U8C3_SSSE3_Row(r0, r1, r2, row, w);
#else
            for (int x = 0; x < w; ++x) {
                row[x * 3]     = r0[x];
                row[x * 3 + 1] = r1[x];
                row[x * 3 + 2] = r2[x];
            }
#endif
        }
    };

    if (pixels >= MT_PIXEL_THRESHOLD) {
        ParallelForRows(h, processRows);
    } else {
        processRows(0, h);
    }
}

// ---- 通用路径: 带分块 + 预取 + 多线程 ----
template<typename T>
static void I2P_Generic(const T* __restrict src, T* __restrict dst, int ch,
                        int srcStepT, int w, int h) {
    const int pixels = w * h;

    auto processRows = [&](int yStart, int yEnd) {
        for (int y = yStart; y < yEnd; ++y) {
            const T* row = src + y * srcStepT;
            if (y + 1 < yEnd) PREFETCH_READ(src + (y + 1) * srcStepT);
            for (int x = 0; x < w; ++x) {
                const int pixelIdx = y * w + x;
                for (int c = 0; c < ch; ++c) {
                    dst[c * pixels + pixelIdx] = row[x * ch + c];
                }
            }
        }
    };

    if (pixels >= MT_PIXEL_THRESHOLD) {
        ParallelForRows(h, processRows);
    } else {
        processRows(0, h);
    }
}

template<typename T>
static void P2I_Generic(const T* __restrict src, T* __restrict dst, int ch,
                        int w, int h) {
    const int pixels = w * h;

    auto processRows = [&](int yStart, int yEnd) {
        for (int y = yStart; y < yEnd; ++y) {
            T* row = dst + y * w * ch;
            for (int x = 0; x < w; ++x) {
                const int pixelIdx = y * w + x;
                for (int c = 0; c < ch; ++c) {
                    row[x * ch + c] = src[c * pixels + pixelIdx];
                }
            }
        }
    };

    if (pixels >= MT_PIXEL_THRESHOLD) {
        ParallelForRows(h, processRows);
    } else {
        processRows(0, h);
    }
}

bool LayoutTransformer::InterleavedToPlanar(const ImageDescriptor& src, void* dstPtr) {
    if (!src.dataPtr || !dstPtr || src.layout != MemoryLayout::Interleaved) return false;

    const int ch     = src.getChannels();
    const int w      = src.width;
    const int h      = src.height;
    const int elemSz = ElementSize(src.format);
    const int srcStep = src.step;

    if (ch == 1) {
        // 单通道: 逐行拷贝(处理stride padding)
        const uint8_t* s = static_cast<const uint8_t*>(src.dataPtr);
        uint8_t* d = static_cast<uint8_t*>(dstPtr);
        const int rowBytes = w * elemSz;
        for (int y = 0; y < h; ++y) {
            std::memcpy(d + y * rowBytes, s + y * srcStep, rowBytes);
        }
        return true;
    }

    // [优化3] U8_C3 特化快速路径 (工业视觉最常见)
    if (elemSz == 1 && ch == 3) {
        I2P_U8C3_Optimized(static_cast<const uint8_t*>(src.dataPtr),
                           static_cast<uint8_t*>(dstPtr), srcStep, w, h);
        return true;
    }

    // 其他U8格式(C4等)
    if (elemSz == 1) {
        I2P_Generic<uint8_t>(static_cast<const uint8_t*>(src.dataPtr),
                             static_cast<uint8_t*>(dstPtr), ch, srcStep, w, h);
        return true;
    }

    // Float32 格式
    I2P_Generic<float>(reinterpret_cast<const float*>(src.dataPtr),
                       reinterpret_cast<float*>(dstPtr), ch,
                       srcStep / static_cast<int>(sizeof(float)), w, h);
    return true;
}

bool LayoutTransformer::PlanarToInterleaved(const ImageDescriptor& src, void* dstPtr) {
    if (!src.dataPtr || !dstPtr || src.layout != MemoryLayout::Planar) return false;

    const int ch     = src.getChannels();
    const int w      = src.width;
    const int h      = src.height;
    const int elemSz = ElementSize(src.format);
    const int pixels = w * h;

    if (ch == 1) {
        std::memcpy(dstPtr, src.dataPtr, static_cast<size_t>(pixels) * elemSz);
        return true;
    }

    if (elemSz == 1 && ch == 3) {
        P2I_U8C3_Optimized(static_cast<const uint8_t*>(src.dataPtr),
                           static_cast<uint8_t*>(dstPtr), w, h);
        return true;
    }

    if (elemSz == 1) {
        P2I_Generic<uint8_t>(static_cast<const uint8_t*>(src.dataPtr),
                             static_cast<uint8_t*>(dstPtr), ch, w, h);
        return true;
    }

    P2I_Generic<float>(reinterpret_cast<const float*>(src.dataPtr),
                       reinterpret_cast<float*>(dstPtr), ch, w, h);
    return true;
}

// ============================================================================
//  ImageUnifyService 实现
// ============================================================================

ImageUnifyService::ImageUnifyService() {
    std::cout << "[ImageUnifyService] 初始化完成 (maxMemory="
              << maxMemory_ / (1024 * 1024) << "MB, 内存池=ON, 预取=ON)" << std::endl;
}

ImageUnifyService::~ImageUnifyService() {
    // 先释放所有帧(使用池回收), 再清理池本身
    {
        std::lock_guard<std::mutex> lock(framesMutex_);
        for (auto& [id, frame] : frames_) freeFrameData(*frame);
        frames_.clear();
        frameOrder_.clear();
    }
    pool_.Clear();
}

// ---- 池化内存 ----

void* ImageUnifyService::poolAlloc(size_t size) {
    return pool_.Allocate(size);
}

void ImageUnifyService::poolFree(void* ptr, size_t size) {
    pool_.Deallocate(ptr, size);
}

void ImageUnifyService::freeFrameData(FrameItem& frame) {
    // Fix 2.2: Skip views with refCount > 0, defer to orphanedViews_
    for (auto& v : frame.views) {
        if (v->dataPtr) {
            if (v->refCount.load(std::memory_order_relaxed) > 0) {
                orphanedViews_.push_back(std::move(v));
            } else {
                if (v->dataSize > 0) { memoryUsage_.fetch_sub(v->dataSize, std::memory_order_relaxed); poolFree(v->dataPtr, v->dataSize); }
                v->dataPtr = nullptr;
            }
        }
    }
    frame.views.clear();
    // 释放原始数据
    if (frame.ownedData) {
        memoryUsage_.fetch_sub(frame.ownedDataSize, std::memory_order_relaxed);
        poolFree(frame.ownedData, frame.ownedDataSize);
        frame.ownedData = nullptr;
        frame.original.dataPtr = nullptr;
    }
}

void ImageUnifyService::cleanupOrphanedViews() {
    for (auto it = orphanedViews_.begin(); it != orphanedViews_.end(); ) {
        if ((*it)->refCount.load(std::memory_order_relaxed) <= 0 && (*it)->dataPtr) {
            if ((*it)->dataSize > 0) { memoryUsage_.fetch_sub((*it)->dataSize, std::memory_order_relaxed); poolFree((*it)->dataPtr, (*it)->dataSize); }
            (*it)->dataPtr = nullptr;
            it = orphanedViews_.erase(it);
        } else {
            ++it;
        }
    }
}

// ---- 帧管理 ----

uint64_t ImageUnifyService::SubmitFrame(void* data, int width, int height,
                                         PixelFormat format, MemoryLayout layout, int step) {
    if (!data || width <= 0 || height <= 0 || format == PixelFormat::Unknown) {
        setError("SubmitFrame: 参数无效");
        return 0;
    }

    ImageDescriptor tmp;
    tmp.format = format;
    int bpp = tmp.getBytesPerPixel();
    int actualStep = (step > 0) ? step : width * bpp;
    size_t dataSize = static_cast<size_t>(height) * actualStep;
    size_t planarSize = static_cast<size_t>(width) * height * bpp;

    // [优化11] 融合路径: 预测需要Planar时, 跳过interleaved memcpy,
    //          直接从用户buffer转换为planar, 单次内存遍历替代两次
    MemoryLayout predicted = predictLayout();
    bool fusedConvert = (predicted == MemoryLayout::Planar &&
                         layout == MemoryLayout::Interleaved);

    void* ownedData = nullptr;
    size_t ownedSize = 0;
    MemoryLayout storedLayout = layout;

    if (fusedConvert) {
        // 融合路径: 从用户buffer直接I→P转换, 存储为Planar
        ownedSize = planarSize;
        ownedData = poolAlloc(ownedSize);
        if (!ownedData) { setError("SubmitFrame: 内存分配失败"); return 0; }

        ImageDescriptor srcDesc;
        srcDesc.dataPtr = data;
        srcDesc.width   = width;
        srcDesc.height  = height;
        srcDesc.step    = actualStep;
        srcDesc.format  = format;
        srcDesc.layout  = MemoryLayout::Interleaved;
        LayoutTransformer::InterleavedToPlanar(srcDesc, ownedData);

        storedLayout = MemoryLayout::Planar;
    } else {
        // 常规路径: memcpy
        ownedSize = dataSize;
        ownedData = poolAlloc(ownedSize);
        if (!ownedData) { setError("SubmitFrame: 内存分配失败"); return 0; }
        std::memcpy(ownedData, data, ownedSize);
    }

    memoryUsage_.fetch_add(ownedSize, std::memory_order_relaxed);  // [优化7]

    auto frame = std::make_unique<FrameItem>();
    frame->ownedData     = ownedData;
    frame->ownedDataSize = ownedSize;
    frame->original.dataPtr  = ownedData;
    frame->original.width    = width;
    frame->original.height   = height;
    frame->original.step     = fusedConvert ? (width * bpp) : actualStep;
    frame->original.format   = format;
    frame->original.layout   = storedLayout;

    uint64_t id = nextFrameId_.fetch_add(1);
    frame->original.frameId = id;

    // Fix 3.7/3.16: Perform prefetchView BEFORE move to avoid dangling pointer
    FrameItem* rawFrame = frame.get();
    if (!fusedConvert) {
        // [优化5] 预取: prefetch must happen before ownership transfer
        MemoryLayout predicted = predictLayout();
        if (predicted != MemoryLayout::Unknown && predicted != rawFrame->original.layout) {
            findOrCreateView(*rawFrame, predicted);
        }
    }

    {
        std::lock_guard<std::mutex> lock(framesMutex_);
        frames_[id] = std::move(frame);
        frameOrder_.push_back(id);
    }

    // 被动GC — after frame is safely stored
    performMaintenance();

    return id;
}

void ImageUnifyService::RemoveFrame(uint64_t frameId) {
    std::lock_guard<std::mutex> lock(framesMutex_);
    auto it = frames_.find(frameId);
    if (it != frames_.end()) {
        freeFrameData(*(it->second));
        frames_.erase(it);
    }
    auto oit = std::find(frameOrder_.begin(), frameOrder_.end(), frameId);
    if (oit != frameOrder_.end()) frameOrder_.erase(oit);
}

bool ImageUnifyService::HasFrame(uint64_t frameId) const {
    std::lock_guard<std::mutex> lock(framesMutex_);
    return frames_.find(frameId) != frames_.end();
}

// ---- 视图获取 ----

ImageDescriptor ImageUnifyService::GetView(uint64_t frameId, MemoryLayout targetLayout) {
    // Fix #2: Fine-grained locking — find frame under global lock, then release it
    FrameItem* framePtr = nullptr;
    {
        std::lock_guard<std::mutex> lock(framesMutex_);
        auto it = frames_.find(frameId);
        if (it == frames_.end()) {
            setError("GetView: frameId=" + std::to_string(frameId) + " 不存在");
            return {};
        }
        framePtr = it->second.get();
    }

    // [优化5] 更新布局请求统计
    if (targetLayout == MemoryLayout::Planar)      planarHits_++;
    else if (targetLayout == MemoryLayout::Interleaved) interleavedHits_++;

    // Per-item lock for the potentially expensive transformation
    std::lock_guard<std::mutex> itemLock(framePtr->itemMutex);

    // Fix #4: Zero-copy path now uses a virtual view for consistent refCount semantics
    auto view = findOrCreateView(*framePtr, targetLayout);
    if (!view || !view->dataPtr) {
        setError("GetView: 布局转换失败");
        return {};
    }

    // [优化2] 引用计数 + 时间戳
    view->refCount.fetch_add(1, std::memory_order_relaxed);
    view->lastAccess = std::chrono::steady_clock::now();

    ImageDescriptor desc = framePtr->original;
    desc.dataPtr = view->dataPtr;
    desc.layout  = targetLayout;
    desc.step    = desc.width * desc.getBytesPerPixel();
    return desc;
}

void ImageUnifyService::ReleaseView(uint64_t frameId, void* viewPtr) {
    // [优化2] 引用计数递减 — 当refCount归零时视图可被LRU淘汰
    std::lock_guard<std::mutex> lock(framesMutex_);
    auto it = frames_.find(frameId);
    if (it != frames_.end()) {
        for (auto& v : it->second->views) {
            if (v->dataPtr == viewPtr) {
                v->refCount.fetch_sub(1, std::memory_order_relaxed);
                cleanupOrphanedViews();
                return;
            }
        }
    }
    // Fix 2.2: Also check orphaned views for matching pointer
    for (auto& v : orphanedViews_) {
        if (v->dataPtr == viewPtr) {
            v->refCount.fetch_sub(1, std::memory_order_relaxed);
            break;
        }
    }
    cleanupOrphanedViews();
}

// ---- 配置 ----

void ImageUnifyService::SetMaxMemory(size_t maxBytes) {
    std::lock_guard<std::mutex> lock(framesMutex_);
    maxMemory_ = maxBytes;
}

size_t ImageUnifyService::GetMemoryUsage() const {
    return memoryUsage_.load(std::memory_order_relaxed);  // [优化7] O(1)
}

void ImageUnifyService::ClearCache() {
    std::lock_guard<std::mutex> lock(framesMutex_);
    for (auto& [id, frame] : frames_) freeFrameData(*frame);
    frames_.clear();
    frameOrder_.clear();
    memoryUsage_.store(0, std::memory_order_relaxed);  // [优化7]
}

const char* ImageUnifyService::GetLastError() const {
    std::lock_guard<std::mutex> lock(errorMutex_);
    return lastError_.c_str();
}

// ---- 内部方法 ----

std::shared_ptr<ViewCacheItem> ImageUnifyService::findOrCreateView(
    FrameItem& frame, MemoryLayout targetLayout)
{
    // 查找已缓存
    for (auto& v : frame.views) {
        if (v->layout == targetLayout && v->dataPtr) {
            return v;
        }
    }

    // Fix #4: Zero-copy path — create virtual view referencing original data (no alloc, dataSize=0)
    if (frame.original.layout == targetLayout) {
        auto view = std::make_shared<ViewCacheItem>();
        view->layout   = targetLayout;
        view->dataPtr  = frame.original.dataPtr;
        view->dataSize = 0;  // 0 = not owned, skip poolFree
        frame.views.push_back(view);
        return view;
    }

    // [优化1] 从内存池分配目标缓冲区
    size_t dataSize = frame.original.getDataSize();
    void* buf = poolAlloc(dataSize);
    if (!buf) return nullptr;

    bool ok = false;
    if (frame.original.layout == MemoryLayout::Interleaved && targetLayout == MemoryLayout::Planar) {
        ok = LayoutTransformer::InterleavedToPlanar(frame.original, buf);
    } else if (frame.original.layout == MemoryLayout::Planar && targetLayout == MemoryLayout::Interleaved) {
        ok = LayoutTransformer::PlanarToInterleaved(frame.original, buf);
    }

    if (!ok) {
        poolFree(buf, dataSize);
        return nullptr;
    }

    auto view = std::make_shared<ViewCacheItem>();
    view->layout   = targetLayout;
    view->dataPtr  = buf;
    view->dataSize = dataSize;
    memoryUsage_.fetch_add(dataSize, std::memory_order_relaxed);  // [优化7]
    frame.views.push_back(view);
    return view;
}

void ImageUnifyService::performMaintenance() {
    std::lock_guard<std::mutex> lock(framesMutex_);

    // [优化2] 先尝试淘汰零引用的视图缓存
    evictZeroRefViews();

    // [优化7] 直接读原子计数器, 不再遍历
    while (memoryUsage_.load(std::memory_order_relaxed) > maxMemory_ && frameOrder_.size() > 1) {
        uint64_t oldest = frameOrder_.front();
        frameOrder_.pop_front();

        auto it = frames_.find(oldest);
        if (it != frames_.end()) {
            freeFrameData(*(it->second));  // freeFrameData内部更新memoryUsage_
            frames_.erase(it);
        }
    }
}

// [优化2] LRU淘汰: 释放refCount==0且最久未访问的视图
void ImageUnifyService::evictZeroRefViews() {
    if (memoryUsage_.load(std::memory_order_relaxed) <= maxMemory_) return;  // [优化7]

    // 收集所有可淘汰的视图 (refCount == 0)
    struct EvictCandidate {
        FrameItem* frame;
        size_t viewIdx;
        std::chrono::steady_clock::time_point lastAccess;
        size_t dataSize;
    };
    std::vector<EvictCandidate> candidates;

    for (auto& [id, frame] : frames_) {
        for (size_t i = 0; i < frame->views.size(); ++i) {
            auto& v = frame->views[i];
            if (v->refCount.load(std::memory_order_relaxed) <= 0 && v->dataPtr) {
                candidates.push_back({frame.get(), i, v->lastAccess, v->dataSize});
            }
        }
    }

    // 按最久未访问排序 (LRU: 最老的先淘汰)
    std::sort(candidates.begin(), candidates.end(),
              [](const EvictCandidate& a, const EvictCandidate& b) {
                  return a.lastAccess < b.lastAccess;
              });

    // 逐个淘汰直到内存降到阈值以下
    for (auto& c : candidates) {
        if (memoryUsage_.load(std::memory_order_relaxed) <= maxMemory_) break;  // [优化7]
        auto& v = c.frame->views[c.viewIdx];
        if (v->dataPtr) {
            memoryUsage_.fetch_sub(v->dataSize, std::memory_order_relaxed);  // [优化7]
            poolFree(v->dataPtr, v->dataSize);
            v->dataPtr = nullptr;
        }
    }
}

// [优化5] 预测下次最可能请求的布局
MemoryLayout ImageUnifyService::predictLayout() const {
    int p = planarHits_.load(std::memory_order_relaxed);
    int i = interleavedHits_.load(std::memory_order_relaxed);
    int total = p + i;
    if (total < 5) return MemoryLayout::Unknown;  // 样本不足, 不预测
    // 超过70%的请求是某种布局则预测它
    if (p * 10 > total * 7) return MemoryLayout::Planar;
    if (i * 10 > total * 7) return MemoryLayout::Interleaved;
    return MemoryLayout::Unknown;
}

// [优化5] 提交帧后立即预转换为预测布局
void ImageUnifyService::prefetchView(FrameItem& frame) {
    MemoryLayout predicted = predictLayout();
    if (predicted == MemoryLayout::Unknown) return;
    if (predicted == frame.original.layout) return;  // 原始布局就是目标, 零拷贝, 无需预取

    // 在锁内预转换
    std::lock_guard<std::mutex> lock(framesMutex_);
    findOrCreateView(frame, predicted);
}
