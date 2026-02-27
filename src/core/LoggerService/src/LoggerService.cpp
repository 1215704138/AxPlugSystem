#include "../include/LoggerService.h"
#include "AxPlug/OSUtils.hpp"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <filesystem> // Added for std::filesystem
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>


LoggerService::LoggerService()
    : currentLevel_(LogLevel::Info), consoleOutputEnabled_(true),
      timestampFormat_("default"), fileOpen_(false), asyncEnabled_(false),
      stopFlag_(false) {
  // Determine log directory relative to executable
  std::string exePath = AxPlug::OSUtils::GetCurrentModulePath();
  std::string exeDir = AxPlug::OSUtils::GetDirectoryPath(exePath);

  // Fallback to current directory if retrieval fails
  if (exeDir.empty()) {
    exeDir = ".";
  }

  std::string logDir = exeDir + "/logs";

  // Ensure logs directory exists
  std::error_code ec;
  std::filesystem::create_directories(logDir, ec);

  logFilePath_ = logDir + "/app.log";
}

LoggerService::~LoggerService() {
  // 停止异步日志线程
  if (asyncEnabled_ && logThread_.joinable()) {
    {
      std::lock_guard<std::mutex> lock(queueMutex_);
      stopFlag_ = true;
    }
    queueCondition_.notify_all();
    logThread_.join();
  }

  if (logFile_.is_open()) {
    logFile_.close();
  }
}

std::string LoggerService::getCurrentTimestamp() const {
  auto now = std::time(nullptr);
  struct tm tm;
  localtime_s(&tm, &now);  // Fix 2.7: localtime_s is thread-safe (Windows)

  std::ostringstream oss;
  if (timestampFormat_ == "iso") {
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
  } else if (timestampFormat_ == "simple") {
    oss << std::put_time(&tm, "%H:%M:%S");
  } else {
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
  }
  return oss.str();
}

std::string LoggerService::getLevelString(LogLevel level) const {
  switch (level) {
  case LogLevel::Trace:
    return "TRACE";
  case LogLevel::Debug:
    return "DEBUG";
  case LogLevel::Info:
    return "INFO ";
  case LogLevel::Warn:
    return "WARN ";
  case LogLevel::Error:
    return "ERROR";
  case LogLevel::Critical:
    return "CRIT ";
  default:
    return "UNKN ";
  }
}

void LoggerService::formatLogMessage(LogLevel level, const std::string &message,
                                     std::string &formattedMessage) {
  std::ostringstream oss;
  oss << "[" << getCurrentTimestamp() << "] "
      << "[" << getLevelString(level) << "] " << message;
  formattedMessage = oss.str();
}

void LoggerService::writeToFile(const std::string &message) {
  if (!logFilePath_.empty()) {
    if (!fileOpen_ || !logFile_.is_open()) {
      if (logFile_.is_open()) {
        logFile_.close();
      }
      logFile_.open(logFilePath_, std::ios::app);
      fileOpen_ = logFile_.is_open();
    }

    if (logFile_.is_open()) {
      logFile_ << message << std::endl;
      logFile_.flush();
    }
  }
}

void LoggerService::writeToConsole(const std::string &message) {
  if (consoleOutputEnabled_) {
    // 使用 \n 代替 std::endl，减少控制台锁的竞争
    std::cout << message << "\n";
    std::cout.flush(); // 手动刷新
  }
}

void LoggerService::Log(LogLevel level, const char *message) {
  if (level < currentLevel_) {
    return;
  }

  if (asyncEnabled_) {
    // 异步模式：将日志消息放入队列
    LogMessage logMsg(level, message ? message : "", getCurrentTimestamp());
    {
      std::lock_guard<std::mutex> lock(queueMutex_);
      logQueue_.push_back(std::move(logMsg));
    }
    queueCondition_.notify_one();
  } else {
    // 同步模式：必须加锁保护 logFile_ 和 console
    std::lock_guard<std::mutex> lock(mutex_);
    std::string formattedMessage;
    formatLogMessage(level, message ? message : "", formattedMessage);
    writeToFile(formattedMessage);
    writeToConsole(formattedMessage);
  }
}

void LoggerService::Trace(const char *message) {
  Log(LogLevel::Trace, message);
}

void LoggerService::Debug(const char *message) {
  Log(LogLevel::Debug, message);
}

void LoggerService::Info(const char *message) { Log(LogLevel::Info, message); }

void LoggerService::Warn(const char *message) { Log(LogLevel::Warn, message); }

void LoggerService::Error(const char *message) {
  Log(LogLevel::Error, message);
}

void LoggerService::Critical(const char *message) {
  Log(LogLevel::Critical, message);
}

