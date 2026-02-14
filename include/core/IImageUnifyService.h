#pragma once
#include "../AxPlug/IAxObject.h"
#include <stdint.h>

// ============================================================================
//  IImageUnifyService - 工业视觉图像统一服务
//
//  设计目标: 用最少的代码完成图像在不同视觉库间的格式转换与缓存
//
//  典型用法 (3步):
//    1. uint64_t fid = svc->SubmitFrame(data, w, h, PixelFormat::U8_C3);
//    2. auto view = svc->GetView(fid, MemoryLayout::Planar);  // 给Halcon
//    3. svc->ReleaseView(fid, view.dataPtr);                  // 或用ScopedView自动释放
//
//  更简单 - 使用RAII:
//    ScopedFrame frame(svc, data, w, h, PixelFormat::U8_C3);
//    ScopedView  planar(svc, frame.Id(), MemoryLayout::Planar);
//    // 离开作用域自动释放, 无需手动管理
// ============================================================================

// 像素格式
enum class PixelFormat : uint8_t {
    Unknown      = 0,
    U8_C1        = 1,    // 8位单通道 (灰度)
    U8_C3        = 3,    // 8位三通道 (BGR/RGB)
    U8_C4        = 4,    // 8位四通道 (BGRA/RGBA)
    Float32_C1   = 32,   // 32位浮点单通道
    Float32_C3   = 96,   // 32位浮点三通道
    Float32_C4   = 128   // 32位浮点四通道
};

// 内存布局
enum class MemoryLayout : uint8_t {
    Unknown      = 0,
    Interleaved  = 1,    // 交错 [BGR][BGR][BGR]... (OpenCV/Qt)
    Planar       = 2     // 平面 [BBB...][GGG...][RRR...] (Halcon)
};

// 图像描述符 - 图像数据的完整描述
struct ImageDescriptor {
    uint64_t     frameId;    // 帧ID (由SubmitFrame分配)
    void*        dataPtr;    // 数据首地址
    int          width;      // 宽度 (像素)
    int          height;     // 高度 (像素)
    int          step;       // 行步长 (字节), 0表示紧凑排列
    PixelFormat  format;     // 像素格式
    MemoryLayout layout;     // 内存布局

    ImageDescriptor()
        : frameId(0), dataPtr(nullptr), width(0), height(0),
          step(0), format(PixelFormat::Unknown), layout(MemoryLayout::Unknown) {}

    // 便捷构造
    static ImageDescriptor Create(void* data, int w, int h, PixelFormat fmt,
                                  MemoryLayout lay = MemoryLayout::Interleaved,
                                  int rowStep = 0) {
        ImageDescriptor d;
        d.dataPtr = data;
        d.width   = w;
        d.height  = h;
        d.format  = fmt;
        d.layout  = lay;
        d.step    = (rowStep > 0) ? rowStep : w * d.getBytesPerPixel();
        return d;
    }

    int getChannels() const {
        switch (format) {
            case PixelFormat::U8_C1: case PixelFormat::Float32_C1: return 1;
            case PixelFormat::U8_C3: case PixelFormat::Float32_C3: return 3;
            case PixelFormat::U8_C4: case PixelFormat::Float32_C4: return 4;
            default: return 0;
        }
    }

    int getBytesPerPixel() const {
        switch (format) {
            case PixelFormat::U8_C1:      return 1;
            case PixelFormat::U8_C3:      return 3;
            case PixelFormat::U8_C4:      return 4;
            case PixelFormat::Float32_C1: return 4;
            case PixelFormat::Float32_C3: return 12;
            case PixelFormat::Float32_C4: return 16;
            default: return 0;
        }
    }

    size_t getDataSize() const {
        return static_cast<size_t>(height) * (step > 0 ? step : width * getBytesPerPixel());
    }

    bool isValid() const {
        return dataPtr != nullptr && width > 0 && height > 0 &&
               format != PixelFormat::Unknown && layout != MemoryLayout::Unknown &&
               step >= width * getBytesPerPixel();
    }

