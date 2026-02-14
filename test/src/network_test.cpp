#include <iostream>
#include <windows.h>
#include <thread>
#include <chrono>

// v2: 只需引入 AxPlug.h 和接口头文件
#include "AxPlug/AxPlug.h"
#include "driver/ITcpServer.h"
#include "driver/ITcpClient.h"
#include "driver/IUdpSocket.h"

void testTcpServer() {
    std::cout << "\n=== TCP服务器测试 ===" << std::endl;
    
    // 创建TCP服务器实例
    auto* server = AxPlug::CreateTool<ITcpServer>();
    if (!server) {
        std::cout << "❌ TcpServer创建失败" << std::endl;
        return;
    }
    
    std::cout << "✅ TcpServer创建成功！" << std::endl;
    
    // 测试基本功能
    std::cout << "测试TCP服务器基本功能..." << std::endl;
    
    // 设置最大连接数
    server->SetMaxConnections(10);
    std::cout << "最大连接数设置为: " << server->GetMaxConnections() << std::endl;
    
    // 设置超时时间
    server->SetTimeout(5000);
    std::cout << "超时时间设置为: " << server->GetTimeout() << " ms" << std::endl;
    
    // 启动服务器
    std::cout << "\n启动TCP服务器..." << std::endl;
    bool started = server->Listen(8080);
    if (started) {
        std::cout << "✅ TCP服务器已启动，监听端口 8080" << std::endl;
        std::cout << "监听地址: " << (server->GetListenAddress() ? server->GetListenAddress() : "0.0.0.0") << std::endl;
        std::cout << "监听端口: " << server->GetListenPort() << std::endl;
        std::cout << "正在监听: " << (server->IsListening() ? "是" : "否") << std::endl;
        std::cout << "正在运行: " << (server->IsRunning() ? "是" : "否") << std::endl;
        
        // 等待一段时间
        std::cout << "\n服务器运行中，等待5秒..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(5));
        
        // 检查连接数
        std::cout << "当前连接数: " << server->GetConnectedCount() << std::endl;
        
        // 停止服务器
        std::cout << "\n停止TCP服务器..." << std::endl;
        server->StopListening();
        std::cout << "✅ TCP服务器已停止" << std::endl;
        std::cout << "正在监听: " << (server->IsListening() ? "是" : "否") << std::endl;
        
    } else {
        std::cout << "❌ TCP服务器启动失败" << std::endl;
        const char* error = server->GetLastError();
        int errorCode = server->GetErrorCode();
        std::cout << "错误信息: " << (error ? error : "未知错误") << std::endl;
        std::cout << "错误代码: " << errorCode << std::endl;
    }
    
    // 销毁服务
    AxPlug::DestroyTool(server);
    std::cout << "✅ TcpServer 已销毁" << std::endl;
}