void LoggerService::LogFormat(LogLevel level, const char *format, ...) {
  if (level < currentLevel_) {
    return;
  }

  va_list args;
  va_start(args, format);
  va_list argsCopy;
  va_copy(argsCopy, args);

  char buffer[1024];
  int ret = vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  // Fix 3.15: If truncated, dynamically allocate a larger buffer
  if (ret >= static_cast<int>(sizeof(buffer))) {
    std::vector<char> dynBuf(ret + 1);
    vsnprintf(dynBuf.data(), dynBuf.size(), format, argsCopy);
    va_end(argsCopy);
    Log(level, dynBuf.data());
  } else {
    va_end(argsCopy);
    if (ret > 0) Log(level, buffer);
  }
}

void LoggerService::InfoFormat(const char *format, ...) {
  if (LogLevel::Info < currentLevel_)
    return;

  va_list args;
  va_start(args, format);

  // 使用更安全的初始长度，并确保 va_end 总是被执行
  char buffer[1024] = {0};
  int ret = vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  if (ret > 0) {
    this->Info(buffer);
  }
}

void LoggerService::ErrorFormat(const char *format, ...) {
  va_list args;
  va_start(args, format);
  va_list argsCopy;
  va_copy(argsCopy, args);

  char buffer[1024];
  int ret = vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  // Fix 3.15: If truncated, dynamically allocate a larger buffer
  if (ret >= static_cast<int>(sizeof(buffer))) {
    std::vector<char> dynBuf(ret + 1);
    vsnprintf(dynBuf.data(), dynBuf.size(), format, argsCopy);
    va_end(argsCopy);
    Error(dynBuf.data());
  } else {
    va_end(argsCopy);
    Error(buffer);
  }
}

void LoggerService::SetLevel(LogLevel level) {
  std::lock_guard<std::mutex> lock(mutex_);
  currentLevel_ = level;
}

LogLevel LoggerService::GetLevel() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return currentLevel_;
}

void LoggerService::SetLogFile(const char *filePath) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (logFile_.is_open()) {
    logFile_.close();
  }

  logFilePath_ = filePath ? filePath : "";
  fileOpen_ = false;
}

const char *LoggerService::GetLogFile() const {
  std::lock_guard<std::mutex> lock(mutex_);
  thread_local std::string snapshot;
  snapshot = logFilePath_;
  return snapshot.c_str();
}

void LoggerService::EnableConsoleOutput(bool enable) {
  std::lock_guard<std::mutex> lock(mutex_);
  consoleOutputEnabled_ = enable;
}

bool LoggerService::IsConsoleOutputEnabled() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return consoleOutputEnabled_;
}

void LoggerService::SetTimestampFormat(const char *format) {
  std::lock_guard<std::mutex> lock(mutex_);
  timestampFormat_ = format ? format : "";
}

const char *LoggerService::GetTimestampFormat() const {
  std::lock_guard<std::mutex> lock(mutex_);
  thread_local std::string snapshot;
  snapshot = timestampFormat_;
  return snapshot.c_str();
}

void LoggerService::Flush() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (logFile_.is_open()) {
    logFile_.flush();
  }
}

// 异步日志实现
void LoggerService::EnableAsyncLogging(bool enable) {
  std::lock_guard<std::mutex> asyncLock(asyncStateMutex_);  // Fix 2.1: Protect Check-Then-Act
  if (enable && !asyncEnabled_) {
    asyncEnabled_ = true;
    stopFlag_ = false;
    logThread_ = std::thread(&LoggerService::asyncLogWorker, this);
  } else if (!enable && asyncEnabled_) {
    asyncEnabled_ = false;
    if (logThread_.joinable()) {
      {
        std::lock_guard<std::mutex> lock(queueMutex_);
        stopFlag_ = true;
      }
      queueCondition_.notify_all();
      logThread_.join();
    }
    // 处理剩余的日志消息
    std::lock_guard<std::mutex> lock(queueMutex_);
    while (!logQueue_.empty()) {
      processLogMessage(logQueue_.front());
      logQueue_.pop_front();
    }
  }
}

bool LoggerService::IsAsyncLoggingEnabled() const { return asyncEnabled_; }

void LoggerService::asyncLogWorker() {
  while (true) {
    std::unique_lock<std::mutex> lock(queueMutex_);

    // 等待日志消息或停止信号
    queueCondition_.wait(lock,
                         [this] { return !logQueue_.empty() || stopFlag_; });

    // 检查是否需要停止
    if (stopFlag_ && logQueue_.empty()) {
      break;
    }

    // 处理所有待处理的日志消息
    while (!logQueue_.empty()) {
      LogMessage msg = std::move(logQueue_.front());
      logQueue_.pop_front();
      lock.unlock(); // 解锁以避免在处理I/O时阻塞其他线程

      processLogMessage(msg);

      lock.lock(); // 重新锁定以检查队列状态
    }
  }
}

void LoggerService::processLogMessage(const LogMessage &message) {
  std::lock_guard<std::mutex> lock(mutex_);  // Fix 2.2/2.3: Protect file I/O against concurrent SetLogFile()
  std::string formattedMessage;
  formatLogMessage(message.level, message.message, formattedMessage);
  writeToFile(formattedMessage);
  writeToConsole(formattedMessage);
}
