#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>


// v2: 只需引入 AxPlug.h 和接口头文件
#include "AxPlug/AxPlug.h"
#include "AxPlug/OSUtils.hpp"
#include "business/IMath.h"
#include "core/LoggerService.h"
#include "driver/ITcpServer.h"
#include "driver/ITcpClient.h"
#include "driver/IUdpSocket.h"

// 辅助函数：打印插件信息
void printPluginInfo(int index) {
  auto info = AxPlug::GetPluginInfo(index);
  if (info.isLoaded) {
    std::cout << "插件 [" << index << "]:" << std::endl;
    std::cout << "  文件名: " << (info.fileName ? info.fileName : "N/A")
              << std::endl;
    std::cout << "  接口名: "
              << (info.interfaceName ? info.interfaceName : "N/A") << std::endl;
    std::cout << "  类型: " << (info.isTool ? "Tool" : "Service") << std::endl;
    std::cout << "  已加载: " << (info.isLoaded ? "是" : "否") << std::endl;
  }
}

void testSmartPointerTool() {
  std::cout << "\n=== [2] 智能指针 Tool 测试 (AxPtr / shared_ptr) ==="
            << std::endl;
  AX_PROFILE_SCOPE("testSmartPointerTool");

  // 使用智能指针创建 Tool - 自动引用计数，作用域结束自动释放
  {
    auto math = AxPlug::CreateTool<IMath>();
    if (!math) {
      std::cout << "MathPlugin 创建失败" << std::endl;
      return;
    }

    std::cout << "MathPlugin 智能指针创建成功 (use_count=" << math.use_count()
              << ")" << std::endl;

    // 测试基本运算
    int a = 100, b = 25;
    std::cout << "  " << a << " + " << b << " = " << math->Add(a, b)
              << std::endl;
    std::cout << "  " << a << " - " << b << " = " << math->Sub(a, b)
              << std::endl;

    // 测试引用计数 - 复制 shared_ptr
    auto mathCopy = math;
    std::cout << "复制后 use_count=" << math.use_count() << std::endl;

    // mathCopy 离开作用域时 use_count 减少
    mathCopy.reset();
    std::cout << "reset 后 use_count=" << math.use_count() << std::endl;

    // 测试显式 DestroyTool (shared_ptr 版本)
    AxPlug::DestroyTool(math);
    std::cout << "DestroyTool 后 math 是否为空: "
              << (math == nullptr ? "是" : "否") << std::endl;
  }
  // 作用域结束，如果还有引用会自动释放
  std::cout << "智能指针 Tool 测试通过 (RAII 自动释放)" << std::endl;
}

void testRawPointerTool() {
  std::cout << "\n=== [3] 原始指针 Tool 测试 (CreateToolRaw) ===" << std::endl;
  AX_PROFILE_SCOPE("testRawPointerTool");

  // 使用原始指针创建 Tool - 需要手动释放
  auto *math = AxPlug::CreateToolRaw<IMath>();
  if (!math) {
    std::cout << "MathPlugin (Raw) 创建失败" << std::endl;
    return;
  }

  std::cout << "MathPlugin 原始指针创建成功" << std::endl;

  // 测试基本运算
  int a = 50, b = 10;
  std::cout << "  " << a << " + " << b << " = " << math->Add(a, b) << std::endl;

  // 必须显式释放
  AxPlug::DestroyTool(math);
  std::cout << "原始指针 Tool 已释放" << std::endl;
}

void testService() {
  std::cout << "\n=== [4] Service 单例测试 ===" << std::endl;
  AX_PROFILE_SCOPE("testService");

  // 获取 Service 单例 (全局唯一)
  // 第一次调用会自动创建
  auto logger = AxPlug::GetService<ILoggerService>();
  if (!logger) {
    std::cout << "LoggerPlugin Service 获取失败" << std::endl;
    return;
  }

  std::cout << "LoggerPlugin Service 获取成功" << std::endl;

  // 使用 Service
  logger->Log(LogLevel::Info, "This is a log message from Plugin System Test");
  logger->Log(LogLevel::Warn, "This is a warning message");
  logger->Log(LogLevel::Error, "This is an error message");

  // 再次获取应该返回同一个实例
  auto logger2 = AxPlug::GetService<ILoggerService>();
  std::cout << "单例一致性检查: " << (logger.get() == logger2.get() ? "通过" : "失败") << std::endl;

  // Service 不需要手动释放，由 PluginManager 管理
  // 但可以显式释放
  AxPlug::ReleaseService<ILoggerService>();
  std::cout << "Service 已显式释放" << std::endl;
}

