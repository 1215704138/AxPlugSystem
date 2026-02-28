// ============================================================================
//  ImageUnifyService 测试 Demo
//
//  演示工业视觉场景下的典型用法:
//    - 提交相机采集的图像帧
//    - 获取不同布局的视图 (Interleaved给OpenCV, Planar给Halcon)
//    - RAII自动生命周期管理
//    - 高性能零拷贝访问
//    - OpenCV缺陷检测 + 测量示例
// ============================================================================

#ifdef _WIN32
#include <windows.h>
#endif

#include <string>
#include "AxPlug/AxPlug.h"
#include "core/IImageUnifyService.h"

#ifdef HAS_OPENCV
#include "core/Unify.hpp"
#include <opencv2/imgproc.hpp>
#endif

#ifdef HAS_HALCON
#ifndef HAS_OPENCV
#include "core/Unify.hpp"
#endif
#include <HalconCpp.h>
#endif

#include <iostream>
#include <vector>
#include <chrono>
#include <cstring>
#include <cmath>
#include <cassert>
#include <filesystem>
#include <fstream>

// ============================================================================
//  辅助: 生成模拟工业相机图像 (带缺陷的灰度图)
// ============================================================================
static std::vector<uint8_t> GenerateTestImage(int width, int height, int channels) {
    std::vector<uint8_t> data(width * height * channels, 128);

    // 画一个亮色矩形 (模拟工件区域)
    for (int y = height / 4; y < height * 3 / 4; ++y) {
        for (int x = width / 4; x < width * 3 / 4; ++x) {
            int idx = (y * width + x) * channels;
            for (int c = 0; c < channels; ++c)
                data[idx + c] = 200;
        }
    }

    // 画几个暗点 (模拟缺陷)
    auto putDefect = [&](int cx, int cy, int radius) {
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (dx * dx + dy * dy <= radius * radius) {
                    int px = cx + dx, py = cy + dy;
                    if (px >= 0 && px < width && py >= 0 && py < height) {
                        int idx = (py * width + px) * channels;
                        for (int c = 0; c < channels; ++c)
                            data[idx + c] = 50;
                    }
                }
            }
        }
    };

    putDefect(width / 3, height / 2, 5);
    putDefect(width * 2 / 3, height / 2, 3);
    putDefect(width / 2, height / 3, 4);

    return data;
}

// ============================================================================
//  辅助: 验证Planar布局转换正确性
// ============================================================================
static bool VerifyPlanarConversion(const uint8_t* interleaved,
                                    const uint8_t* planar,
                                    int width, int height, int channels) {
    int pixels = width * height;
    for (int i = 0; i < pixels; ++i) {
        for (int c = 0; c < channels; ++c) {
            uint8_t expected = interleaved[i * channels + c];
            uint8_t actual   = planar[c * pixels + i];
            if (expected != actual) {
                std::cerr << "  Planar验证失败: pixel=" << i << " ch=" << c
                          << " expected=" << (int)expected << " actual=" << (int)actual << std::endl;
                return false;
            }
        }
    }
    return true;
}

// ============================================================================
//  测试1: 基本API流程 — SubmitFrame / GetView / ReleaseView
// ============================================================================
static void Test_BasicAPI(IImageUnifyService* svc) {
    std::cout << "\n===== 测试1: 基本API流程 =====" << std::endl;

    const int W = 640, H = 480, CH = 3;
    auto imgData = GenerateTestImage(W, H, CH);

    // 1. 提交帧
    uint64_t fid = svc->SubmitFrame(imgData.data(), W, H, PixelFormat::U8_C3);
    assert(fid != 0);
    std::cout << "  SubmitFrame 成功, frameId=" << fid << std::endl;

    // 2. 获取Interleaved视图 (零拷贝, 与原始布局相同)
    ImageDescriptor viewI = svc->GetView(fid, MemoryLayout::Interleaved);
    assert(viewI.isValid());
    assert(viewI.layout == MemoryLayout::Interleaved);
    std::cout << "  GetView(Interleaved) 成功, "
              << viewI.width << "x" << viewI.height
              << " " << ImageFormatUtils::GetPixelFormatString(viewI.format)
              << std::endl;

    // 3. 获取Planar视图 (自动转换)
    ImageDescriptor viewP = svc->GetView(fid, MemoryLayout::Planar);
    assert(viewP.isValid());
    assert(viewP.layout == MemoryLayout::Planar);
    std::cout << "  GetView(Planar) 成功, 自动转换完成" << std::endl;

    // 4. 验证Planar数据正确性
    bool correct = VerifyPlanarConversion(
        static_cast<const uint8_t*>(viewI.dataPtr),
        static_cast<const uint8_t*>(viewP.dataPtr),
        W, H, CH);
    std::cout << "  Planar数据验证: " << (correct ? "通过 ✓" : "失败 ✗") << std::endl;
    assert(correct);

    // 5. 释放
    svc->ReleaseView(fid, viewP.dataPtr);
    svc->ReleaseView(fid, viewI.dataPtr);
    svc->RemoveFrame(fid);

    std::cout << "  RemoveFrame 完成" << std::endl;
}