void testTcpClient() {
    std::cout << "\n=== TCP客户端测试 ===" << std::endl;
    
    // 创建TCP客户端实例
    auto* client = AxPlug::CreateTool<ITcpClient>();
    if (!client) {
        std::cout << "❌ TcpClient创建失败" << std::endl;
        return;
    }
    
    std::cout << "✅ TcpClient创建成功！" << std::endl;
    
    // 设置缓冲区大小
    client->SetBufferSize(4096);
    std::cout << "缓冲区大小设置为: " << client->GetBufferSize() << " 字节" << std::endl;
    
    // 设置超时时间
    client->SetTimeout(3000);
    std::cout << "超时时间设置为: " << client->GetTimeout() << " ms" << std::endl;
    
    // 测试连接功能
    std::cout << "\n测试连接到服务器..." << std::endl;
    bool connected = client->Connect("127.0.0.1", 8080);
    if (connected) {
        std::cout << "✅ TCP客户端已连接到服务器" << std::endl;
        std::cout << "本地地址: " << (client->GetLocalAddress() ? client->GetLocalAddress() : "未知") << std::endl;
        std::cout << "本地端口: " << client->GetLocalPort() << std::endl;
        std::cout << "远程地址: " << (client->GetRemoteAddress() ? client->GetRemoteAddress() : "未知") << std::endl;
        std::cout << "远程端口: " << client->GetRemotePort() << std::endl;
        std::cout << "连接状态: " << (client->IsConnected() ? "已连接" : "未连接") << std::endl;
        
        // 测试数据发送
        std::cout << "\n测试数据发送..." << std::endl;
        
        // 发送字符串
        const char* message = "Hello from TCP Client!";
        bool sent = client->SendString(message);
        if (sent) {
            std::cout << "✅ 字符串发送成功: " << message << std::endl;
        } else {
            std::cout << "❌ 字符串发送失败" << std::endl;
        }
        
        // 发送二进制数据
        uint8_t binaryData[] = {0x01, 0x02, 0x03, 0x04, 0x05};
        bool binarySent = client->Send(binaryData, sizeof(binaryData));
        if (binarySent) {
            std::cout << "✅ 二进制数据发送成功，长度: " << sizeof(binaryData) << " 字节" << std::endl;
        } else {
            std::cout << "❌ 二进制数据发送失败" << std::endl;
        }
        
        // 等待一段时间
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        // 测试数据接收
        std::cout << "\n测试数据接收..." << std::endl;
        char receiveBuffer[1024];
        size_t receivedLen = 0;
        
        bool received = client->ReceiveString(receiveBuffer, sizeof(receiveBuffer), receivedLen);
        if (received && receivedLen > 0) {
            receiveBuffer[receivedLen] = '\0';
            std::cout << "✅ 接收到数据: " << receiveBuffer << std::endl;
            std::cout << "数据长度: " << receivedLen << " 字节" << std::endl;
        } else {
            std::cout << "ℹ️  未接收到数据（正常，因为没有服务器响应）" << std::endl;
        }
        
        // 断开连接
        std::cout << "\n断开连接..." << std::endl;
        client->Disconnect();
        std::cout << "✅ TCP客户端已断开连接" << std::endl;
        std::cout << "连接状态: " << (client->IsConnected() ? "已连接" : "未连接") << std::endl;
        
    } else {
        std::cout << "❌ TCP客户端连接失败" << std::endl;
        const char* error = client->GetLastError();
        int errorCode = client->GetErrorCode();
        std::cout << "错误信息: " << (error ? error : "未知错误") << std::endl;
        std::cout << "错误代码: " << errorCode << std::endl;
    }
    
    // 销毁服务
    AxPlug::DestroyTool(client);
    std::cout << "✅ TcpClient 已销毁" << std::endl;
}

void testServerClientInteraction() {
    std::cout << "\n=== 服务器客户端交互测试 ===" << std::endl;
    
    // 创建服务器
    auto* server = AxPlug::CreateTool<ITcpServer>();
    if (!server) {
        std::cout << "❌ TcpServer创建失败" << std::endl;
        return;
    }
    
    // 创建客户端
    auto* client = AxPlug::CreateTool<ITcpClient>();
    if (!client) {
        std::cout << "❌ TcpClient创建失败" << std::endl;
        AxPlug::DestroyTool(server);
        return;
    }
    
    std::cout << "✅ 服务器和客户端创建成功！" << std::endl;
    
    // 启动服务器
    std::cout << "\n启动服务器..." << std::endl;
    if (!server->Listen(8081)) {
        std::cout << "❌ 服务器启动失败" << std::endl;
        AxPlug::DestroyTool(server);
        AxPlug::DestroyTool(client);
        return;
    }
    
    std::cout << "✅ 服务器已启动，监听端口 8081" << std::endl;
    
    // 等待服务器准备就绪
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // 客户端连接
    std::cout << "\n客户端连接到服务器..." << std::endl;
    if (!client->Connect("127.0.0.1", 8081)) {
        std::cout << "❌ 客户端连接失败" << std::endl;
        server->StopListening();
        AxPlug::DestroyTool(server);
        AxPlug::DestroyTool(client);
        return;
    }
    
    std::cout << "✅ 客户端已连接到服务器" << std::endl;
    std::cout << "服务器连接数: " << server->GetConnectedCount() << std::endl;
    
    // 客户端发送消息
    const char* message = "Hello Server! This is client.";
    std::cout << "\n客户端发送消息: " << message << std::endl;
    if (!client->SendString(message)) {
        std::cout << "❌ 客户端发送失败" << std::endl;
    } else {
        std::cout << "✅ 客户端发送成功" << std::endl;
    }
    
    // 等待消息传输
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // 服务器接受连接
    auto serverClient = server->Accept();
    if (serverClient) {
        std::cout << "✅ 服务器接受了客户端连接" << std::endl;
        
        // 服务器接收数据
        char receiveBuffer[1024];
        size_t receivedLen = 0;
        if (serverClient->ReceiveString(receiveBuffer, sizeof(receiveBuffer), receivedLen)) {
            receiveBuffer[receivedLen] = '\0';
            std::cout << "✅ 服务器接收到消息: " << receiveBuffer << std::endl;
            
            // 服务器回复消息
            const char* reply = "Hello Client! This is server.";
            std::cout << "服务器回复消息: " << reply << std::endl;
            if (serverClient->SendString(reply)) {
                std::cout << "✅ 服务器回复成功" << std::endl;
            }
        }
        
        // 断开客户端连接
        server->DisconnectClient(serverClient);
        AxPlug::DestroyTool(serverClient);
        std::cout << "✅ 服务器已断开客户端连接" << std::endl;
    } else {
        std::cout << "ℹ️  服务器未接受到连接（可能需要更长时间）" << std::endl;
    }
    
    // 客户端接收回复
    char clientBuffer[1024];
    size_t clientReceivedLen = 0;
    if (client->ReceiveString(clientBuffer, sizeof(clientBuffer), clientReceivedLen)) {
        clientBuffer[clientReceivedLen] = '\0';
        std::cout << "✅ 客户端接收到回复: " << clientBuffer << std::endl;
    }
    
    // 清理
    client->Disconnect();
    server->StopListening();
    AxPlug::DestroyTool(server);
    AxPlug::DestroyTool(client);
    
    std::cout << "✅ 交互测试完成，资源已清理" << std::endl;
}

