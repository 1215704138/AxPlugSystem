#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>


// v2: 只需引入 AxPlug.h 和接口头文件
#include "AxPlug/AxPlug.h"
#include "business/IMath.h"
#include "core/LoggerService.h"

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
  auto *logger = AxPlug::GetService<ILoggerService>();
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
  auto *logger2 = AxPlug::GetService<ILoggerService>();
  std::cout << "单例一致性检查: " << (logger == logger2 ? "通过" : "失败")
            << std::endl;

  // Service 不需要手动释放，由 PluginManager 管理
  // 但可以显式释放
  AxPlug::ReleaseService<ILoggerService>();
  std::cout << "Service 已显式释放" << std::endl;
}

void testPerformance() {
  std::cout << "\n=== [5] 性能基准测试 (Hot Path) ===" << std::endl;
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

  // 6. 性能测试
  testPerformance();

  // 结束 Profiler 会话
  AxPlug::ProfilerEnd();

  std::cout << "\n=== 测试全部完成 ===" << std::endl;
  return 0;
}
