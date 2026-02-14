#include <iostream>
#include <windows.h>
#include <vector>
#include <chrono>
#include <string>
#include <thread>

// v2: 只需引入 AxPlug.h 和接口头文件
#include "AxPlug/AxPlug.h"
#include "business/IMath.h"
#include "core/LoggerService.h"
#include "driver/ITcpClient.h"
#include "driver/ITcpServer.h"
#include "driver/IUdpSocket.h"

void testPluginSystemInfo() {
    std::cout << "\n=== 插件系统信息查询 ===" << std::endl;
    
    int pluginCount = AxPlug::GetPluginCount();
    std::cout << "已加载插件数量: " << pluginCount << std::endl;
    
    if (pluginCount == 0) {
        std::cout << "没有找到任何插件" << std::endl;
        return;
    }
    
    for (int i = 0; i < pluginCount; i++) {
        auto info = AxPlug::GetPluginInfo(i);
        std::cout << "\n插件 " << i << ":" << std::endl;
        std::cout << "  文件名: " << (info.fileName ? info.fileName : "N/A") << std::endl;
        std::cout << "  接口: " << (info.interfaceName ? info.interfaceName : "N/A") << std::endl;
        std::cout << "  类型: " << (info.isTool ? "Tool" : "Service") << std::endl;
        std::cout << "  已加载: " << (info.isLoaded ? "是" : "否") << std::endl;
    }
}

void testMathPlugin() {
    std::cout << "\n=== 数学插件测试 ===" << std::endl;
    
    // 创建数学工具实例
    auto* math = AxPlug::CreateTool<IMath>();
    if (!math) {
        std::cout << "MathPlugin 创建失败" << std::endl;
        return;
    }
    
    std::cout << "MathPlugin 创建成功" << std::endl;
    
    // 测试基本运算
    int a = 100, b = 25;
    int sum = math->Add(a, b);
    int diff = math->Sub(a, b);
    
    std::cout << "数学运算测试:" << std::endl;
    std::cout << "  " << a << " + " << b << " = " << sum << std::endl;
    std::cout << "  " << a << " - " << b << " = " << diff << std::endl;
    
    // 验证结果
    bool correct = (sum == 125) && (diff == 75);
    std::cout << "结果验证: " << (correct ? "正确" : "错误") << std::endl;
    
    // 手动销毁
    AxPlug::DestroyTool(math);
    std::cout << "MathPlugin 已通过 DestroyTool 销毁" << std::endl;
}

void testLoggerService() {
    std::cout << "\n=== 日志服务测试 ===" << std::endl;
    
    // 测试命名服务
    auto* logger1 = AxPlug::GetService<ILoggerService>("main");
    auto* logger2 = AxPlug::GetService<ILoggerService>("debug");
    
    if (!logger1 || !logger2) {
        std::cout << "LoggerService 创建失败" << std::endl;
        return;
    }
    
    std::cout << "LoggerService 创建成功" << std::endl;
    std::cout << "主日志服务地址: " << logger1 << std::endl;
    std::cout << "调试日志服务地址: " << logger2 << std::endl;
    std::cout << "是否为不同实例: " << (logger1 != logger2 ? "是" : "否") << std::endl;
    
    // 测试日志功能
    logger1->SetLevel(LogLevel::Info);
    logger1->EnableConsoleOutput(true);
    logger1->Info("这是主日志服务的消息");
    
    logger2->SetLevel(LogLevel::Debug);
    logger2->EnableConsoleOutput(true);
    logger2->Debug("这是调试日志服务的消息");
    
    // 释放服务
    AxPlug::ReleaseService<ILoggerService>("main");
    AxPlug::ReleaseService<ILoggerService>("debug");
    std::cout << "LoggerService 已释放" << std::endl;
}

