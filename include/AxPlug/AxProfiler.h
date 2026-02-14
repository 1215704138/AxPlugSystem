#pragma once

#include <string>
#include <chrono>
#include <fstream>
#include <mutex>
#include <thread>
#include <sstream>
#include <vector>
#include <algorithm>

// ============================================================
// AxProfiler - Built-in Performance Profiler
// Generates Chrome trace format (trace.json) for chrome://tracing
// ============================================================

struct AxProfileResult {
    std::string name;
    std::string category;
    long long start;      // microseconds since epoch
    long long duration;   // microseconds
    uint32_t threadId;
    uint32_t processId;
};

class AxProfiler {
public:
    static AxProfiler& Instance() {
        static AxProfiler instance;
        return instance;
    }

    void BeginSession(const char* name, const char* filepath = "trace.json") {
        std::lock_guard<std::mutex> lock(mutex_);
        if (active_) return;

        sessionName_ = name ? name : "AxPlug";
        filepath_ = filepath ? filepath : "trace.json";
        active_ = true;
        results_.clear();
        results_.reserve(4096);
    }

    void EndSession() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!active_) return;

        // Write all results to file
        std::ofstream file(filepath_);
        if (file.is_open()) {
            file << "{\"otherData\":{\"session\":\"" << sessionName_ << "\"},\"traceEvents\":[";

            for (size_t i = 0; i < results_.size(); ++i) {
                const auto& r = results_[i];
                if (i > 0) file << ",";
                file << "{"
                     << "\"cat\":\"" << escapeJson(r.category) << "\","
                     << "\"dur\":" << r.duration << ","
                     << "\"name\":\"" << escapeJson(r.name) << "\","
                     << "\"ph\":\"X\","
                     << "\"pid\":" << r.processId << ","
                     << "\"tid\":" << r.threadId << ","
                     << "\"ts\":" << r.start
                     << "}";
            }

            file << "]}";
            file.flush();
        }

        active_ = false;
        results_.clear();
    }

    void WriteProfile(const AxProfileResult& result) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!active_) return;
        results_.push_back(result);
    }

    bool IsActive() const { return active_; }

private:
    AxProfiler() : active_(false) {}
    ~AxProfiler() { if (active_) EndSession(); }
    AxProfiler(const AxProfiler&) = delete;
    AxProfiler& operator=(const AxProfiler&) = delete;

    static std::string escapeJson(const std::string& s) {
        std::string result;
        result.reserve(s.size());
        for (char c : s) {
            switch (c) {
                case '"':  result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\n': result += "\\n";  break;
                case '\t': result += "\\t";  break;
                default:   result += c;      break;
            }
        }
        return result;
    }

    std::mutex mutex_;
    std::string sessionName_;
    std::string filepath_;
    std::vector<AxProfileResult> results_;
    bool active_;
};

// RAII scope timer - automatically records duration on scope exit
class AxProfileTimer {
public:
    AxProfileTimer(const char* name, const char* category = "function")
        : name_(name), category_(category), stopped_(false)
    {
        startPoint_ = std::chrono::steady_clock::now();
    }

    ~AxProfileTimer() {
        if (!stopped_) Stop();
    }

    void Stop() {
        auto endPoint = std::chrono::steady_clock::now();

        long long start = std::chrono::time_point_cast<std::chrono::microseconds>(
            startPoint_).time_since_epoch().count();
        long long end = std::chrono::time_point_cast<std::chrono::microseconds>(
            endPoint).time_since_epoch().count();

        // Get thread ID as uint32
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

        AxProfiler::Instance().WriteProfile(result);
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

// Enable/disable profiler at compile time
#ifndef AX_PROFILE_ENABLED
    #define AX_PROFILE_ENABLED 1
#endif

#if AX_PROFILE_ENABLED
    #define AX_PROFILE_BEGIN_SESSION(name, filepath) ::AxProfiler::Instance().BeginSession(name, filepath)
    #define AX_PROFILE_END_SESSION()                 ::AxProfiler::Instance().EndSession()
    #define AX_PROFILE_SCOPE(name)                   ::AxProfileTimer ax_profiler_timer_##__LINE__(name)
    #define AX_PROFILE_FUNCTION()                    AX_PROFILE_SCOPE(__FUNCTION__)
#else
    #define AX_PROFILE_BEGIN_SESSION(name, filepath)
    #define AX_PROFILE_END_SESSION()
    #define AX_PROFILE_SCOPE(name)
    #define AX_PROFILE_FUNCTION()
#endif
