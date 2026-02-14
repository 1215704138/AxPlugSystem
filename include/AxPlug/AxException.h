#pragma once

#include <string>
#include <functional>

// ============================================================
// AxException - Cross-module Exception Handling
// Thread-local error storage + exception guard for safe DLL calls
// ============================================================

// Error info structure
struct AxError {
    int code = 0;            // 0 = no error
    std::string message;
    std::string source;      // which function/module raised the error

    bool HasError() const { return code != 0; }
    void Clear() { code = 0; message.clear(); source.clear(); }
};

// Thread-local error storage
class AxErrorState {
public:
    static AxError& GetThreadLocal() {
        thread_local AxError error;
        return error;
    }

    static void Set(int code, const char* message, const char* source = "") {
        auto& err = GetThreadLocal();
        err.code = code;
        err.message = message ? message : "";
        err.source = source ? source : "";
    }

    static void Clear() {
        GetThreadLocal().Clear();
    }

    static bool HasError() {
        return GetThreadLocal().HasError();
    }

    static const char* GetErrorMessage() {
        return GetThreadLocal().message.c_str();
    }

    static int GetCode() {
        return GetThreadLocal().code;
    }
};

// Exception guard - wraps cross-module calls with try/catch
class AxExceptionGuard {
public:
    // Safe call returning pointer (returns nullptr on exception)
    template<typename Func>
    static auto SafeCallPtr(Func&& func, const char* source = "") -> decltype(func()) {
        AxErrorState::Clear();
        try {
            return func();
        } catch (const std::exception& e) {
            AxErrorState::Set(1, e.what(), source);
            return nullptr;
        } catch (...) {
            AxErrorState::Set(2, "Unknown exception caught in cross-module call", source);
            return nullptr;
        }
    }

    // Safe call returning void
    template<typename Func>
    static void SafeCallVoid(Func&& func, const char* source = "") {
        AxErrorState::Clear();
        try {
            func();
        } catch (const std::exception& e) {
            AxErrorState::Set(1, e.what(), source);
        } catch (...) {
            AxErrorState::Set(2, "Unknown exception caught in cross-module call", source);
        }
    }

    // Safe call returning value with default fallback
    template<typename Func, typename T>
    static auto SafeCallValue(Func&& func, T defaultVal, const char* source = "") -> T {
        AxErrorState::Clear();
        try {
            return func();
        } catch (const std::exception& e) {
            AxErrorState::Set(1, e.what(), source);
            return defaultVal;
        } catch (...) {
            AxErrorState::Set(2, "Unknown exception caught in cross-module call", source);
            return defaultVal;
        }
    }
};

// Error code definitions
namespace AxErrorCode {
    constexpr int None = 0;
    constexpr int StdException = 1;
    constexpr int UnknownException = 2;
    constexpr int PluginNotFound = 100;
    constexpr int PluginNotLoaded = 101;
    constexpr int FactoryFailed = 102;
    constexpr int InvalidArgument = 103;
    constexpr int ServiceNotFound = 104;
}
