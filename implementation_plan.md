# 插件系统缺陷修复实施方案 (Implementation Plan)

本方案详细记录了在 `AxCore` 模块的 `AxPluginManager` 中修复对象生命周期与内存安全相关的 4 个具体风险的实施步骤。

## 修复目标
本次修复的目的是堵住 `AxPluginManager` 在对象生命周期管理上的漏洞，消除潜在的内存泄漏、修复异常安全问题、解决 API 中存在的局部静态指针覆盖陷阱，并从机制上确保在所有正常与非正常流转情况下，各个对象的 `OnShutdown()` 钩子都能被正确并且仅被正确调用一次。

## 拟定修改点

### AxCore 组件
#### [MODIFY] [AxPluginManager.h](file:///c:/Users/22610/Desktop/AxPlugSystem/src/AxCore/AxPluginManager.h)
- 引入 `<atomic>` 与 `<deque>` 头文件。
- 添加原子的停机状态标志 `std::atomic<bool> isShuttingDown_{false};`，用于拦截关机过程中的所有新对象创建请求，防止重入。
- 将原本存放 DLL 模块信息的 `std::vector<PluginModule> modules_;` 改为 **`std::deque<PluginModule> modules_;`**。这是因为通过 `GetPluginFileName` 返回的 `c_str()` 指针，只有在宿主容器不发生内部数据重分配（Reallocation）移动时才安全，`deque` 能保证元素内存地址的绝对稳定。

#### [MODIFY] [AxPluginManager.cpp](file:///c:/Users/22610/Desktop/AxPlugSystem/src/AxCore/AxPluginManager.cpp)
- **修复 1：拦截停机期间的对象重入与内存泄漏**
  - 在 `ReleaseAllSingletons()` 开头增加 `isShuttingDown_ = true;` 的赋值动作。
  - 在 `GetSingletonById()` 的入口增加防护拦截：`if (isShuttingDown_) return nullptr;`。这彻底杜绝任何 Service 在它的 `OnShutdown` 销毁流程里再次拉起新的 Service 形成僵尸漏斗的问题。

- **修复 2：修补手动释放服务的 `OnShutdown` 旁路漏洞**
  - 在 `ReleaseSingletonById()` 中不能只是简简单单地 `erase()` 从 `shutdownStack_` 中剔除元素。
  - 必须将其提取出来 `std::shared_ptr<IAxObject> instanceToRelease = std::move(*it);` 然后再 `erase()`。
  - 在当前线程释放完 `mutex_` 互斥锁之后，主动安全地调用 `instanceToRelease->OnShutdown();`，这样就能保证它在析构被彻底销毁前，必定发生一次临终钩子调用。

- **修复 3：修补 `OnInit` 失败残留的僵尸对象问题**
  - 在 `GetSingletonById()` 中使用 `std::call_once` 初始化单例失败捕获异常（`catch (...)`）的逻辑分支中，当前仅置空了 `holder->instance`，导致先一步放入栈中的 `shared_ptr` 残留悬空。
  - 加入相应的清理逻辑：如果在被保护的初始化块内捕获到任意异常，需要持有锁立即从 `shutdownStack_` 中精确弹出/移除刚才放进去的那一个僵尸 `shared_ptr`。

- **修复 4：修复 `GetPluginFileName` 的指针重叠问题**
  - 完全移除 `GetPluginFileName()` 内部使用的线程局部变量 `thread_local std::string snapshot;`。
  - 直接安全地按 `modules_` 索引查询并且返回 `modules_[...].fileName.c_str();`（基于在头文件中将容器修改为 `deque` 的前提，这里的裸指针提取和外部传阅是完全线程安全和生命周期安全的）。

### Core 插件组件 (LoggerService)
#### [MODIFY] [LoggerService.cpp](file:///c:/Users/22610/Desktop/AxPlugSystem/src/core/LoggerService/src/LoggerService.cpp)
- **修复 5：修补 `OnShutdown` 中的死锁漏洞**
  - 在 `OnShutdown()` 中的 `logThread_.join()` 前缺少了对停止信号和条件变量的触发。如果启用了异步日志操作并在主程序关闭时被调用，将会永远阻塞（Deadlock）。
  - 需要在 `logThread_.join()` 前加上与析构函数中相同的停止逻辑：获取锁，设置 `stopFlag_ = true;`，并调用 `queueCondition_.notify_all();` 唤醒线程。
- **修复 6：修补 `InfoFormat` 日志静默截断漏洞**
  - `InfoFormat` 现在如果格式化输出超过了 1024 字节就会被直接静默截断输出，不同于 `LogFormat` 中的正确处理。
  - 修改 `InfoFormat` 的逻辑，像 `LogFormat` 和 `ErrorFormat` 那样，如果在初次尝试组装（`vsnprintf`）返回所需大小超出了静态缓冲区大小，则利用 `std::vector<char>` 从堆上分配足够大的动态内存进行第二次完整保留的格式化组装并写入。

## 验证计划

### 自动化测试
- **全面重编译**: 用 CMake 在原本的 `build` 或相关构建目录下重新编译整个系统，验证是否由于 API 参数类型变更（如 `vector` 到 `deque`）引发潜在的编译拦截。
- **单元测试回归**: 在测试目录下重新执行已有的 CTest 测试集（`ctest -V`）。确保生命周期核心对象加载、调用和释放依然正常。

### 此修复无需用户手动验证
- 这是一份偏向底层的内存调度与生命周期修复，只要自动化测试正常通过、静态分析不再报生命周期未配对警告，即可宣告修复完成。