// ============================================================================
//  测试2: RAII生命周期管理 — ScopedFrame + ScopedView
// ============================================================================
static void Test_RAII(IImageUnifyService* svc) {
    std::cout << "\n===== 测试2: RAII 自动生命周期 =====" << std::endl;

    const int W = 320, H = 240, CH = 1;
    auto imgData = GenerateTestImage(W, H, CH);

    {
        // ScopedFrame: 离开作用域自动 RemoveFrame
        ScopedFrame frame(svc, imgData.data(), W, H, PixelFormat::U8_C1);
        assert(frame.Ok());
        std::cout << "  ScopedFrame 创建, id=" << frame.Id() << std::endl;

        {
            // ScopedView: 离开作用域自动 ReleaseView
            ScopedView view(svc, frame.Id(), MemoryLayout::Interleaved);
            assert(view.Ok());
            std::cout << "  ScopedView(Interleaved) "
                      << view.Width() << "x" << view.Height()
                      << " data=" << (view.Data() ? "有效" : "空") << std::endl;

            // 验证数据一致性
            assert(std::memcmp(view.Data(), imgData.data(), W * H) == 0);
            std::cout << "  数据一致性验证: 通过 ✓" << std::endl;
        }
        std::cout << "  ScopedView 已自动释放" << std::endl;
    }
    std::cout << "  ScopedFrame 已自动移除" << std::endl;
}

// ============================================================================
//  测试3: 多帧连续采集 + 自动内存回收
// ============================================================================
static void Test_MultiFrame(IImageUnifyService* svc) {
    std::cout << "\n===== 测试3: 多帧连续采集 + 内存管理 =====" << std::endl;

    // 限制内存为10MB, 模拟连续采集
    svc->SetMaxMemory(10 * 1024 * 1024);

    const int W = 640, H = 480, CH = 3;
    const int FRAME_COUNT = 20;

    std::vector<uint64_t> frameIds;

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < FRAME_COUNT; ++i) {
        auto imgData = GenerateTestImage(W, H, CH);
        uint64_t fid = svc->SubmitFrame(imgData.data(), W, H, PixelFormat::U8_C3);
        if (fid != 0) frameIds.push_back(fid);
    }

    auto end = std::chrono::high_resolution_clock::now();
    double ms = std::chrono::duration<double, std::milli>(end - start).count();

    std::cout << "  提交 " << FRAME_COUNT << " 帧, 耗时 " << ms << " ms" << std::endl;
    std::cout << "  内存使用: " << svc->GetMemoryUsage() / 1024 << " KB" << std::endl;
    std::cout << "  存活帧数: " << frameIds.size()
              << " (旧帧已被自动回收以维持内存限制)" << std::endl;

    // 获取最后一帧的视图
    if (!frameIds.empty()) {
        uint64_t lastFid = frameIds.back();
        ScopedView view(svc, lastFid, MemoryLayout::Planar);
        if (view.Ok()) {
            std::cout << "  最后一帧Planar视图获取成功: "
                      << view.Width() << "x" << view.Height() << std::endl;
        }
    }

    // 清理
    svc->ClearCache();
    std::cout << "  ClearCache 后内存: " << svc->GetMemoryUsage() << " bytes" << std::endl;

    // 恢复默认
    svc->SetMaxMemory(256 * 1024 * 1024);
}

// ============================================================================
//  测试4: 性能基准 — 布局转换吞吐量
// ============================================================================
static void Test_Performance(IImageUnifyService* svc) {
    std::cout << "\n===== 测试4: 性能基准 =====" << std::endl;

    const int W = 1920, H = 1080, CH = 3;
    auto imgData = GenerateTestImage(W, H, CH);

    // 测试单帧转换性能
    uint64_t fid = svc->SubmitFrame(imgData.data(), W, H, PixelFormat::U8_C3);

    const int ITERATIONS = 100;
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < ITERATIONS; ++i) {
        ImageDescriptor view = svc->GetView(fid, MemoryLayout::Planar);
        svc->ReleaseView(fid, view.dataPtr);
    }

    auto end = std::chrono::high_resolution_clock::now();
    double totalMs = std::chrono::duration<double, std::milli>(end - start).count();

    double perFrameMs = totalMs / ITERATIONS;
    double fps = 1000.0 / perFrameMs;
    double mbPerSec = (W * H * CH / 1024.0 / 1024.0) * fps;

    std::cout << "  分辨率: " << W << "x" << H << " (" << CH << "通道)" << std::endl;
    std::cout << "  Interleaved→Planar 转换:" << std::endl;
    std::cout << "    " << ITERATIONS << "次迭代, 总耗时 " << totalMs << " ms" << std::endl;
    std::cout << "    单次: " << perFrameMs << " ms" << std::endl;
    std::cout << "    吞吐: " << fps << " fps, " << mbPerSec << " MB/s" << std::endl;

    // 测试缓存命中 (第二次获取同布局应该直接返回缓存)
    start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < ITERATIONS; ++i) {
        ImageDescriptor view = svc->GetView(fid, MemoryLayout::Planar);
        // 不release, 让缓存保持
    }

    end = std::chrono::high_resolution_clock::now();
    double cachedMs = std::chrono::duration<double, std::milli>(end - start).count();

    std::cout << "  缓存命中:" << std::endl;
    std::cout << "    " << ITERATIONS << "次迭代, 总耗时 " << cachedMs << " ms" << std::endl;
    std::cout << "    单次: " << cachedMs / ITERATIONS << " ms" << std::endl;

    svc->RemoveFrame(fid);
}