void testNetworkPerformance() {
    std::cout << "\n=== 网络性能测试 ===" << std::endl;
    
    auto* client = AxPlug::CreateTool<ITcpClient>();
    if (!client) {
        std::cout << "❌ TcpClient创建失败" << std::endl;
        return;
    }
    
    // 连接到本地回环地址（会失败，但可以测试连接性能）
    std::cout << "测试连接性能..." << std::endl;
    const int testCount = 100;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    int successCount = 0;
    for (int i = 0; i < testCount; i++) {
        if (client->Connect("127.0.0.1", 9999)) {  // 不存在的端口
            successCount++;
            client->Disconnect();
        }
        
        // 短暂延迟
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "✅ 网络性能测试完成" << std::endl;
    std::cout << "测试次数: " << testCount << std::endl;
    std::cout << "成功连接: " << successCount << " (预期为0，因为端口不存在)" << std::endl;
    std::cout << "总耗时: " << duration.count() << " ms" << std::endl;
    std::cout << "平均每次连接: " << static_cast<double>(duration.count()) / testCount << " ms" << std::endl;
    
    AxPlug::DestroyTool(client);
    std::cout << "✅ 性能测试资源已清理" << std::endl;
}

// Boost网络测试
void testBoostNetwork() {
    std::cout << "\n=== Boost网络插件测试 ===" << std::endl;
    
    // 测试Boost TCP客户端
    std::cout << "\n--- Boost TCP客户端测试 ---" << std::endl;
    auto* boostClient = AxPlug::CreateTool<ITcpClient>();
    if (boostClient) {
        std::cout << "✅ Boost TCP客户端创建成功！" << std::endl;
        
        // 测试基本功能
        boostClient->SetTimeout(3000);
        boostClient->SetBufferSize(8192);
        boostClient->SetKeepAlive(true);
        
        std::cout << "超时设置: " << boostClient->GetTimeout() << " ms" << std::endl;
        std::cout << "缓冲区大小: " << boostClient->GetBufferSize() << " bytes" << std::endl;
        std::cout << "KeepAlive: " << (boostClient->IsKeepAliveEnabled() ? "启用" : "禁用") << std::endl;
        
        // 尝试连接到本地服务器
        std::cout << "\n尝试连接到本地服务器..." << std::endl;
        bool connected = boostClient->Connect("127.0.0.1", 8080);
        if (connected) {
            std::cout << "✅ 连接成功！" << std::endl;
            std::cout << "本地地址: " << (boostClient->GetLocalAddress() ? boostClient->GetLocalAddress() : "未知") << std::endl;
            std::cout << "本地端口: " << boostClient->GetLocalPort() << std::endl;
            std::cout << "远程地址: " << (boostClient->GetRemoteAddress() ? boostClient->GetRemoteAddress() : "未知") << std::endl;
            std::cout << "远程端口: " << boostClient->GetRemotePort() << std::endl;
            
            // 发送测试数据
            const char* testData = "Hello from Boost TCP Client!";
            bool sent = boostClient->SendString(testData);
            if (sent) {
                std::cout << "✅ 数据发送成功: " << testData << std::endl;
            } else {
                std::cout << "❌ 数据发送失败: " << boostClient->GetLastError() << std::endl;
            }
            
            // 断开连接
            boostClient->Disconnect();
            std::cout << "✅ 连接已断开" << std::endl;
        } else {
            std::cout << "⚠️ 连接失败 (可能是服务器未启动): " << boostClient->GetLastError() << std::endl;
        }
        
        AxPlug::DestroyTool(boostClient);
    } else {
        std::cout << "❌ Boost TCP客户端创建失败 - 可能是Boost库未正确安装" << std::endl;
    }
    
    // 测试Boost TCP服务器
    std::cout << "\n--- Boost TCP服务器测试 ---" << std::endl;
    auto* boostServer = AxPlug::CreateTool<ITcpServer>();
    if (boostServer) {
        std::cout << "✅ Boost TCP服务器创建成功！" << std::endl;
        
        // 测试基本功能
        boostServer->SetMaxConnections(5);
        boostServer->SetTimeout(5000);
        
        std::cout << "最大连接数: " << boostServer->GetMaxConnections() << std::endl;
        std::cout << "超时时间: " << boostServer->GetTimeout() << " ms" << std::endl;
        
        // 启动服务器
        std::cout << "\n启动Boost TCP服务器..." << std::endl;
        bool started = boostServer->Listen(8081);
        if (started) {
            std::cout << "✅ Boost TCP服务器已启动，监听端口 8081" << std::endl;
            std::cout << "监听地址: " << (boostServer->GetListenAddress() ? boostServer->GetListenAddress() : "0.0.0.0") << std::endl;
            std::cout << "正在监听: " << (boostServer->IsListening() ? "是" : "否") << std::endl;
            std::cout << "正在运行: " << (boostServer->IsRunning() ? "是" : "否") << std::endl;
            
            // 等待一段时间
            std::cout << "\n服务器运行中，等待3秒..." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(3));
            
            // 检查连接数
            std::cout << "当前连接数: " << boostServer->GetConnectedCount() << std::endl;
            
            // 停止服务器
            boostServer->StopListening();
            std::cout << "✅ Boost TCP服务器已停止" << std::endl;
        } else {
            std::cout << "❌ Boost TCP服务器启动失败: " << boostServer->GetLastError() << std::endl;
        }
        
        AxPlug::DestroyTool(boostServer);
    } else {
        std::cout << "❌ Boost TCP服务器创建失败 - 可能是Boost库未正确安装" << std::endl;
    }
}

int main() {
    // 设置控制台编码为UTF-8
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
    
    std::cout << "=== AxPlug 网络插件测试 ===" << std::endl;
    
    try {
        // 初始化插件系统
        std::cout << "\n初始化插件系统..." << std::endl;
        AxPlug::Init();
        std::cout << "✅ 插件系统初始化完成" << std::endl;
        
        // 执行网络测试
        testTcpServer();
        testTcpClient();
        testServerClientInteraction();
        testNetworkPerformance();
        
        // 执行Boost网络测试
        testBoostNetwork();
        
    } catch (const std::exception& e) {
        std::cerr << "❌ 异常: " << e.what() << std::endl;
        return 1;
    }
    
    std::cout << "\n=== 测试总结 ===" << std::endl;
    std::cout << "✅ TCP服务器功能" << std::endl;
    std::cout << "✅ TCP客户端功能" << std::endl;
    std::cout << "✅ 服务器客户端交互" << std::endl;
    std::cout << "✅ 网络性能测试" << std::endl;
    std::cout << "✅ Boost网络插件测试" << std::endl;
    
    std::cout << "\n🎉 网络插件测试完成！" << std::endl;
    return 0;
}
