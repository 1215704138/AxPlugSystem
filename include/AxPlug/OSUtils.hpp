#pragma once

/**
 * @file OSUtils.hpp
 * @brief 跨平台操作系统工具类 - Header-Only实现
 *
 * 这个文件提供了跨平台的动态库加载和路径操作功能。
 * 作为header-only实现，可以直接包含使用，无需单独编译。
 *
 * @author AxPlug Team
 * @version 1.0
 */

#include <algorithm>
#include <filesystem>
#include <functional>
#include <iostream>
#include <string>

#ifdef _WIN32
#include <shlwapi.h>
#include <windows.h>
#pragma comment(lib, "shlwapi.lib")
#else
#include <dlfcn.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

#endif

namespace fs = std::filesystem;

namespace AxPlug {

// 平台相关的动态库句柄类型
#ifdef _WIN32
using LibraryHandle = HMODULE;
#else
using LibraryHandle = void *;
#endif

// 平台相关的导出宏定义
#ifdef _WIN32
#ifdef AX_CORE_EXPORTS
#define AX_API_EXPORT __declspec(dllexport)
#else
#define AX_API_EXPORT __declspec(dllimport)
#endif
#else
#ifdef AX_CORE_EXPORTS
#define AX_API_EXPORT __attribute__((visibility("default")))
#else
#define AX_API_EXPORT
#endif
#endif

/**
 * @brief 跨平台操作系统工具类 - Header-Only版本
 *
 * 提供统一的接口来处理：
 * 1. 动态库加载和卸载
 * 2. 符号地址获取
 * 3. 路径处理
 * 4. 错误信息获取
 */
class OSUtils {
public:
  /**
   * @brief 加载动态库
   * @param libraryPath 库文件路径
   * @return 库句柄，失败返回nullptr
   */
  static LibraryHandle LoadLibrary(const std::string &libraryPath) {
#ifdef _WIN32
    // Fix 3.2: Proper UTF-8 to UTF-16 conversion via MultiByteToWideChar
    std::wstring widePath = Utf8ToWide(libraryPath);
    HMODULE handle = ::LoadLibraryW(widePath.c_str());
    return handle;
#else
    void *handle = dlopen(libraryPath.c_str(), RTLD_LAZY);
    return handle;
#endif
  }

  /**
   * @brief 卸载动态库
   * @param handle 库句柄
   * @return 是否成功
   */
  static bool UnloadLibrary(LibraryHandle handle) {
    if (!handle)
      return false;

#ifdef _WIN32
    return ::FreeLibrary(handle) != FALSE;
#else
    return dlclose(handle) == 0;
#endif
  }

  /**
   * @brief 获取库中的符号地址
   * @param handle 库句柄
   * @param symbolName 符号名称
   * @return 符号地址，失败返回nullptr
   */
  static void *GetSymbol(LibraryHandle handle, const std::string &symbolName) {
    if (!handle)
      return nullptr;

#ifdef _WIN32
    return ::GetProcAddress(handle, symbolName.c_str());
#else
    return dlsym(handle, symbolName.c_str());
#endif
  }

  /**
   * @brief 获取最后一次操作的错误信息
   * @return 错误信息字符串
   */
  static std::string GetLastError() {
#ifdef _WIN32
    return GetWindowsError(::GetLastError());
#else
    return GetUnixError();
#endif
  }

  /**
   * @brief 获取动态库文件扩展名
   * @return 平台相关的扩展名（Windows: ".dll", Linux/macOS: ".so"）
   */
  static std::string GetLibraryExtension() {
#ifdef _WIN32
    return ".dll";
#else
    return ".so";
#endif
  }

  /**
   * @brief 获取当前模块的路径
   * @return 当前模块的完整路径
   */
  static std::string GetCurrentModulePath() {
#ifdef _WIN32
    wchar_t buffer[32768];
    DWORD length = GetModuleFileNameW(nullptr, buffer, 32768);
    if (length > 0 && length < 32768) {
      return WideToUtf8(std::wstring(buffer, length));
    }
#else
    char buffer[PATH_MAX];
    ssize_t length = readlink("/proc/self/exe", buffer, PATH_MAX - 1);
    if (length != -1) {
      buffer[length] = '\0';
      return std::string(buffer);
    }
#endif
    return "";
  }

  /**
   * @brief 获取当前工作目录
   * @return 当前工作目录路径
   */
  static std::string GetCurrentWorkingDirectory() {
#ifdef _WIN32
    wchar_t buffer[MAX_PATH];
    DWORD length = GetCurrentDirectoryW(MAX_PATH, buffer);
    if (length > 0) {
      return WideToUtf8(std::wstring(buffer, length));
    }
#else
    char buffer[PATH_MAX];
    if (getcwd(buffer, PATH_MAX) != nullptr) {
      return std::string(buffer);
    }
#endif
    return "";
  }

  /**
   * @brief 设置DLL搜索路径（仅Windows有效）
   * @param directory 搜索目录
   * @return 是否成功
   */
  static bool SetLibrarySearchPath(const std::string &directory) {
#ifdef _WIN32
    // Fix 3.2: Proper UTF-8 to UTF-16 conversion via MultiByteToWideChar
    std::wstring wideDir = Utf8ToWide(directory);
    return SetDllDirectoryW(wideDir.c_str()) != FALSE;
#else
    // Linux/macOS 不需要设置特定的库搜索路径
    // 可以通过 LD_LIBRARY_PATH 环境变量或 RPATH/RUNPATH 来控制
    return true;
#endif
  }