// ============================================================================
//  测试5: OpenCV集成 — 缺陷检测 + 测量 Demo
// ============================================================================
#ifdef HAS_OPENCV
static void Test_OpenCVIntegration(IImageUnifyService* svc) {
    std::cout << "\n===== 测试5: OpenCV集成 — 缺陷检测 =====" << std::endl;

    const int W = 640, H = 480, CH = 1;
    auto imgData = GenerateTestImage(W, H, CH);

    // 用RAII提交帧
    ScopedFrame frame(svc, imgData.data(), W, H, PixelFormat::U8_C1);
    assert(frame.Ok());

    // 一行获取cv::Mat
    cv::Mat mat = Unify::ToCvMat(svc, frame.Id());
    std::cout << "  cv::Mat: " << mat.cols << "x" << mat.rows
              << " type=" << mat.type() << std::endl;

    // 简单缺陷检测: 二值化 + 连通域
    cv::Mat binary;
    cv::threshold(mat, binary, 100, 255, cv::THRESH_BINARY_INV);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    std::cout << "  检测到 " << contours.size() << " 个缺陷区域:" << std::endl;
    for (size_t i = 0; i < contours.size(); ++i) {
        cv::Rect bbox = cv::boundingRect(contours[i]);
        double area = cv::contourArea(contours[i]);
        if (area > 5) {
            std::cout << "    缺陷#" << i << ": 位置=(" << bbox.x << "," << bbox.y
                      << ") 尺寸=" << bbox.width << "x" << bbox.height
                      << " 面积=" << area << "px²" << std::endl;
        }
    }

    // 从cv::Mat提交回服务 (反向集成)
    auto frame2 = Unify::SubmitCvMat(svc, binary);
    assert(frame2.Ok());
    std::cout << "  cv::Mat → SubmitFrame 成功, id=" << frame2.Id() << std::endl;

    // 获取Planar视图 (模拟传给Halcon做进一步处理)
    ScopedView planarView(svc, frame2.Id(), MemoryLayout::Planar);
    std::cout << "  二值图Planar视图: " << (planarView.Ok() ? "成功 ✓" : "失败 ✗") << std::endl;
}
#endif

// ============================================================================
//  测试6: ImageDescriptor::Create 便捷构造
// ============================================================================
static void Test_DescriptorCreate(IImageUnifyService* svc) {
    std::cout << "\n===== 测试6: ImageDescriptor::Create =====" << std::endl;

    const int W = 100, H = 100;
    std::vector<uint8_t> data(W * H * 3, 128);

    auto desc = ImageDescriptor::Create(data.data(), W, H, PixelFormat::U8_C3);
    assert(desc.width == W);
    assert(desc.height == H);
    assert(desc.format == PixelFormat::U8_C3);
    assert(desc.layout == MemoryLayout::Interleaved);
    assert(desc.step == W * 3);
    assert(desc.isValid());

    std::cout << "  Create: " << desc.width << "x" << desc.height
              << " fmt=" << ImageFormatUtils::GetPixelFormatString(desc.format)
              << " layout=" << ImageFormatUtils::GetMemoryLayoutString(desc.layout)
              << " step=" << desc.step << std::endl;
    std::cout << "  isValid: " << (desc.isValid() ? "是 ✓" : "否 ✗") << std::endl;
}

// ============================================================================
//  辅助: 读取BMP文件头信息
// ============================================================================
#pragma pack(push, 1)
struct BMPFileHeader {
    uint16_t signature;
    uint32_t file_size;
    uint16_t reserved1;
    uint16_t reserved2;
    uint32_t data_offset;
};

struct BMPInfoHeader {
    uint32_t header_size;
    int32_t  width;
    int32_t  height;
    uint16_t planes;
    uint16_t bits_per_pixel;
    uint32_t compression;
    uint32_t image_size;
    int32_t  x_pixels_per_meter;
    int32_t  y_pixels_per_meter;
    uint32_t colors_used;
    uint32_t colors_important;
};
#pragma pack(pop)

// ============================================================================
//  辅助: 读取BMP文件为RGB数据
// ============================================================================
static std::vector<uint8_t> LoadBMPAsRGB(const std::string& filepath, int& width, int& height) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "无法打开文件: " << filepath << std::endl;
        return {};
    }

    BMPFileHeader file_header;
    BMPInfoHeader info_header;
    
    file.read(reinterpret_cast<char*>(&file_header), sizeof(file_header));
    file.read(reinterpret_cast<char*>(&info_header), sizeof(info_header));
    
    // 检查BMP格式
    if (file_header.signature != 0x4D42) { // "BM"
        std::cerr << "不是有效的BMP文件: " << filepath << std::endl;
        return {};
    }
    
    if (info_header.bits_per_pixel != 24) {
        std::cerr << "只支持24位BMP文件: " << filepath << " (bits=" << info_header.bits_per_pixel << ")" << std::endl;
        return {};
    }
    
    width = info_header.width;
    height = std::abs(info_header.height);
    
    // 计算行字节数 (4字节对齐)
    int row_stride = ((width * 3 + 3) / 4) * 4;
    
    // 分配RGB数据
    std::vector<uint8_t> rgb_data(width * height * 3);
    
    // BMP行存储顺序: 从下到上或从上到下
    bool top_down = info_header.height < 0;
    
    file.seekg(file_header.data_offset);
    
    // 读取像素数据并转换为RGB格式
    for (int row = 0; row < height; ++row) {
        int src_row = top_down ? row : (height - 1 - row);
        file.seekg(file_header.data_offset + src_row * row_stride);
        
        for (int col = 0; col < width; ++col) {
            uint8_t b, g, r;
            file.read(reinterpret_cast<char*>(&b), 1);
            file.read(reinterpret_cast<char*>(&g), 1);
            file.read(reinterpret_cast<char*>(&r), 1);
            
            int dst_idx = (row * width + col) * 3;
            rgb_data[dst_idx + 0] = r; // 转换为RGB顺序
            rgb_data[dst_idx + 1] = g;
            rgb_data[dst_idx + 2] = b;
        }
    }
    
    return rgb_data;
}