    // ---- Planar通道便捷访问 ----
    // 单个通道平面的字节数 (仅Planar布局有意义)
    size_t getPlaneSize() const {
        int ch = getChannels();
        if (ch <= 0) return 0;
        int elemSize = getBytesPerPixel() / ch;  // 单元素字节数
        return static_cast<size_t>(width) * height * elemSize;
    }

    // 获取Planar布局中第channelIndex个通道的指针 (0-based)
    // 仅当 layout==Planar 且 channelIndex < getChannels() 时有效, 否则返回nullptr
    void* getChannelPtr(int channelIndex) const {
        if (!dataPtr || layout != MemoryLayout::Planar) return nullptr;
        int ch = getChannels();
        if (channelIndex < 0 || channelIndex >= ch) return nullptr;
        return static_cast<uint8_t*>(dataPtr) + channelIndex * getPlaneSize();
    }

    // 类型安全版本
    template<typename T = uint8_t>
    T* channelData(int channelIndex) const {
        return static_cast<T*>(getChannelPtr(channelIndex));
    }

    // ---- 语义化通道访问 (仅Planar布局有效, 否则返回nullptr) ----
    //   auto r = view.R();       // uint8_t* 红色通道
    //   auto g = view.G();       // uint8_t* 绿色通道
    //   auto b = view.B();       // uint8_t* 蓝色通道
    //   auto ch = view.Channel(2); // 等价于 B()
    template<typename T = uint8_t> T* R() const { return channelData<T>(0); }
    template<typename T = uint8_t> T* G() const { return channelData<T>(1); }
    template<typename T = uint8_t> T* B() const { return channelData<T>(2); }
    template<typename T = uint8_t> T* A() const { return channelData<T>(3); }
    template<typename T = uint8_t> T* Channel(int i) const { return channelData<T>(i); }
};

// ============================================================================
//  服务接口 — 只暴露用户真正需要的方法
// ============================================================================
class IImageUnifyService : public IAxObject {
    AX_INTERFACE(IImageUnifyService)
public:
    // ---- 帧管理 ----
    // 提交原始图像, 返回帧ID (服务内部拷贝数据, 调用方可自由释放原始buffer)
    virtual uint64_t SubmitFrame(void* data, int width, int height,
                                 PixelFormat format,
                                 MemoryLayout layout = MemoryLayout::Interleaved,
                                 int step = 0) = 0;

    // 移除帧及其所有缓存视图
    virtual void RemoveFrame(uint64_t frameId) = 0;

    // 查询帧是否存在 (帧可能被自动淘汰)
    virtual bool HasFrame(uint64_t frameId) const = 0;

    // ---- 视图获取 ----
    // 获取指定布局的视图 (自动转换, 自动缓存)
    // 返回的 ImageDescriptor 中 dataPtr 指向服务管理的内存, 用完需 ReleaseView
    virtual ImageDescriptor GetView(uint64_t frameId,
                                    MemoryLayout targetLayout) = 0;

    // 释放视图 (引用计数-1, 计数归零后缓存可被自动回收)
    virtual void ReleaseView(uint64_t frameId, void* viewPtr) = 0;

    // ---- 配置 (全部可选, 有合理默认值) ----
    virtual void   SetMaxMemory(size_t maxBytes) = 0;   // 默认256MB
    virtual size_t GetMemoryUsage() const = 0;
    virtual void   ClearCache() = 0;

    // ---- 错误 ----
    virtual const char* GetLastError() const = 0;

protected:
    virtual ~IImageUnifyService() = default;
};

#define IMAGE_UNIFY_SERVICE_ID "core.imageunify.service"

// ============================================================================
//  RAII 辅助类 — 包含在接口头文件中, 零额外依赖
// ============================================================================

// ScopedFrame: 作用域内自动管理帧生命周期
//   构造时 SubmitFrame, 析构时 RemoveFrame
class ScopedFrame {
    IImageUnifyService* svc_;
    uint64_t id_;
public:
    ScopedFrame(IImageUnifyService* svc, void* data, int w, int h,
                PixelFormat fmt,
                MemoryLayout layout = MemoryLayout::Interleaved,
                int step = 0)
        : svc_(svc), id_(0)
    {
        if (svc_) id_ = svc_->SubmitFrame(data, w, h, fmt, layout, step);
    }

