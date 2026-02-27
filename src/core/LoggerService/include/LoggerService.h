#pragma once
#include "core/LoggerService.h"
#include "AxPlug/AxPluginExport.h"
#include <fstream>
#include <mutex>
#include <string>
#include <memory>
#include <ctime>
#include <deque>
#include <thread>
#include <condition_variable>
#include <atomic>

// 日志消息结构 - 私有实现，不暴露给用户
struct LogMessage {
    LogLevel level;
    std::string message;
    std::string timestamp;
    
    LogMessage(LogLevel lvl, const std::string& msg, const std::string& ts)
        : level(lvl), message(msg), timestamp(ts) {}
};

class LoggerService : public ILoggerService {
public:
    // 接口类型别名 - 用于类型指纹系统
    using InterfaceType = ILoggerService;
    
private:
    LogLevel currentLevel_;
    std::string logFilePath_;
    bool consoleOutputEnabled_;
    std::string timestampFormat_;
    mutable std::mutex mutex_;
    std::ofstream logFile_;
    bool fileOpen_;
    
    std::atomic<bool> asyncEnabled_;
    std::deque<LogMessage> logQueue_;
    std::mutex queueMutex_;
    std::condition_variable queueCondition_;
    std::thread logThread_;
    std::atomic<bool> stopFlag_;
    std::mutex asyncStateMutex_;  // Fix 2.1: Protects EnableAsyncLogging Check-Then-Act

    std::string getCurrentTimestamp() const;
    std::string getLevelString(LogLevel level) const;
    void formatLogMessage(LogLevel level, const std::string& message, std::string& formattedMessage);
    void writeToFile(const std::string& message);
    void writeToConsole(const std::string& message);
    void asyncLogWorker();
    void processLogMessage(const LogMessage& message);

public:
    LoggerService();
    ~LoggerService();

    // 基础日志记录方法
    virtual void Log(LogLevel level, const char* message) override;
    
    // 便捷日志方法
    virtual void Trace(const char* message) override;
    virtual void Debug(const char* message) override;
    virtual void Info(const char* message) override;
    virtual void Warn(const char* message) override;
    virtual void Error(const char* message) override;
    virtual void Critical(const char* message) override;
    
    // 格式化日志方法 (non-virtual, implementation-only convenience)
    void LogFormat(LogLevel level, const char* format, ...);
    void InfoFormat(const char* format, ...);
    void ErrorFormat(const char* format, ...);
    
    // 配置方法
    virtual void SetLevel(LogLevel level) override;
    virtual LogLevel GetLevel() const override;
    
    // 文件配置
    virtual void SetLogFile(const char* filePath) override;
    virtual const char* GetLogFile() const override;
    
    // 控制台输出控制
    virtual void EnableConsoleOutput(bool enable) override;
    virtual bool IsConsoleOutputEnabled() const override;
    
    // 时间戳格式
    virtual void SetTimestampFormat(const char* format) override;
    virtual const char* GetTimestampFormat() const override;
    
    // 刷新缓冲区
    virtual void Flush() override;
    
    // 异步日志控制
    virtual void EnableAsyncLogging(bool enable) override;
    virtual bool IsAsyncLoggingEnabled() const override;
    
protected:
    void Destroy() override { delete this; }
};