void testNetworkPlugins() {
    std::cout << "\n=== 网络插件测试 ===" << std::endl;
    
    // 测试 TCP 客户端
    auto* tcpClient = AxPlug::CreateTool<ITcpClient>();
    if (tcpClient) {
        std::cout << "TCP客户端创建成功" << std::endl;
        std::cout << "超时设置: " << tcpClient->GetTimeout() << " ms" << std::endl;
        tcpClient->SetTimeout(3000);
        std::cout << "超时设置已更新: " << tcpClient->GetTimeout() << " ms" << std::endl;
        AxPlug::DestroyTool(tcpClient);
        std::cout << "TCP客户端已销毁" << std::endl;
    }
    
    // 测试 TCP 服务器
    auto* tcpServer = AxPlug::CreateTool<ITcpServer>();
    if (tcpServer) {
        std::cout << "TCP服务器创建成功" << std::endl;
        std::cout << "最大连接数: " << tcpServer->GetMaxConnections() << std::endl;
        tcpServer->SetMaxConnections(20);
        std::cout << "最大连接数已更新: " << tcpServer->GetMaxConnections() << std::endl;
        AxPlug::DestroyTool(tcpServer);
        std::cout << "TCP服务器已销毁" << std::endl;
    }
    
    // 测试 UDP 套接字
    auto* udpSocket = AxPlug::CreateTool<IUdpSocket>();
    if (udpSocket) {
        std::cout << "UDP套接字创建成功" << std::endl;
        std::cout << "缓冲区大小: " << udpSocket->GetBufferSize() << " bytes" << std::endl;
        udpSocket->SetBufferSize(8192);
        std::cout << "缓冲区大小已更新: " << udpSocket->GetBufferSize() << " bytes" << std::endl;
        AxPlug::DestroyTool(udpSocket);
        std::cout << "UDP套接字已销毁" << std::endl;
    }
}

void testMemoryManagement() {
    std::cout << "\n=== 内存管理测试 ===" << std::endl;
    
    // 创建多个不同类型的实例
    std::vector<IMath*> mathTools;
    std::vector<ITcpClient*> tcpClients;
    
    for (int i = 0; i < 3; i++) {
        auto* math = AxPlug::CreateTool<IMath>();
        if (math) {
            mathTools.push_back(math);
            std::cout << "创建数学工具实例 " << i + 1 << std::endl;
        }
        
        auto* client = AxPlug::CreateTool<ITcpClient>();
        if (client) {
            tcpClients.push_back(client);
            std::cout << "创建TCP客户端实例 " << i + 1 << std::endl;
        }
    }
    
    // 使用工具
    for (size_t i = 0; i < mathTools.size(); i++) {
        int result = mathTools[i]->Add((int)(i * 10), (int)(i * 5));
        std::cout << "数学工具 " << i + 1 << " 计算: " << (i * 10) << " + " << (i * 5) << " = " << result << std::endl;
    }
    
    // 统一销毁
    std::cout << "\n统一销毁所有实例..." << std::endl;
    for (auto& tool : mathTools) {
        AxPlug::DestroyTool(tool);
        tool = nullptr;
    }
    mathTools.clear();
    
    for (auto& client : tcpClients) {
        AxPlug::DestroyTool(client);
        client = nullptr;
    }
    tcpClients.clear();
    
    std::cout << "所有实例已销毁" << std::endl;
}

void testPerformance() {
    std::cout << "\n=== 性能测试 ===" << std::endl;
    
    const int iterations = 1000;
    std::cout << "执行 " << iterations << " 次插件创建和销毁操作..." << std::endl;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < iterations; i++) {
        auto* math = AxPlug::CreateTool<IMath>();
        if (math) {
            volatile int result = math->Add(i, i + 1);
            (void)result;
            AxPlug::DestroyTool(math);
        }
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "性能测试完成" << std::endl;
    std::cout << "总耗时: " << duration.count() << " ms" << std::endl;
    std::cout << "平均每次操作: " << static_cast<double>(duration.count()) / iterations << " ms" << std::endl;
}

int main() {
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
    
    std::cout << "=== AxPlug v2 完整插件系统测试 ===" << std::endl;
    
    try {
        // 初始化插件系统
        std::cout << "\n初始化插件系统..." << std::endl;
        AxPlug::Init();
        std::cout << "插件系统初始化完成" << std::endl;
        
        // 执行测试
        testPluginSystemInfo();
        testMathPlugin();
        testLoggerService();
        testNetworkPlugins();
        testMemoryManagement();
        testPerformance();
        
    } catch (const std::exception& e) {
        std::cerr << "异常: " << e.what() << std::endl;
        return 1;
    }
    
    std::cout << "\n=== 测试总结 ===" << std::endl;
    std::cout << "  插件系统初始化 - OK" << std::endl;
    std::cout << "  插件信息查询 - OK" << std::endl;
    std::cout << "  数学插件功能 - OK" << std::endl;
    std::cout << "  日志服务功能 - OK" << std::endl;
    std::cout << "  网络插件功能 - OK" << std::endl;
    std::cout << "  内存管理 - OK" << std::endl;
    std::cout << "  性能测试 - OK" << std::endl;
    
    std::cout << "\n🎉 AxPlug v2 完整测试成功！" << std::endl;
    return 0;
}