    ~ScopedFrame() { if (svc_ && id_) svc_->RemoveFrame(id_); }

    uint64_t Id() const { return id_; }
    bool     Ok() const { return id_ != 0; }

    // 禁止拷贝, 允许移动
    ScopedFrame(const ScopedFrame&) = delete;
    ScopedFrame& operator=(const ScopedFrame&) = delete;
    ScopedFrame(ScopedFrame&& o) noexcept : svc_(o.svc_), id_(o.id_) { o.id_ = 0; }
    ScopedFrame& operator=(ScopedFrame&& o) noexcept {
        if (this != &o) { if (svc_ && id_) svc_->RemoveFrame(id_); svc_ = o.svc_; id_ = o.id_; o.id_ = 0; }
        return *this;
    }
};

// ScopedView: 作用域内自动管理视图生命周期
//   构造时 GetView, 析构时 ReleaseView
class ScopedView {
    IImageUnifyService* svc_;
    ImageDescriptor     desc_;
public:
    ScopedView(IImageUnifyService* svc, uint64_t frameId, MemoryLayout layout)
        : svc_(svc)
    {
        if (svc_) desc_ = svc_->GetView(frameId, layout);
    }

    ~ScopedView() { Release(); }

    void Release() {
        if (svc_ && desc_.dataPtr) {
            svc_->ReleaseView(desc_.frameId, desc_.dataPtr);
            desc_.dataPtr = nullptr;
        }
    }

    // 访问
    const ImageDescriptor& Desc()   const { return desc_; }
    void*                  Data()   const { return desc_.dataPtr; }
    int                    Width()  const { return desc_.width; }
    int                    Height() const { return desc_.height; }
    int                    Step()   const { return desc_.step; }
    bool                   Ok()     const { return desc_.dataPtr != nullptr; }

    // ---- 语义化通道访问 (Planar布局专用) ----
    template<typename T = uint8_t> T* R() const { return desc_.R<T>(); }
    template<typename T = uint8_t> T* G() const { return desc_.G<T>(); }
    template<typename T = uint8_t> T* B() const { return desc_.B<T>(); }
    template<typename T = uint8_t> T* A() const { return desc_.A<T>(); }
    template<typename T = uint8_t> T* Channel(int i) const { return desc_.Channel<T>(i); }

    // 禁止拷贝, 允许移动
    ScopedView(const ScopedView&) = delete;
    ScopedView& operator=(const ScopedView&) = delete;
    ScopedView(ScopedView&& o) noexcept : svc_(o.svc_), desc_(o.desc_) { o.desc_.dataPtr = nullptr; }
    ScopedView& operator=(ScopedView&& o) noexcept {
        if (this != &o) { Release(); svc_ = o.svc_; desc_ = o.desc_; o.desc_.dataPtr = nullptr; }
        return *this;
    }
};

// ============================================================================
//  格式工具函数
// ============================================================================
namespace ImageFormatUtils {
    inline const char* GetPixelFormatString(PixelFormat format) {
        switch (format) {
            case PixelFormat::U8_C1:      return "U8_C1";
            case PixelFormat::U8_C3:      return "U8_C3";
            case PixelFormat::U8_C4:      return "U8_C4";
            case PixelFormat::Float32_C1: return "Float32_C1";
            case PixelFormat::Float32_C3: return "Float32_C3";
            case PixelFormat::Float32_C4: return "Float32_C4";
            default: return "Unknown";
        }
    }

    inline const char* GetMemoryLayoutString(MemoryLayout layout) {
        switch (layout) {
            case MemoryLayout::Interleaved: return "Interleaved";
            case MemoryLayout::Planar:      return "Planar";
            default: return "Unknown";
        }
    }

    inline size_t CalculateImageSize(int width, int height, PixelFormat format) {
        ImageDescriptor tmp;
        tmp.format = format;
        return static_cast<size_t>(width) * height * tmp.getBytesPerPixel();
    }
}