void testNamedBinding() {
  std::cout << "\n=== [6] 命名绑定测试 ===" << std::endl;
  AX_PROFILE_SCOPE("testNamedBinding");

  // Test 1: CreateTool<ITcpServer>() returns default (winsock) impl
  auto defaultServer = AxPlug::CreateTool<ITcpServer>();
  std::cout << "[1] CreateTool<ITcpServer>(): " << (defaultServer ? "OK" : "FAIL") << std::endl;

  // Test 2: CreateTool<ITcpServer>("boost") returns boost impl
  auto boostServer = AxPlug::CreateTool<ITcpServer>("boost");
  std::cout << "[2] CreateTool<ITcpServer>(\"boost\"): " << (boostServer ? "OK" : "FAIL") << std::endl;

  // Test 3: They should be different objects
  std::cout << "[3] Different objects: " << (defaultServer.get() != boostServer.get() ? "OK" : "FAIL") << std::endl;

  // Test 4: CreateTool<ITcpClient>() returns default
  auto defaultClient = AxPlug::CreateTool<ITcpClient>();
  std::cout << "[4] CreateTool<ITcpClient>(): " << (defaultClient ? "OK" : "FAIL") << std::endl;

  // Test 5: CreateTool<ITcpClient>("boost") returns boost impl
  auto boostClient = AxPlug::CreateTool<ITcpClient>("boost");
  std::cout << "[5] CreateTool<ITcpClient>(\"boost\"): " << (boostClient ? "OK" : "FAIL") << std::endl;

  // Test 6: CreateTool<IUdpSocket>() returns default
  auto defaultUdp = AxPlug::CreateTool<IUdpSocket>();
  std::cout << "[6] CreateTool<IUdpSocket>(): " << (defaultUdp ? "OK" : "FAIL") << std::endl;

  // Test 7: CreateTool<IUdpSocket>("boost") returns boost impl
  auto boostUdp = AxPlug::CreateTool<IUdpSocket>("boost");
  std::cout << "[7] CreateTool<IUdpSocket>(\"boost\"): " << (boostUdp ? "OK" : "FAIL") << std::endl;

  // Test 8: Invalid name returns nullptr
  auto invalid = AxPlug::CreateTool<ITcpServer>("nonexistent");
  std::cout << "[8] CreateTool<ITcpServer>(\"nonexistent\"): " << (invalid == nullptr ? "OK (null as expected)" : "FAIL") << std::endl;

  // Test 9: Boost server actually works (quick listen test)
  if (boostServer) {
    boostServer->SetMaxConnections(5);
    bool ok = boostServer->Listen(19999);
    std::cout << "[9] Boost server Listen(19999): " << (ok ? "OK" : "FAIL") << std::endl;
    if (ok) boostServer->StopListening();
  }

  // Test 10: Default server also works
  if (defaultServer) {
    defaultServer->SetMaxConnections(5);
    bool ok = defaultServer->Listen(19998);
    std::cout << "[10] Default server Listen(19998): " << (ok ? "OK" : "FAIL") << std::endl;
    if (ok) defaultServer->StopListening();
  }

  std::cout << "命名绑定测试完成" << std::endl;
}

