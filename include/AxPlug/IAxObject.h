#pragma once

#include <cstdint>
#include <memory>

// Forward declaration
class AxPluginManager;

// ============================================================
// Compile-time FNV-1a hash for type identification (Hot Path)
// Replaces std::string key lookups with O(1) integer comparison
// ============================================================
constexpr uint64_t AX_FNV_OFFSET = 14695981039346656037ULL;
constexpr uint64_t AX_FNV_PRIME  = 1099511628211ULL;

constexpr uint64_t AxTypeHash(const char* str, uint64_t hash = AX_FNV_OFFSET) {
    return (*str == 0) ? hash : AxTypeHash(str + 1, (hash ^ static_cast<uint64_t>(*str)) * AX_FNV_PRIME);
}

// Runtime version for dynamic string hashing
inline uint64_t AxTypeHashRuntime(const char* str) {
    uint64_t hash = AX_FNV_OFFSET;
    while (*str) {
        hash ^= static_cast<uint64_t>(*str++);
        hash *= AX_FNV_PRIME;
    }
    return hash;
}

// ============================================================
// Smart pointer alias - automatic reference counting
// ============================================================
template<typename T>
using AxPtr = std::shared_ptr<T>;

// ============================================================
// Interface type key macro
// Declares compile-time string literal + uint64 type ID for cross-DLL matching
// Usage: class IMath : public IAxObject { AX_INTERFACE(IMath) public: ... };
// ============================================================
#define AX_INTERFACE(InterfaceName) \
public: \
    static constexpr const char* ax_interface_name = #InterfaceName; \
    static constexpr uint64_t ax_type_id = AxTypeHash(#InterfaceName); \
private:

// Base object interface - all plugin interfaces must inherit from this
class IAxObject {
public:
    virtual ~IAxObject() = default;
    
    // 初始化钩子，系统完成对象的实例化与映射表存入后调用
    virtual void OnInit() {}
    
    // 提给子类重写的安全谢幕钩子，由管理器在析构之前主动调用
    virtual void OnShutdown() {}

protected:
    // Self-destruct interface, only AxPluginManager can call
    virtual void Destroy() = 0;

private:
    friend class AxPluginManager;
};