// ============================================================================
//  辅助: 常规内存搬运方式转换为Planar格式
// ============================================================================
static void ConvertToPlanarManual(const uint8_t* interleaved_rgb, uint8_t* planar_rgb, 
                                  int width, int height) {
    int pixels = width * height;
    
    for (int i = 0; i < pixels; ++i) {
        uint8_t r = interleaved_rgb[i * 3 + 0];
        uint8_t g = interleaved_rgb[i * 3 + 1];
        uint8_t b = interleaved_rgb[i * 3 + 2];
        
        planar_rgb[0 * pixels + i] = r; // R平面
        planar_rgb[1 * pixels + i] = g; // G平面
        planar_rgb[2 * pixels + i] = b; // B平面
    }
}

// ============================================================================
//  测试7: 真实图像性能对比 — 插件 vs 常规内存搬运
// ============================================================================
static void Test_RealImagePerformance(IImageUnifyService* svc) {
    std::cout << "\n===== 测试7: 真实图像性能对比 =====" << std::endl;
    
    const std::string image_dir = "C:/Users/12157/Desktop/AxPlug/test/image/OriImage";
    const std::string output_dir = "C:/Users/12157/Desktop/AxPlug/test/image";
    const int test_image_count = 10;
    const int ITERATIONS = 100;
    
    std::vector<std::string> image_files;
    
    // 获取BMP文件列表
    try {
        for (const auto& entry : std::filesystem::directory_iterator(image_dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".bmp") {
                image_files.push_back(entry.path().string());
                if (image_files.size() >= test_image_count) break;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "无法读取图像目录: " << e.what() << std::endl;
        return;
    }
    
    if (image_files.empty()) {
        std::cout << "未找到BMP图像文件" << std::endl;
        return;
    }
    
    std::cout << "找到 " << image_files.size() << " 张BMP图像进行测试" << std::endl;
    
    double total_plugin_time = 0.0;
    double total_manual_time = 0.0;
#ifdef HAS_HALCON
    double total_halcon_time = 0.0;
#endif
    size_t total_bytes = 0;
    int valid_image_count = 0;
    
    for (size_t img_idx = 0; img_idx < image_files.size(); ++img_idx) {
        const std::string& filepath = image_files[img_idx];
        
        // 读取图像
        int width, height;
        std::vector<uint8_t> rgb_data = LoadBMPAsRGB(filepath, width, height);
        if (rgb_data.empty()) {
            std::cerr << "跳过无效图像: " << filepath << std::endl;
            continue;
        }
        
        int pixels = width * height;
        size_t image_bytes = static_cast<size_t>(pixels) * 3;
        total_bytes += image_bytes;
        valid_image_count++;
        
        std::cout << "  图像" << (img_idx + 1) << ": " << width << "x" << height 
                  << " (" << pixels << " 像素)" << std::endl;
        
        // === 方法1: 使用插件转换 (每次重新提交帧, 避免缓存命中) ===
        double plugin_time = 0.0;
        {
            auto start = std::chrono::high_resolution_clock::now();
            for (int i = 0; i < ITERATIONS; ++i) {
                uint64_t fid = svc->SubmitFrame(rgb_data.data(), width, height, PixelFormat::U8_C3);
                ImageDescriptor planar_view = svc->GetView(fid, MemoryLayout::Planar);
                svc->ReleaseView(fid, planar_view.dataPtr);
                svc->RemoveFrame(fid);
            }
            auto end = std::chrono::high_resolution_clock::now();
            plugin_time = std::chrono::duration<double, std::milli>(end - start).count();
            total_plugin_time += plugin_time;
        }
        
        // === 方法2: 常规内存搬运 ===
        std::vector<uint8_t> manual_planar(image_bytes);
        double manual_time = 0.0;
        {
            auto start = std::chrono::high_resolution_clock::now();
            for (int i = 0; i < ITERATIONS; ++i) {
                ConvertToPlanarManual(rgb_data.data(), manual_planar.data(), width, height);
            }
            auto end = std::chrono::high_resolution_clock::now();
            manual_time = std::chrono::duration<double, std::milli>(end - start).count();
            total_manual_time += manual_time;
        }
        
#ifdef HAS_HALCON
        // === 方法4: Halcon GenImageInterleaved + GetImagePointer3 (含分配+转换+访问) ===
        double halcon_time = 0.0;
        {
            auto start = std::chrono::high_resolution_clock::now();
            for (int i = 0; i < ITERATIONS; ++i) {
                HalconCpp::HImage himg;
                himg.GenImageInterleaved(
                    const_cast<uint8_t*>(rgb_data.data()),
                    "rgb", width, height, 0, "byte",
                    width, height, 0, 0, 8, 0);
                // 强制访问三通道指针, 确保转换完全执行 (与插件GetView对等)
                void *pR, *pG, *pB;
                HalconCpp::HString hType;
                Hlong hw, hh;
                himg.GetImagePointer3(&pR, &pG, &pB, &hType, &hw, &hh);
            }
            auto end = std::chrono::high_resolution_clock::now();
            halcon_time = std::chrono::duration<double, std::milli>(end - start).count();
            total_halcon_time += halcon_time;
        }
#endif
        
        // 验证结果一致性
        uint64_t verify_fid = svc->SubmitFrame(rgb_data.data(), width, height, PixelFormat::U8_C3);
        ImageDescriptor plugin_view = svc->GetView(verify_fid, MemoryLayout::Planar);
        const uint8_t* plugin_data = static_cast<const uint8_t*>(plugin_view.dataPtr);
        
        bool match = std::memcmp(plugin_data, manual_planar.data(), image_bytes) == 0;
        std::cout << "    插件: " << (plugin_time / ITERATIONS) << " ms, "
#ifdef HAS_HALCON
                  << "Halcon: " << (halcon_time / ITERATIONS) << " ms, "
#endif
                  << "常规: " << (manual_time / ITERATIONS) << " ms, "
                  << "验证: " << (match ? "✓" : "✗") << std::endl;
        
        // 对第一张图像: 直接通过ImageDescriptor获取通道指针并保存raw文件
        if (img_idx == 0) {
            std::cout << "  保存第1张图像的三通道raw文件 (使用ImageDescriptor::channelData)..." << std::endl;
            
            size_t channel_bytes = plugin_view.getPlaneSize();
            for (int c = 0; c < 3; ++c) {
                const uint8_t* chPtr = plugin_view.channelData<uint8_t>(c);
                if (!chPtr) { std::cerr << "    通道" << c << "指针为空" << std::endl; continue; }
                
                std::string raw_path = output_dir + "/channel_ch" + std::to_string(c)
                    + "_" + std::to_string(width) + "x" + std::to_string(height) + ".raw";
                std::ofstream raw_file(raw_path, std::ios::binary);
                if (raw_file.is_open()) {
                    raw_file.write(reinterpret_cast<const char*>(chPtr), channel_bytes);
                    std::cout << "    已保存: " << raw_path << " (" << channel_bytes << " bytes)" << std::endl;
                } else {
                    std::cerr << "    无法写入: " << raw_path << std::endl;
                }
            }
        }
        
        svc->ReleaseView(verify_fid, plugin_view.dataPtr);
        svc->RemoveFrame(verify_fid);
    }
    
    if (valid_image_count == 0) return;
    
    // 计算总体性能统计
    double avg_plugin_ms = total_plugin_time / (valid_image_count * ITERATIONS);
    double avg_manual_ms = total_manual_time / (valid_image_count * ITERATIONS);
    double avg_image_bytes = static_cast<double>(total_bytes) / valid_image_count;
    
    std::cout << "\n  性能统计 (" << valid_image_count << "张图像, 每张" << ITERATIONS << "次迭代):" << std::endl;
    std::cout << "    插件完整流程 (Submit+GetView+Release+Remove): " << avg_plugin_ms << " ms/次" << std::endl;
#ifdef HAS_HALCON
    double avg_halcon_ms = total_halcon_time / (valid_image_count * ITERATIONS);
    std::cout << "    Halcon (GenImageInterleaved+GetPointer3):     " << avg_halcon_ms << " ms/次" << std::endl;
#endif
    std::cout << "    常规标量搬运:                                 " << avg_manual_ms << " ms/次" << std::endl;
    
    std::cout << "  性能比 (vs 常规):" << std::endl;
    std::cout << "    插件完整流程: " << (avg_manual_ms / avg_plugin_ms) << "x" << std::endl;
#ifdef HAS_HALCON
    std::cout << "    Halcon:       " << (avg_manual_ms / avg_halcon_ms) << "x" << std::endl;
    std::cout << "  插件 vs Halcon: " << (avg_halcon_ms / avg_plugin_ms) << "x" << std::endl;
#endif
    
    // 吞吐量
    double plugin_mbps = (avg_image_bytes / (1024.0 * 1024.0)) / (avg_plugin_ms / 1000.0);
    double manual_mbps = (avg_image_bytes / (1024.0 * 1024.0)) / (avg_manual_ms / 1000.0);
    
    std::cout << "  吞吐量:" << std::endl;
    std::cout << "    插件完整: " << plugin_mbps << " MB/s" << std::endl;
#ifdef HAS_HALCON
    double halcon_mbps = (avg_image_bytes / (1024.0 * 1024.0)) / (avg_halcon_ms / 1000.0);
    std::cout << "    Halcon:   " << halcon_mbps << " MB/s" << std::endl;
#endif
    std::cout << "    常规:     " << manual_mbps << " MB/s" << std::endl;
}

// ============================================================================
//  测试8: R()/G()/B() 语义化通道访问演示
// ============================================================================
static void Test_ChannelAccess(IImageUnifyService* svc) {
    std::cout << "\n===== 测试8: R()/G()/B() 语义化通道访问 =====" << std::endl;
    
    const int W = 640, H = 480, CH = 3;
    auto imgData = GenerateTestImage(W, H, CH);
    
    ScopedFrame frame(svc, imgData.data(), W, H, PixelFormat::U8_C3);
    assert(frame.Ok());
    std::cout << "  ScopedFrame 创建, id=" << frame.Id() << std::endl;
    
    // ★ ScopedView + R()/G()/B() — 一行获取通道指针
    {
        ScopedView planar(svc, frame.Id(), MemoryLayout::Planar);
        assert(planar.Ok());
        
        auto ch0 = planar.R();   // uint8_t* 红色通道
        auto ch1 = planar.G();   // uint8_t* 绿色通道
        auto ch2 = planar.B();   // uint8_t* 蓝色通道
        
        std::cout << "  R()=" << (void*)ch0 << " G()=" << (void*)ch1 << " B()=" << (void*)ch2 << std::endl;
        std::cout << "  平面大小: " << planar.Desc().getPlaneSize() << " bytes" << std::endl;
        
        // 验证数据正确性
        bool dataValid = true;
        for (int i = 0; i < 10 && dataValid; ++i) {
            int px = i * 100;
            if (px < W * H) {
                if (ch0[px] != imgData[px * 3 + 0] || 
                    ch1[px] != imgData[px * 3 + 1] || 
                    ch2[px] != imgData[px * 3 + 2]) {
                    dataValid = false;
                }
            }
        }
        std::cout << "  数据验证: " << (dataValid ? "通过 ✓" : "失败 ✗") << std::endl;
        
        // Interleaved描述符上调用getChannelPtr应返回nullptr
        ImageDescriptor intDesc = svc->GetView(frame.Id(), MemoryLayout::Interleaved);
        assert(intDesc.getChannelPtr(0) == nullptr);
        svc->ReleaseView(frame.Id(), intDesc.dataPtr);
        
        // 计算通道平均值
        double sum0 = 0, sum1 = 0, sum2 = 0;
        int pixels = W * H;
        for (int i = 0; i < pixels; ++i) {
            sum0 += ch0[i]; sum1 += ch1[i]; sum2 += ch2[i];
        }
        std::cout << "  通道平均值: ch0=" << (sum0 / pixels) 
                  << ", ch1=" << (sum1 / pixels) 
                  << ", ch2=" << (sum2 / pixels) << std::endl;
    }
}

// ============================================================================
//  测试10: 多格式转换性能对比
// ============================================================================
static void Test_MultiFormatPerformance(IImageUnifyService* svc) {
    std::cout << "\n===== 测试10: 多格式转换性能 =====" << std::endl;

    const int W = 1920, H = 1080;
    const int ITERATIONS = 100;

    struct FormatTestCase {
        PixelFormat fmt;
        const char* name;
        int channels;
        int elemSize; // bytes per element per channel
    };

    FormatTestCase cases[] = {
        { PixelFormat::U8_C1,      "U8_C1",      1, 1 },
        { PixelFormat::U8_C3,      "U8_C3",      3, 1 },
        { PixelFormat::U8_C4,      "U8_C4",      4, 1 },
        { PixelFormat::Float32_C1, "Float32_C1", 1, 4 },
        { PixelFormat::Float32_C3, "Float32_C3", 3, 4 },
        { PixelFormat::Float32_C4, "Float32_C4", 4, 4 },
    };

    std::cout << "  分辨率: " << W << "x" << H << ", " << ITERATIONS << "次迭代" << std::endl;
    std::cout << "  格式           | I→P (ms) | P→I (ms) | 数据量 (MB)" << std::endl;
    std::cout << "  ---------------+----------+----------+-----------" << std::endl;

    for (auto& tc : cases) {
        ImageDescriptor tmp;
        tmp.format = tc.fmt;
        int bpp = tmp.getBytesPerPixel();
        size_t dataSize = static_cast<size_t>(W) * H * bpp;
        double dataMB = dataSize / (1024.0 * 1024.0);

        // 生成测试数据
        std::vector<uint8_t> interleavedData(dataSize, 0);
        for (size_t i = 0; i < dataSize; ++i)
            interleavedData[i] = static_cast<uint8_t>(i & 0xFF);

        // I→P: Submit interleaved, GetView planar
        double i2p_ms = 0.0;
        {
            auto start = std::chrono::high_resolution_clock::now();
            for (int i = 0; i < ITERATIONS; ++i) {
                uint64_t fid = svc->SubmitFrame(interleavedData.data(), W, H, tc.fmt,
                                                 MemoryLayout::Interleaved);
                ImageDescriptor view = svc->GetView(fid, MemoryLayout::Planar);
                svc->ReleaseView(fid, view.dataPtr);
                svc->RemoveFrame(fid);
            }
            auto end = std::chrono::high_resolution_clock::now();
            i2p_ms = std::chrono::duration<double, std::milli>(end - start).count() / ITERATIONS;
        }

        // P→I: Submit planar, GetView interleaved
        // 先创建一个planar buffer
        std::vector<uint8_t> planarData(dataSize, 0);
        {
            // 用服务转一次得到正确的planar数据
            uint64_t fid = svc->SubmitFrame(interleavedData.data(), W, H, tc.fmt,
                                             MemoryLayout::Interleaved);
            ImageDescriptor pv = svc->GetView(fid, MemoryLayout::Planar);
            if (pv.dataPtr) std::memcpy(planarData.data(), pv.dataPtr, dataSize);
            svc->ReleaseView(fid, pv.dataPtr);
            svc->RemoveFrame(fid);
        }

        double p2i_ms = 0.0;
        {
            auto start = std::chrono::high_resolution_clock::now();
            for (int i = 0; i < ITERATIONS; ++i) {
                uint64_t fid = svc->SubmitFrame(planarData.data(), W, H, tc.fmt,
                                                 MemoryLayout::Planar);
                ImageDescriptor view = svc->GetView(fid, MemoryLayout::Interleaved);
                svc->ReleaseView(fid, view.dataPtr);
                svc->RemoveFrame(fid);
            }
            auto end = std::chrono::high_resolution_clock::now();
            p2i_ms = std::chrono::duration<double, std::milli>(end - start).count() / ITERATIONS;
        }

        char buf[128];
        snprintf(buf, sizeof(buf), "  %-15s| %8.4f | %8.4f | %8.2f",
                 tc.name, i2p_ms, p2i_ms, dataMB);
        std::cout << buf << std::endl;
    }
}

// ============================================================================
//  测试11: QImage 格式支持
// ============================================================================
static void Test_QImageFormat(IImageUnifyService* svc) {
    std::cout << "\n===== 测试11: QImage 格式支持 =====" << std::endl;

    // QImage 使用 RGBA (U8_C4) Interleaved 格式
    // 模拟QImage数据: 640x480 RGBA
    const int W = 640, H = 480;
    const int CH = 4;
    std::vector<uint8_t> rgbaData(W * H * CH);

    // 填充测试图案 (红/绿/蓝/alpha 条纹)
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            int idx = (y * W + x) * CH;
            rgbaData[idx + 0] = static_cast<uint8_t>(x % 256);  // R
            rgbaData[idx + 1] = static_cast<uint8_t>(y % 256);  // G
            rgbaData[idx + 2] = static_cast<uint8_t>((x + y) % 256); // B
            rgbaData[idx + 3] = 255;  // A (不透明)
        }
    }

    // 测试 U8_C4 提交和转换
    ScopedFrame frame(svc, rgbaData.data(), W, H, PixelFormat::U8_C4);
    assert(frame.Ok());
    std::cout << "  U8_C4 (QImage/RGBA) 提交成功, id=" << frame.Id() << std::endl;

    // I→P 转换
    {
        ScopedView planar(svc, frame.Id(), MemoryLayout::Planar);
        assert(planar.Ok());

        auto r = planar.R();
        auto g = planar.G();
        auto b = planar.B();
        auto a = planar.A();

        std::cout << "  Planar视图: R=" << (void*)r << " G=" << (void*)g
                  << " B=" << (void*)b << " A=" << (void*)a << std::endl;

        // 验证通道数据
        bool valid = true;
        for (int i = 0; i < 100 && valid; ++i) {
            int px = i * 50;
            if (px < W * H) {
                int x = px % W, y = px / W;
                if (r[px] != static_cast<uint8_t>(x % 256) ||
                    g[px] != static_cast<uint8_t>(y % 256) ||
                    b[px] != static_cast<uint8_t>((x + y) % 256) ||
                    a[px] != 255) {
                    valid = false;
                }
            }
        }
        std::cout << "  RGBA通道验证: " << (valid ? "通过 ✓" : "失败 ✗") << std::endl;
    }

    // P→I 往返验证
    {
        ScopedView interleaved(svc, frame.Id(), MemoryLayout::Interleaved);
        assert(interleaved.Ok());
        const uint8_t* iData = static_cast<const uint8_t*>(interleaved.Data());

        bool roundTrip = true;
        for (int i = 0; i < 100 && roundTrip; ++i) {
            int px = i * 50;
            if (px < W * H) {
                int idx = px * CH;
                if (iData[idx] != rgbaData[idx] || iData[idx+1] != rgbaData[idx+1] ||
                    iData[idx+2] != rgbaData[idx+2] || iData[idx+3] != rgbaData[idx+3]) {
                    roundTrip = false;
                }
            }
        }
        std::cout << "  I→P→I 往返验证: " << (roundTrip ? "通过 ✓" : "失败 ✗") << std::endl;
    }

    // 性能测试: U8_C4 转换
    const int ITERATIONS = 100;
    {
        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < ITERATIONS; ++i) {
            uint64_t fid = svc->SubmitFrame(rgbaData.data(), W, H, PixelFormat::U8_C4);
            ImageDescriptor view = svc->GetView(fid, MemoryLayout::Planar);
            svc->ReleaseView(fid, view.dataPtr);
            svc->RemoveFrame(fid);
        }
        auto end = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(end - start).count() / ITERATIONS;
        double mbps = (W * H * CH / 1024.0 / 1024.0) / (ms / 1000.0);
        std::cout << "  U8_C4 I→P 性能: " << ms << " ms/次, " << mbps << " MB/s" << std::endl;
    }

