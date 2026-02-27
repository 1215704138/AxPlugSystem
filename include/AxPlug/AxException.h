#pragma once

#include <string>
#include <functional>

// ============================================================
// AxException - Cross-module Exception Handling
// Error storage lives in AxCore.dll (canonical thread_local).
// All DLLs route through C API to ensure cross-DLL visibility.
// ============================================================

// AX_CORE_API: dllexport inside AxCore, dllimport everywhere else
#ifndef AX_CORE_API
#ifdef AX_CORE_EXPORTS
#define AX_CORE_API __declspec(dllexport)
#else
#ifdef _WIN32
#define AX_CORE_API __declspec(dllimport)
#else
#define AX_CORE_API
#endif
#endif
#endif

// C API for cross-DLL error handling (implemented in AxCore.dll)
extern "C" {
AX_CORE_API void Ax_SetError(int code, const char* message, const char* source);
AX_CORE_API int Ax_GetErrorCode();
AX_CORE_API const char* Ax_GetLastError();
AX_CORE_API const char* Ax_GetErrorSource();
AX_CORE_API bool Ax_HasErrorState();
AX_CORE_API void Ax_ClearLastError();
}

// Thread-safe error state — routes through AxCore.dll C API for cross-DLL safety
class AxErrorState {
public:
    static void Set(int code, const char* message, const char* source = "") {
        Ax_SetError(code, message ? message : "", source ? source : "");
    }
    static void Clear() { Ax_ClearLastError(); }
    static bool HasError() { return Ax_HasErrorState(); }
    static const char* GetErrorMessage() { return Ax_GetLastError(); }
    static int GetCode() { return Ax_GetErrorCode(); }
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
