#include "AxPlug/AxProfiler.h"
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <fstream>
#include <algorithm>

// ============================================================
// AxProfiler — Compiled implementation (lives in AxCore.dll)
// All plugin DLLs route through C API → single canonical instance
// ============================================================

namespace {

struct InternalResult {
    std::string name;
    std::string category;
    long long start;
    long long duration;
    uint32_t threadId;
    uint32_t processId;
};

static std::string EscapeJson(const std::string& s) {
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

class AxProfilerImpl {
public:
    static AxProfilerImpl& Instance() {
        static AxProfilerImpl instance;
        return instance;
    }

    void BeginSession(const char* name, const char* filepath) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (active_) return;
        sessionName_ = name ? name : "AxPlug";
        filepath_ = filepath ? filepath : "trace.json";
        active_ = true;
        resultCount_ = 0;
        results_.clear();
        results_.reserve(kFlushThreshold);
    }

    void EndSession() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!active_) return;
        FlushResultsLocked();
        if (file_.is_open()) {
            file_ << "]}";
            file_.flush();
            file_.close();
        }
        active_ = false;
        resultCount_ = 0;
    }

    void WriteProfile(const AxProfileResult* result) {
        if (!result) return;
        std::lock_guard<std::mutex> lock(mutex_);
        if (!active_) return;
        InternalResult ir;
        ir.name = result->name ? result->name : "";
        ir.category = result->category ? result->category : "";
        ir.start = result->start;
        ir.duration = result->duration;
        ir.threadId = result->threadId;
        ir.processId = result->processId;
        results_.push_back(std::move(ir));
        if (results_.size() >= kFlushThreshold) {
            FlushResultsLocked();
        }
    }

    bool IsActive() const {
        return active_.load(std::memory_order_acquire);
    }

private:
    AxProfilerImpl() {}
    ~AxProfilerImpl() { if (active_) EndSession(); }
    AxProfilerImpl(const AxProfilerImpl&) = delete;
    AxProfilerImpl& operator=(const AxProfilerImpl&) = delete;

    // Flush buffered results to file (caller must hold mutex_)
    void FlushResultsLocked() {
        if (results_.empty()) return;
        if (!file_.is_open()) {
            file_.open(filepath_);
            if (!file_.is_open()) { results_.clear(); return; }
            file_ << "{\"otherData\":{\"session\":\"" << EscapeJson(sessionName_) << "\"},\"traceEvents\":[";
        }
        for (const auto& r : results_) {
            if (resultCount_ > 0) file_ << ",";
            file_ << "{\"cat\":\"" << EscapeJson(r.category) << "\",\"dur\":" << r.duration << ",\"name\":\"" << EscapeJson(r.name) << "\",\"ph\":\"X\",\"pid\":" << r.processId << ",\"tid\":" << r.threadId << ",\"ts\":" << r.start << "}";
            resultCount_++;
        }
        file_.flush();
        results_.clear();
    }

    static constexpr size_t kFlushThreshold = 8192;
    std::mutex mutex_;
    std::string sessionName_;
    std::string filepath_;
    std::ofstream file_;
    std::vector<InternalResult> results_;
    size_t resultCount_ = 0;
    std::atomic<bool> active_{false};
};

} // anonymous namespace

// ============================================================
// C API — exported from AxCore.dll
// ============================================================

#ifdef AX_CORE_EXPORTS
#define AX_CORE_API __declspec(dllexport)
#else
#ifdef _WIN32
#define AX_CORE_API __declspec(dllimport)
#else
#define AX_CORE_API
#endif
#endif

extern "C" {

AX_CORE_API void Ax_ProfilerBeginSession(const char* name, const char* filepath) {
    AxProfilerImpl::Instance().BeginSession(name, filepath);
}

AX_CORE_API void Ax_ProfilerEndSession() {
    AxProfilerImpl::Instance().EndSession();
}

AX_CORE_API void Ax_ProfilerWriteProfile(const AxProfileResult* result) {
    AxProfilerImpl::Instance().WriteProfile(result);
}

AX_CORE_API int Ax_ProfilerIsActive() {
    return AxProfilerImpl::Instance().IsActive() ? 1 : 0;
}

} // extern "C"