#ifdef QT_CORE_LIB
    // QImage 真实集成测试 (仅在Qt环境下编译)
    {
        ScopedView intView(svc, frame.Id(), MemoryLayout::Interleaved);
        QImage qimg = Unify::ToQImage(intView);
        std::cout << "  QImage 转换: " << qimg.width() << "x" << qimg.height()
                  << " format=" << qimg.format() << std::endl;
    }
#else
    std::cout << "  [跳过] QImage真实集成 (未定义 QT_CORE_LIB)" << std::endl;
#endif
}

// ============================================================================
//  测试9: Halcon集成 — HImage转换 + 图像处理
// ============================================================================
#ifdef HAS_HALCON
static void Test_HalconIntegration(IImageUnifyService* svc) {
    std::cout << "\n===== 测试9: Halcon集成 — HImage转换 =====" << std::endl;

    const int W = 640, H = 480, CH = 3;
    auto imgData = GenerateTestImage(W, H, CH);

    // 1. 提交帧到服务
    ScopedFrame frame(svc, imgData.data(), W, H, PixelFormat::U8_C3);
    assert(frame.Ok());
    std::cout << "  ScopedFrame 创建, id=" << frame.Id() << std::endl;

    // 2. ★ 一行代码: ScopedView → HImage (推荐用法)
    ScopedView planarView(svc, frame.Id(), MemoryLayout::Planar);
    HalconCpp::HImage himg = Unify::ToHImage(planarView);  // 一行搞定!
    HalconCpp::HTuple hWidth, hHeight;
    himg.GetImageSize(&hWidth, &hHeight);
    std::cout << "  Unify::ToHImage(view) 一行转换: " << hWidth.I() << "x" << hHeight.I() << std::endl;

    // 也可以通过 R()/G()/B() 直接访问Planar通道
    std::cout << "  planarView.R()=" << (void*)planarView.R()
              << " G()=" << (void*)planarView.G()
              << " B()=" << (void*)planarView.B() << std::endl;

    // 3. 获取三通道指针验证
    void *ptrR = nullptr, *ptrG = nullptr, *ptrB = nullptr;
    HalconCpp::HString hType;
    Hlong pw = 0, ph = 0;
    himg.GetImagePointer3(&ptrR, &ptrG, &ptrB, &hType, &pw, &ph);
    std::cout << "  GetImagePointer3: type=" << hType.Text()
              << " " << pw << "x" << ph
              << " R=" << ptrR << " G=" << ptrG << " B=" << ptrB << std::endl;

    // 4. GenImageInterleaved: 直接从交错数据创建HImage
    HalconCpp::HImage himg2;
    himg2.GenImageInterleaved(
        const_cast<uint8_t*>(imgData.data()),
        "rgb", W, H, 0, "byte", W, H, 0, 0, 8, 0);
    HalconCpp::HTuple hW2, hH2;
    himg2.GetImageSize(&hW2, &hH2);
    std::cout << "  GenImageInterleaved 成功: " << hW2.I() << "x" << hH2.I() << std::endl;

    // 5. Halcon图像处理: RGB→灰度→阈值→连通域
    HalconCpp::HImage gray = himg.Rgb1ToGray();
    HalconCpp::HTuple gw, gh;
    gray.GetImageSize(&gw, &gh);
    std::cout << "  Rgb1ToGray: " << gw.I() << "x" << gh.I() << std::endl;

    HalconCpp::HRegion dark_region = gray.Threshold(0, 100);
    HalconCpp::HRegion connected = dark_region.Connection();
    Hlong region_count = connected.CountObj();
    std::cout << "  阈值分割: 检测到 " << region_count << " 个区域" << std::endl;

    if (region_count > 0) {
        for (Hlong i = 1; i <= std::min(region_count, (Hlong)5); ++i) {
            HalconCpp::HRegion single = connected.SelectObj(i);
            double row, col;
            Hlong area = single.AreaCenter(&row, &col);
            if (area > 5) {
                std::cout << "    区域#" << i << ": 面积=" << area
                          << " 中心=(" << (int)col << "," << (int)row << ")" << std::endl;
            }
        }
    }

    // 6. 从Halcon三通道数据提交回服务 (Planar → 服务)
    //    使用GetImagePointer3获取的Planar指针, 手动拼接为连续Planar buffer
    {
        size_t planeSize = static_cast<size_t>(W) * H;
        std::vector<uint8_t> planarBuf(planeSize * 3);
        std::memcpy(planarBuf.data(),              ptrR, planeSize);
        std::memcpy(planarBuf.data() + planeSize,  ptrG, planeSize);
        std::memcpy(planarBuf.data() + planeSize * 2, ptrB, planeSize);

        uint64_t fid = svc->SubmitFrame(planarBuf.data(), W, H,
                                         PixelFormat::U8_C3, MemoryLayout::Planar);
        std::cout << "  Halcon Planar → SubmitFrame 成功, id=" << fid << std::endl;

        // 获取Interleaved视图 (反向转换验证)
        ScopedView intView(svc, fid, MemoryLayout::Interleaved);
        std::cout << "  Interleaved视图: " << (intView.Ok() ? "成功 ✓" : "失败 ✗") << std::endl;

        svc->RemoveFrame(fid);
    }

    // 7. 释放Planar视图引用
    svc->ReleaseView(frame.Id(), nullptr);  // frame析构时自动RemoveFrame
    std::cout << "  Halcon集成测试完成 ✓" << std::endl;
}
#endif

