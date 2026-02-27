#pragma once
#include "../AxPlug/IAxObject.h"

// 日志级别枚举
enum class LogLevel {
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warn = 3,
    Error = 4,
    Critical = 5
};

// 日志服务接口 - 纯接口定义，不包含实现细节
class ILoggerService : public IAxObject {
    AX_INTERFACE(ILoggerService)
public:
    // 基础日志记录方法
    virtual void Log(LogLevel level, const char* message) = 0;
    
    // 便捷日志方法
    virtual void Trace(const char* message) = 0;
    virtual void Debug(const char* message) = 0;
    virtual void Info(const char* message) = 0;
    virtual void Warn(const char* message) = 0;
    virtual void Error(const char* message) = 0;
    virtual void Critical(const char* message) = 0;
    
    // Fix 1.16: Variadic methods removed from DLL interface (ABI unsafe).
    // Use Log(level, preFormattedMessage) instead.
    // Variadic convenience wrappers are provided in the implementation class.
    
    // 配置方法
    virtual void SetLevel(LogLevel level) = 0;
    virtual LogLevel GetLevel() const = 0;
    
    // 文件配置
    virtual void SetLogFile(const char* filePath) = 0;
    virtual const char* GetLogFile() const = 0;
    
    // 控制台输出控制
    virtual void EnableConsoleOutput(bool enable) = 0;
    virtual bool IsConsoleOutputEnabled() const = 0;
    
    // 时间戳格式
    virtual void SetTimestampFormat(const char* format) = 0;
    virtual const char* GetTimestampFormat() const = 0;
    
    // 刷新缓冲区
    virtual void Flush() = 0;
    
    // 异步日志控制
    virtual void EnableAsyncLogging(bool enable) = 0;
    virtual bool IsAsyncLoggingEnabled() const = 0;
    
};