void testNewFeatures() {
  std::cout << "\n=== [7] 新特性测试 ===" << std::endl;
  AX_PROFILE_SCOPE("testNewFeatures");

  // Test 1: TryGetService noexcept API
  std::cout << "[1] TryGetService 测试:" << std::endl;
  // 明确指定使用返回 pair 的重载
  auto logger_result = AxPlug::TryGetService<ILoggerService>("test");
  auto logger = logger_result.first;
  auto error = logger_result.second;
  if (error == AxInstanceError::kSuccess && logger) {
    std::cout << "  获取成功" << std::endl;
    logger->Log(LogLevel::Info, "TryGetService 测试日志");
  } else {
    std::cout << "  获取失败: " << AxPlug::GetLastError() << std::endl;
  }

  // Test 2: 接口内省 API
  std::cout << "[2] 接口内省测试:" << std::endl;
  auto implementations = AxPlug::FindImplementations<ITcpServer>();
  std::cout << "  ITcpServer 实现数量: " << implementations.size() << std::endl;
  for (const auto& impl : implementations) {
    std::cout << "    - " << (impl.fileName ? impl.fileName : "N/A") 
              << " (" << (impl.interfaceName ? impl.interfaceName : "N/A") << ")" << std::endl;
  }

  // Test 3: 原子文件写入
  std::cout << "[3] 原子文件写入测试:" << std::endl;
  std::string testContent = "测试内容\n第二行\n";
  bool writeSuccess = AxPlug::OSUtils::AtomicWriteFile("test_atomic.txt", testContent);
  std::cout << "  写入结果: " << (writeSuccess ? "成功" : "失败") << std::endl;

  // Test 4: 编译期安全检查（这些会在编译期被检查）
  std::cout << "[4] 编译期安全检查:" << std::endl;
  std::cout << "  所有模板 API 都包含 static_assert 检查" << std::endl;
  
  // 测试正确的类型使用
  auto math = AxPlug::CreateTool<IMath>();
  std::cout << "  正确类型使用: " << (math ? "通过" : "失败") << std::endl;

  std::cout << "新特性测试完成" << std::endl;
}

void testPerformance() {
  std::cout << "\n=== [8] 性能基准测试 (Hot Path) ===" << std::endl;
  AX_PROFILE_SCOPE("testPerformance");

  // 预热
  {
    auto warmUp = AxPlug::CreateTool<IMath>();
  }

  const int iterations = 1000000; // 100万次调用
  std::cout << "开始性能测试 (" << iterations << " 次 Create/Destroy Tool)..."
            << std::endl;

  auto start = std::chrono::high_resolution_clock::now();

  for (int i = 0; i < iterations; ++i) {
    // 每次创建和销毁，测试 Hot Path (TypeId 哈希查找) 性能
    auto math = AxPlug::CreateTool<IMath>();
    // math 自动销毁
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

  std::cout << "测试完成!" << std::endl;
  std::cout << "总耗时: " << duration.count() << " ms" << std::endl;

  // 防止除以零
  long long count = duration.count();
  double throughput = 0.0;
  if (count > 0) {
    throughput = (double)iterations * 1000.0 / count;
  } else {
    throughput = (double)iterations * 1000.0; // 如果耗时为0，则吞吐量极大
  }

  std::cout << "吞吐量: " << throughput << " ops/sec" << std::endl;
  std::cout << "平均耗时: " << (double)duration.count() * 1000.0 / iterations
            << " us/op" << std::endl;
}

void testPluginSystemInfo() {
  std::cout << "\n=== [1] 插件系统信息 ===" << std::endl;
  int count = AxPlug::GetPluginCount();
  std::cout << "已加载插件数量: " << count << std::endl;

  for (int i = 0; i < count; ++i) {
    printPluginInfo(i);
  }
}

int main() {
  // 设置控制台编码为UTF-8 (Windows)
  SetConsoleOutputCP(65001);
  SetConsoleCP(65001);

  std::cout << "=== AxPlug 插件系统集成测试 ===" << std::endl;

  // 1. 初始化插件系统 (加载插件)
  // 如果不传参数，自动查找当前目录或 exe 目录
  {
    AX_PROFILE_SCOPE("AxPlug::Init");
    AxPlug::Init();
  }

  if (AxPlug::HasError()) {
    std::cout << "初始化错误: " << AxPlug::GetLastError() << std::endl;
    AxPlug::ClearLastError();
  }

  // 开始 Profiler 会话
  AxPlug::ProfilerBegin("AxPlugTestInfo", "plugin_test_trace.json");

  // 2. 打印插件信息
  testPluginSystemInfo();

  // 3. 测试智能指针 Tool
  testSmartPointerTool();

  // 4. 测试原始指针 Tool
  testRawPointerTool();

  // 5. 测试 Service
  testService();

  // 6. 测试命名绑定
  testNamedBinding();

  // 7. 测试新特性
  testNewFeatures();

  // 8. 性能测试
  testPerformance();

  // 结束 Profiler 会话
  AxPlug::ProfilerEnd();

  std::cout << "\n=== 测试全部完成 ===" << std::endl;
  return 0;
}
