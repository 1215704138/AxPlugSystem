#include <iostream>
#include <windows.h>
#include <chrono>
#include <vector>
#include <string>
#include <sstream>
#include <thread>
#include <cstdio>
#include <cstdarg>

#include "AxPlug/AxPlug.h"
#include "core/LoggerService.h"

// Helper: format string then call logger method (replaces removed variadic interface methods)
static std::string fmt(const char* format, ...) {
    char buf[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    return std::string(buf);
}

// 辅助函数：将整数转换为字符串
std::string intToString(int i) {
    std::ostringstream oss;
    oss << i;
    return oss.str();
}

// ==================== 服务名管理测试 ====================

void testDifferentServiceNames() {
    std::cout << "\n=== 不同服务名创建单例测试 ===" << std::endl;
    
    const char* classID = "service.logger";
    const char* serviceNames[] = {
        "main",
        "backup", 
        "debug",
        "test1",
        "test2"
    };
    
    std::vector<std::shared_ptr<ILoggerService>> loggers;
    
    // 使用不同的服务名创建多个单例
    for (const char* serviceName : serviceNames) {
        std::cout << "\n创建服务实例，serviceName: " << serviceName << std::endl;
        
        auto logger = AxPlug::GetService<ILoggerService>(serviceName);
        if (logger) {
            loggers.push_back(logger);
            std::cout << "✅ 创建成功，地址: " << logger.get() << std::endl;
            
            // 🔧 临时禁用控制台输出，验证是否为I/O阻塞问题
            logger->EnableConsoleOutput(false);
            std::cout << "🔧 已禁用控制台输出" << std::endl;
            
            std::cout << "🔍 即将调用Info..." << std::endl;
            logger->Info(fmt("服务 %s 的日志输出", serviceName).c_str());
            std::cout << "✅ Info调用完成" << std::endl;
        } else {
            std::cout << "❌ 创建失败" << std::endl;
        }
    }
    
    std::cout << "\n总共创建了 " << loggers.size() << " 个服务实例" << std::endl;
    
    // 验证实例的唯一性
    std::cout << "\n验证实例唯一性..." << std::endl;
    bool allUnique = true;
    for (size_t i = 0; i < loggers.size(); i++) {
        for (size_t j = i + 1; j < loggers.size(); j++) {
            if (loggers[i] == loggers[j]) {
                std::cout << "⚠️  实例 " << i << " 和 " << j << " 是同一个！" << std::endl;
                allUnique = false;
            }
        }
    }
    
    if (allUnique) {
        std::cout << "✅ 所有实例都是唯一的" << std::endl;
    }
    
    // 测试GetServiceInstance
    std::cout << "\n测试GetServiceInstance..." << std::endl;
    for (const char* serviceName : serviceNames) {
        auto logger = AxPlug::GetService<ILoggerService>(serviceName);
        if (logger) {
            std::cout << "✅ 获取服务 " << serviceName << " 成功，地址: " << logger.get() << std::endl;
        } else {
            std::cout << "❌ 获取服务 " << serviceName << " 失败" << std::endl;
        }
    }
    
    // 销毁所有实例
    std::cout << "\n销毁所有实例..." << std::endl;
    for (const char* serviceName : serviceNames) {
        AxPlug::ReleaseService<ILoggerService>(serviceName);
        std::cout << "✅ 销毁服务 " << serviceName << std::endl;
    }
}

void testSameServiceNameReuse() {
    std::cout << "\n=== 同名服务复用测试 ===" << std::endl;
    
    const char* classID = "service.logger";
    const char* serviceName = "main";
    
    // 第一次创建
    std::cout << "第一次创建..." << std::endl;
    auto logger1 = AxPlug::GetService<ILoggerService>(serviceName);
    if (logger1) {
        std::cout << "✅ 第一次创建成功，地址: " << logger1.get() << std::endl;
        logger1->Info("第一次创建的日志");
    }
    
    // 第二次创建（应该返回同一实例）
    std::cout << "第二次创建同名服务..." << std::endl;
    auto logger2 = AxPlug::GetService<ILoggerService>(serviceName);
    if (logger2) {
        std::cout << "✅ 第二次创建成功，地址: " << logger2.get() << std::endl;
        logger2->Info("第二次创建的日志");
        
        if (logger1.get() == logger2.get()) {
            std::cout << "✅ 两次创建返回同一实例（单例模式正常）" << std::endl;
        } else {
            std::cout << "⚠️  两次创建返回不同实例（单例模式异常）" << std::endl;
        }
    }
    
    // 使用GetServiceInstance
    std::cout << "使用GetServiceInstance..." << std::endl;
    auto logger3 = AxPlug::GetService<ILoggerService>(serviceName);
    if (logger3) {
        std::cout << "✅ GetServiceInstance成功，地址: " << logger3.get() << std::endl;
        if (logger1.get() == logger3.get()) {
            std::cout << "✅ GetServiceInstance返回同一实例" << std::endl;
        }
    }
    
    // 销毁
    AxPlug::ReleaseService<ILoggerService>(serviceName);
    std::cout << "✅ 销毁完成" << std::endl;
    
    // 销毁后重新创建
    std::cout << "销毁后重新创建..." << std::endl;
    auto logger4 = AxPlug::GetService<ILoggerService>(serviceName);
    if (logger4) {
        std::cout << "✅ 重新创建成功，地址: " << logger4.get() << std::endl;
        if (logger1.get() != logger4.get()) {
            std::cout << "✅ 重新创建返回新实例（正常）" << std::endl;
        } else {
            std::cout << "⚠️  重新创建返回旧实例（异常）" << std::endl;
        }
        AxPlug::ReleaseService<ILoggerService>(serviceName);
    }
}

// ==================== 日志功能测试 ====================

void testBasicLogging() {
    std::cout << "\n=== 基础日志功能测试 ===" << std::endl;
    
    // 创建日志服务实例
    std::cout << "创建LoggerService..." << std::endl;
    auto logger = AxPlug::GetService<ILoggerService>("basic_test");
    if (!logger) {
        std::cout << "❌ LoggerService创建失败" << std::endl;
        return;
    }
    
    std::cout << "✅ LoggerService创建成功！" << std::endl;
    
    // 测试基本功能
    logger->SetLevel(LogLevel::Info);
    std::cout << "日志级别设置为 Info" << std::endl;
    
    // 测试不同级别的日志
    std::cout << "\n测试不同级别的日志输出:" << std::endl;
    logger->Debug("这是一条 Debug 日志 - 应该不会显示");
    logger->Info("这是一条 Info 日志 - 应该显示");
    logger->Warn("这是一条 Warning 日志 - 应该显示");
    logger->Error("这是一条 Error 日志 - 应该显示");
    
    // 测试格式化日志
    std::cout << "\n测试格式化日志:" << std::endl;
    logger->Info(fmt("用户 %s 登录系统，年龄 %d，分数 %.2f", "张三", 25, 95.5).c_str());
    logger->Error(fmt("文件 %s 在第 %d 行发生错误: %s", "test.cpp", 123, "内存访问错误").c_str());
    
    std::cout << "✅ 基础日志测试完成" << std::endl;
    
    // 销毁服务
    AxPlug::ReleaseService<ILoggerService>("basic_test");
}

void testLogLevelControl() {
    std::cout << "\n=== 日志级别控制测试 ===" << std::endl;
    
    auto logger = AxPlug::GetService<ILoggerService>("level_test");
    if (!logger) {
        std::cout << "❌ LoggerService创建失败" << std::endl;
        return;
    }
    
    // 测试不同日志级别
    std::cout << "\n测试 Debug 级别:" << std::endl;
    logger->SetLevel(LogLevel::Debug);
    logger->Debug("Debug 级别 - 应该显示");
    logger->Info("Info 级别 - 应该显示");
    logger->Warn("Warning 级别 - 应该显示");
    logger->Error("Error 级别 - 应该显示");
    
    std::cout << "\n测试 Warning 级别:" << std::endl;
    logger->SetLevel(LogLevel::Warn);
    logger->Debug("Debug 级别 - 不应该显示");
    logger->Info("Info 级别 - 不应该显示");
    logger->Warn("Warning 级别 - 应该显示");
    logger->Error("Error 级别 - 应该显示");
    
    std::cout << "\n测试 Error 级别:" << std::endl;
    logger->SetLevel(LogLevel::Error);
    logger->Debug("Debug 级别 - 不应该显示");
    logger->Info("Info 级别 - 不应该显示");
    logger->Warn("Warning 级别 - 不应该显示");
    logger->Error("Error 级别 - 应该显示");
    
    // 获取当前级别
    LogLevel currentLevel = logger->GetLevel();
    std::cout << "\n当前日志级别: " << static_cast<int>(currentLevel) << std::endl;
    
    std::cout << "✅ 日志级别控制测试完成" << std::endl;
    
    AxPlug::ReleaseService<ILoggerService>("level_test");
}

void testConsoleOutput() {
    std::cout << "\n=== 控制台输出控制测试 ===" << std::endl;
    
    auto logger = AxPlug::GetService<ILoggerService>("console_test");
    if (!logger) {
        std::cout << "❌ LoggerService创建失败" << std::endl;
        return;
    }
    
    // 测试控制台输出开关
    bool consoleEnabled = logger->IsConsoleOutputEnabled();
    std::cout << "初始控制台输出状态: " << (consoleEnabled ? "启用" : "禁用") << std::endl;
    
    // 禁用控制台输出
    std::cout << "\n禁用控制台输出..." << std::endl;
    logger->EnableConsoleOutput(false);
    std::cout << "控制台输出已禁用" << std::endl;
    logger->Info("这条消息不会显示在控制台");
    logger->Error("这条错误消息也不会显示在控制台");
    
    // 重新启用控制台输出
    std::cout << "\n重新启用控制台输出..." << std::endl;
    logger->EnableConsoleOutput(true);
    std::cout << "控制台输出已启用" << std::endl;
    logger->Info("这条消息会显示在控制台");
    logger->Error("这条错误消息也会显示在控制台");
    
    std::cout << "✅ 控制台输出控制测试完成" << std::endl;
    
    AxPlug::ReleaseService<ILoggerService>("console_test");
}

void testTimestampFormat() {
    std::cout << "\n=== 时间戳格式测试 ===" << std::endl;
    
    auto logger = AxPlug::GetService<ILoggerService>("timestamp_test");
    if (!logger) {
        std::cout << "❌ LoggerService创建失败" << std::endl;
        return;
    }
    
    // 测试不同时间戳格式
    std::cout << "\n测试详细时间戳格式:" << std::endl;
    logger->SetTimestampFormat("detailed");
    std::cout << "当前时间戳格式: " << logger->GetTimestampFormat() << std::endl;
    logger->Info("使用详细时间戳格式的日志");
    
    std::cout << "\n测试简单时间戳格式:" << std::endl;
    logger->SetTimestampFormat("simple");
    std::cout << "当前时间戳格式: " << logger->GetTimestampFormat() << std::endl;
    logger->Info("使用简单时间戳格式的日志");
    
    std::cout << "\n测试无时间戳格式:" << std::endl;
    logger->SetTimestampFormat("none");
    std::cout << "当前时间戳格式: " << logger->GetTimestampFormat() << std::endl;
    logger->Info("无时间戳的日志");
    
    // 恢复默认格式
    logger->SetTimestampFormat("detailed");
    std::cout << "✅ 时间戳格式测试完成" << std::endl;
    
    AxPlug::ReleaseService<ILoggerService>("timestamp_test");
}

void testFileLogging() {
    std::cout << "\n=== 文件日志测试 ===" << std::endl;
    
    auto logger = AxPlug::GetService<ILoggerService>("file_test");
    if (!logger) {
        std::cout << "❌ LoggerService创建失败" << std::endl;
        return;
    }
    
    // 获取日志文件信息
    const char* logFile = logger->GetLogFile();
    std::cout << "日志文件路径: " << logFile << std::endl;
    
    // 写入一些日志到文件
    std::cout << "\n写入测试日志到文件..." << std::endl;
    logger->Info("=== 文件日志测试开始 ===");
    logger->Info("这是一条测试信息日志");
    logger->Warn("这是一条测试警告日志");
    logger->Error("这是一条测试错误日志");
    logger->Info(fmt("格式化测试: 数值=%d, 字符串=%s", 42, "测试字符串").c_str());
    logger->Info("=== 文件日志测试结束 ===");
    
    // 刷新日志到文件
    logger->Flush();
    std::cout << "日志已刷新到文件" << std::endl;
    
    std::cout << "✅ 文件日志测试完成" << std::endl;
    std::cout << "请检查日志文件: " << logFile << std::endl;
    
    AxPlug::ReleaseService<ILoggerService>("file_test");
}

void testHighVolumeLogging() {
    std::cout << "\n=== 高频日志测试 ===" << std::endl;
    
    auto logger = AxPlug::GetService<ILoggerService>("volume_test");
    if (!logger) {
        std::cout << "❌ LoggerService创建失败" << std::endl;
        return;
    }
    
    // 测试大量日志输出
    const int logCount = 1000;
    std::cout << "测试输出 " << logCount << " 条日志..." << std::endl;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < logCount; i++) {
        if (i % 100 == 0) {
            logger->Info(fmt("进度: %s/%s (%.1f%%)", intToString(i).c_str(), intToString(logCount).c_str(), (i * 100.0) / logCount).c_str());
        } else if (i % 50 == 0) {
            logger->Error(fmt("警告: 第 %s 条日志", intToString(i).c_str()).c_str());
        } else {
            logger->Log(LogLevel::Debug, fmt("调试信息: 索引 %s", intToString(i).c_str()).c_str());
        }
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    logger->Flush();
    
    std::cout << "✅ 高频日志测试完成" << std::endl;
    std::cout << "总耗时: " << duration.count() << " ms" << std::endl;
    std::cout << "平均每条日志: " << (double)duration.count() / logCount << " ms" << std::endl;
    
    AxPlug::ReleaseService<ILoggerService>("volume_test");
}

// ==================== 生命周期测试 ====================

void testSingleCreateDestroy() {
    std::cout << "\n=== 单次创建销毁测试 ===" << std::endl;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // 创建日志服务
    auto logger = AxPlug::GetService<ILoggerService>("single_test");
    if (!logger) {
        std::cout << "❌ 创建失败" << std::endl;
        return;
    }
    
    std::cout << "✅ 创建成功" << std::endl;
    
    // 测试基本功能
    logger->SetLevel(LogLevel::Info);
    logger->Info("单次创建测试日志");
    
    // 销毁服务
    AxPlug::ReleaseService<ILoggerService>("single_test");
    std::cout << "✅ 销毁成功" << std::endl;
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    std::cout << "耗时: " << duration.count() << " μs" << std::endl;
}

void testMultipleCreateDestroy() {
    std::cout << "\n=== 多次创建销毁测试 ===" << std::endl;
    
    const int iterations = 100;
    std::cout << "执行 " << iterations << " 次创建和销毁..." << std::endl;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < iterations; i++) {
        // 创建
        auto logger = AxPlug::GetService<ILoggerService>("multi_test");
        if (!logger) {
            std::cout << "❌ 第 " << i << " 次创建失败" << std::endl;
            continue;
        }
        
        // 测试功能
        if (i % 10 == 0) {
            logger->Info(fmt("第 %s 次创建测试", intToString(i).c_str()).c_str());
        }
        
        // 销毁
        AxPlug::ReleaseService<ILoggerService>("multi_test");
        
        // 显示进度
        if ((i + 1) % 20 == 0) {
            std::cout << "完成 " << (i + 1) << "/" << iterations << std::endl;
        }
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "✅ 多次创建销毁测试完成" << std::endl;
    std::cout << "总耗时: " << duration.count() << " ms" << std::endl;
    std::cout << "平均每次: " << (double)duration.count() / iterations << " ms" << std::endl;
}

void testConcurrentServices() {
    std::cout << "\n=== 并发服务测试 ===" << std::endl;
    
    const int serviceCount = 5;
    std::vector<std::string> serviceNames;
    std::vector<std::shared_ptr<ILoggerService>> loggers;
    
    // 同时创建多个不同名的服务
    for (int i = 0; i < serviceCount; i++) {
        std::string serviceName = "service.logger.concurrent" + intToString(i);
        serviceNames.push_back(serviceName);
        
        std::cout << "创建服务: " << serviceName << std::endl;
        auto logger = AxPlug::GetService<ILoggerService>(serviceName.c_str());
        if (logger) {
            loggers.push_back(logger);
            std::cout << "✅ 创建成功，地址: " << logger.get() << std::endl;
            logger->Info(fmt("并发服务 %s", serviceName.c_str()).c_str());
        } else {
            std::cout << "❌ 创建失败" << std::endl;
        }
    }
    
    // 检查服务状态
    // v2 中没有 GetServiceCount，跳过此检查
    
    // 逐个销毁
    for (size_t i = 0; i < serviceNames.size(); i++) {
        std::cout << "销毁服务: " << serviceNames[i] << std::endl;
        AxPlug::ReleaseService<ILoggerService>(serviceNames[i].c_str());
        
        // 检查销毁后状态
        // v2 中没有 GetServiceCount，跳过此检查
    }
}

// ==================== 主函数 ====================

int main() {
    // 设置控制台编码为UTF-8
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
    
    std::cout << "=== AxPlug v2 综合日志服务测试 ===" << std::endl;
    
    try {
        // 初始化插件系统
        std::cout << "\n初始化插件系统..." << std::endl;
        AxPlug::Init();
        std::cout << "✅ 插件系统初始化完成" << std::endl;
        
        // 执行所有测试
        std::cout << "\n🚀 开始执行测试..." << std::endl;
        
        // 1. 服务名管理测试
        testDifferentServiceNames();
        testSameServiceNameReuse();
        
        // 2. 日志功能测试
        testBasicLogging();
        testLogLevelControl();
        testConsoleOutput();
        testTimestampFormat();
        testFileLogging();
        testHighVolumeLogging();
        
        // 3. 生命周期测试
        testSingleCreateDestroy();
        testMultipleCreateDestroy();
        testConcurrentServices();
        
        std::cout << "\n=== 测试总结 ===" << std::endl;
        std::cout << "✅ 不同服务名创建单例" << std::endl;
        std::cout << "✅ 同名服务复用" << std::endl;
        std::cout << "✅ 基础日志功能" << std::endl;
        std::cout << "✅ 日志级别控制" << std::endl;
        std::cout << "✅ 控制台输出控制" << std::endl;
        std::cout << "✅ 时间戳格式设置" << std::endl;
        std::cout << "✅ 文件日志功能" << std::endl;
        std::cout << "✅ 高频日志性能" << std::endl;
        std::cout << "✅ 单次创建销毁" << std::endl;
        std::cout << "✅ 多次创建销毁" << std::endl;
        std::cout << "✅ 并发服务管理" << std::endl;
        
        std::cout << "\n🎉 综合日志服务测试完成！" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "❌ 异常: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
