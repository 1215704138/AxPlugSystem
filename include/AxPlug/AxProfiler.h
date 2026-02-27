#pragma once

#include <cstdint>
#include <chrono>
#include <thread>
#include <sstream>
#include <string>
#include <functional>

// ============================================================
// AxProfiler — Cross-DLL Performance Profiler (Fix 1.10)
//
// Implementation lives in AxCore.dll (AxProfiler.cpp).
// All DLLs route through C API → single canonical instance.
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

// POD result struct — ABI-safe (const char*, no std::string)
struct AxProfileResult {
    const char* name;
    const char* category;
    long long start;      // microseconds since epoch
    long long duration;   // microseconds
    uint32_t threadId;
    uint32_t processId;
};

// C API — implemented in AxCore.dll (AxProfiler.cpp)
extern "C" {
AX_CORE_API void Ax_ProfilerBeginSession(const char* name, const char* filepath);
AX_CORE_API void Ax_ProfilerEndSession();
AX_CORE_API void Ax_ProfilerWriteProfile(const AxProfileResult* result);
AX_CORE_API int  Ax_ProfilerIsActive();
}

// RAII scope timer — calls C API, works correctly across DLL boundaries
class AxProfileTimer {
public:
    AxProfileTimer(const char* name, const char* category = "function") : name_(name), category_(category), stopped_(false) {
        startPoint_ = std::chrono::steady_clock::now();
    }

    ~AxProfileTimer() {
        if (!stopped_) Stop();
    }

    void Stop() {
        auto endPoint = std::chrono::steady_clock::now();
        long long start = std::chrono::time_point_cast<std::chrono::microseconds>(startPoint_).time_since_epoch().count();
        long long end = std::chrono::time_point_cast<std::chrono::microseconds>(endPoint).time_since_epoch().count();

        std::ostringstream oss;
        oss << std::this_thread::get_id();
        uint32_t tid = static_cast<uint32_t>(std::hash<std::string>{}(oss.str()));

        AxProfileResult result;
        result.name = name_;
        result.category = category_;
        result.start = start;
        result.duration = end - start;
        result.threadId = tid;
        result.processId = 0;

        Ax_ProfilerWriteProfile(&result);
        stopped_ = true;
    }

private:
    const char* name_;
    const char* category_;
    std::chrono::steady_clock::time_point startPoint_;
    bool stopped_;
};

// ============================================================
// Profiler Macros
// ============================================================

#ifndef AX_PROFILE_ENABLED
    #define AX_PROFILE_ENABLED 1
#endif

#define AX_CONCAT_IMPL(a, b) a##b
#define AX_CONCAT(a, b) AX_CONCAT_IMPL(a, b)

#if AX_PROFILE_ENABLED
    #define AX_PROFILE_BEGIN_SESSION(name, filepath) ::Ax_ProfilerBeginSession(name, filepath)
    #define AX_PROFILE_END_SESSION()                 ::Ax_ProfilerEndSession()
    #define AX_PROFILE_SCOPE(name)                   ::AxProfileTimer AX_CONCAT(ax_profiler_timer_, __LINE__)(name)
    #define AX_PROFILE_FUNCTION()                    AX_PROFILE_SCOPE(__FUNCTION__)
#else
    #define AX_PROFILE_BEGIN_SESSION(name, filepath)
    #define AX_PROFILE_END_SESSION()
    #define AX_PROFILE_SCOPE(name)
    #define AX_PROFILE_FUNCTION()
#endif