// ============================================================================
//  Main
// ============================================================================
int main() {
#ifdef _WIN32
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
#endif

    std::cout << "============================================" << std::endl;
    std::cout << "  ImageUnifyService 测试 Demo" << std::endl;
    std::cout << "  工业视觉场景: 缺陷检测 / 测量" << std::endl;
    std::cout << "============================================" << std::endl;

    // 通过插件系统加载服务
    AxPlug::Init();

    auto svc = AxPlug::GetService<IImageUnifyService>();
    if (!svc) {
        std::cerr << "无法加载 ImageUnifyService 插件!" << std::endl;
        return 1;
    }

    // 运行所有测试
    Test_BasicAPI(svc.get());
    Test_RAII(svc.get());
    Test_MultiFrame(svc.get());
    Test_Performance(svc.get());
    Test_DescriptorCreate(svc.get());
    Test_RealImagePerformance(svc.get());
    Test_ChannelAccess(svc.get());
    Test_MultiFormatPerformance(svc.get());
    Test_QImageFormat(svc.get());

#ifdef HAS_OPENCV
    Test_OpenCVIntegration(svc.get());
#else
    std::cout << "\n[跳过] OpenCV集成测试 (未定义 HAS_OPENCV)" << std::endl;
#endif

#ifdef HAS_HALCON
    Test_HalconIntegration(svc.get());
#else
    std::cout << "\n[跳过] Halcon集成测试 (未定义 HAS_HALCON)" << std::endl;
#endif

    std::cout << "\n============================================" << std::endl;
    std::cout << "  所有测试完成!" << std::endl;
    std::cout << "============================================" << std::endl;

    return 0;
}