  /**
   * @brief 规范化路径（处理路径分隔符）
   * @param path 原始路径
   * @return 规范化后的路径
   */
  static std::string NormalizePath(const std::string &path) {
    if (path.empty())
      return path;

    std::string normalized = path;

#ifdef _WIN32
    // Windows: 将正斜杠转换为反斜杠
    std::replace(normalized.begin(), normalized.end(), '/', '\\');
#else
    // Unix: 将反斜杠转换为正斜杠
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
#endif

    return normalized;
  }

  /**
   * @brief 检查文件是否存在
   * @param filePath 文件路径
   * @return 是否存在
   */
  static bool FileExists(const std::string &filePath) {
    std::error_code ec;
    return fs::exists(filePath, ec);
  }

  /**
   * @brief 获取文件的目录部分
   * @param filePath 文件路径
   * @return 目录路径
   */
  static std::string GetDirectoryPath(const std::string &filePath) {
    std::error_code ec;
    fs::path path(filePath);
    return path.parent_path().string();
  }

  /**
   * @brief 获取文件名部分
   * @param filePath 文件路径
   * @return 文件名
   */
  static std::string GetFileName(const std::string &filePath) {
    std::error_code ec;
    fs::path path(filePath);
    return path.filename().string();
  }

private:
  // 平台特定的内部实现
#ifdef _WIN32
  static std::wstring Utf8ToWide(const std::string &utf8) {
    if (utf8.empty())
      return L"";
    int size_needed = ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.length()), nullptr, 0);
    std::wstring wstrTo(size_needed, 0);
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.length()), &wstrTo[0], size_needed);
    return wstrTo;
  }

  static std::string WideToUtf8(const std::wstring &wide) {
    if (wide.empty())
      return "";
    int size_needed = ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                                            static_cast<int>(wide.length()),
                                            nullptr, 0, nullptr, nullptr);
    std::string strTo(size_needed, 0);
    ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                          static_cast<int>(wide.length()), &strTo[0],
                          size_needed, nullptr, nullptr);
    return strTo;
  }

  static std::string GetWindowsError(DWORD errorCode) {
    if (errorCode == 0)
      return "OK";

    wchar_t *messageBuffer = nullptr;
    DWORD size = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPWSTR)&messageBuffer, 0, nullptr);

    std::string message;
    if (size > 0 && messageBuffer) {
      message = WideToUtf8(messageBuffer);
      // 移除末尾的换行符
      while (!message.empty() &&
             (message.back() == '\r' || message.back() == '\n')) {
        message.pop_back();
      }
    } else {
      message = "Unknown Error (Code: " + std::to_string(errorCode) + ")";
    }

    if (messageBuffer) {
      LocalFree(messageBuffer);
    }

    return message;
  }
#else
  static std::string GetUnixError() {
    const char *error = dlerror();
    return error ? error : "No error";
  }
#endif

  // 禁止实例化
  OSUtils() = delete;
  ~OSUtils() = delete;
  OSUtils(const OSUtils &) = delete;
  OSUtils &operator=(const OSUtils &) = delete;
};

/**
 * @brief RAII风格的动态库管理器 - Header-Only版本
 *
 * 自动管理动态库的生命周期，确保资源正确释放
 */
class LibraryRAII {
public:
  explicit LibraryRAII(const std::string &libraryPath)
      : handle_(nullptr), path_(libraryPath) {
    handle_ = OSUtils::LoadLibrary(libraryPath);
    if (!handle_) {
      std::cerr << "[OSUtils] Failed to load library '" << libraryPath
                << "': " << OSUtils::GetLastError() << std::endl;
    }
  }

  ~LibraryRAII() {
    if (handle_) {
      if (!OSUtils::UnloadLibrary(handle_)) {
        std::cerr << "[OSUtils] Failed to unload library '" << path_
                  << "': " << OSUtils::GetLastError() << std::endl;
      }
    }
  }

  // 禁止拷贝
  LibraryRAII(const LibraryRAII &) = delete;
  LibraryRAII &operator=(const LibraryRAII &) = delete;

  // 允许移动
  LibraryRAII(LibraryRAII &&other) noexcept
      : handle_(other.handle_), path_(std::move(other.path_)) {
    other.handle_ = nullptr;
  }

  LibraryRAII &operator=(LibraryRAII &&other) noexcept {
    if (this != &other) {
      // 释放当前资源
      if (handle_) {
        OSUtils::UnloadLibrary(handle_);
      }

      // 移动资源
      handle_ = other.handle_;
      path_ = std::move(other.path_);
      other.handle_ = nullptr;
    }
    return *this;
  }

  /**
   * @brief 获取库句柄
   * @return 库句柄，可能为nullptr
   */
  LibraryHandle GetHandle() const { return handle_; }

  /**
   * @brief 检查库是否已成功加载
   * @return 是否已加载
   */
  bool IsLoaded() const { return handle_ != nullptr; }

  /**
   * @brief 获取库路径
   * @return 库文件路径
   */
  const std::string &GetPath() const { return path_; }

  /**
   * @brief 获取符号地址
   * @param symbolName 符号名称
   * @return 符号地址，失败返回nullptr
   */
  void *GetSymbol(const std::string &symbolName) const {
    if (!handle_)
      return nullptr;

    void *symbol = OSUtils::GetSymbol(handle_, symbolName);
    if (!symbol) {
      std::cerr << "[OSUtils] Failed to get symbol '" << symbolName
                << "' from library '" << path_
                << "': " << OSUtils::GetLastError() << std::endl;
    }

    return symbol;
  }

private:
  LibraryHandle handle_;
  std::string path_;
};

} // namespace AxPlug
