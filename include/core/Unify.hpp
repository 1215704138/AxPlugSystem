#pragma once

// ============================================================================
//  Unify.hpp - 图像统一服务便捷层
//
//  功能: 将 IImageUnifyService 的视图直接转为 OpenCV / Halcon / Qt 对象
//  用法: #include "Unify.hpp", 然后用 Unify:: 命名空间下的自由函数
//
//  使用示例:
//    auto* svc = ...;  // IImageUnifyService*
//
//    // 1. 提交帧
//    ScopedFrame frame(svc, imgData, 640, 480, PixelFormat::U8_C3);
//
//    // 2. 获取OpenCV格式 (自动Interleaved, 零拷贝)
//    cv::Mat mat = Unify::ToCvMat(svc, frame.Id());
//
//    // 3. 获取Halcon格式 (自动转Planar)
//    // HalconCpp::HImage himg = Unify::ToHImage(svc, frame.Id());
//
//    // 4. 获取Qt显示格式
//    // QImage qimg = Unify::ToQImage(svc, frame.Id());
// ============================================================================

#include "IImageUnifyService.h"

// 条件包含第三方库
#ifdef HAS_OPENCV
#include <opencv2/core.hpp>
#endif

#ifdef HAS_HALCON
#include <HalconCpp.h>
#endif

#ifdef QT_CORE_LIB
#include <QImage>
#endif

namespace Unify {

// ============================================================================
//  OpenCV 便捷函数
// ============================================================================
#ifdef HAS_OPENCV

// PixelFormat → OpenCV type 映射
inline int ToCvType(PixelFormat fmt) {
    switch (fmt) {
        case PixelFormat::U8_C1:      return CV_8UC1;
        case PixelFormat::U8_C3:      return CV_8UC3;
        case PixelFormat::U8_C4:      return CV_8UC4;
        case PixelFormat::Float32_C1: return CV_32FC1;
        case PixelFormat::Float32_C3: return CV_32FC3;
        case PixelFormat::Float32_C4: return CV_32FC4;
        default: return -1;
    }
}

// 从 ImageDescriptor 构造 cv::Mat (零拷贝, Interleaved布局)
inline cv::Mat ToCvMat(const ImageDescriptor& desc) {
    if (!desc.dataPtr || desc.layout != MemoryLayout::Interleaved) return {};
    int t = ToCvType(desc.format);
    return (t >= 0) ? cv::Mat(desc.height, desc.width, t, desc.dataPtr, desc.step) : cv::Mat{};
}

// 从 ScopedView 构造 cv::Mat — 一行代码:
//   ScopedView view(svc, fid, MemoryLayout::Interleaved);
//   cv::Mat mat = Unify::ToCvMat(view);
inline cv::Mat ToCvMat(const ScopedView& view) { return ToCvMat(view.Desc()); }

// 从服务+帧ID构造 (兼容旧接口)
inline cv::Mat ToCvMat(IImageUnifyService* svc, uint64_t frameId) {
    ImageDescriptor desc = svc->GetView(frameId, MemoryLayout::Interleaved);
    return ToCvMat(desc);
}

// 从 cv::Mat 提交帧到服务, 返回 ScopedFrame
inline ScopedFrame SubmitCvMat(IImageUnifyService* svc, const cv::Mat& mat) {
    PixelFormat fmt = PixelFormat::Unknown;
    switch (mat.type()) {
        case CV_8UC1:  fmt = PixelFormat::U8_C1;      break;
        case CV_8UC3:  fmt = PixelFormat::U8_C3;      break;
        case CV_8UC4:  fmt = PixelFormat::U8_C4;      break;
        case CV_32FC1: fmt = PixelFormat::Float32_C1;  break;
        case CV_32FC3: fmt = PixelFormat::Float32_C3;  break;
        case CV_32FC4: fmt = PixelFormat::Float32_C4;  break;
        default: return ScopedFrame(nullptr, nullptr, 0, 0, PixelFormat::Unknown);
    }
    return ScopedFrame(svc, mat.data, mat.cols, mat.rows, fmt,
                       MemoryLayout::Interleaved, static_cast<int>(mat.step));
}

#endif // HAS_OPENCV

// ============================================================================
//  Halcon 便捷函数
// ============================================================================
#ifdef HAS_HALCON

// 从 ImageDescriptor 构造 HImage (Planar布局)
inline HalconCpp::HImage ToHImage(const ImageDescriptor& desc) {
    if (!desc.dataPtr || desc.layout != MemoryLayout::Planar) return {};
    int ch = desc.getChannels();
    int w = desc.width, h = desc.height;
    uint8_t* data = static_cast<uint8_t*>(desc.dataPtr);
    HalconCpp::HImage himg;
    if (ch == 1) {
        himg.GenImage1("byte", w, h, data);
    } else if (ch == 3) {
        size_t ps = static_cast<size_t>(w) * h;
        himg.GenImage3("byte", w, h, data, data + ps, data + ps * 2);
    }
    return himg;
}

// 从 ScopedView 构造 HImage — 一行代码:
//   ScopedView planar(svc, fid, MemoryLayout::Planar);
//   auto himg = Unify::ToHImage(planar);
inline HalconCpp::HImage ToHImage(const ScopedView& view) { return ToHImage(view.Desc()); }

// 从服务+帧ID构造 (兼容旧接口)
inline HalconCpp::HImage ToHImage(IImageUnifyService* svc, uint64_t frameId) {
    ImageDescriptor desc = svc->GetView(frameId, MemoryLayout::Planar);
    return ToHImage(desc);
}

#endif // HAS_HALCON

// ============================================================================
//  Qt 便捷函数
// ============================================================================
#ifdef QT_CORE_LIB

// 获取 QImage — Interleaved视图, 适合直接绘制到UI
inline QImage ToQImage(IImageUnifyService* svc, uint64_t frameId) {
    ImageDescriptor desc = svc->GetView(frameId, MemoryLayout::Interleaved);
    if (!desc.dataPtr) return {};

    QImage::Format qfmt = QImage::Format_Invalid;
    switch (desc.format) {
        case PixelFormat::U8_C1: qfmt = QImage::Format_Grayscale8; break;
        case PixelFormat::U8_C3: qfmt = QImage::Format_RGB888;     break;
        case PixelFormat::U8_C4: qfmt = QImage::Format_RGBA8888;   break;
        default: return {};
    }
    return QImage(static_cast<const uchar*>(desc.dataPtr),
                  desc.width, desc.height, desc.step, qfmt);
}

#endif // QT_CORE_LIB

} // namespace Unify
