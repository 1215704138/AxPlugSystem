#pragma once

#include "AxPlug/IAxObject.h"
#include "AxPlug/AxPluginExport.h"
#include "AxPlug/AxException.h"
#include "AxPlug/OSUtils.hpp"
#include <string>
#include <unordered_map>
#include <map>
#include <vector>
#include <shared_mutex>
#include <memory>
#include <mutex>
#include <algorithm>

// Internal plugin module info (one per DLL)
struct PluginModule {
    std::string filePath;
    std::string fileName;
    std::vector<AxPluginInfo> plugins;    // one or more plugins per DLL
    AxPlug::LibraryHandle handle;
    bool isLoaded;
    std::string errorMessage;

    PluginModule() : handle(AxPlug::LibraryHandle()), isLoaded(false) {}
};

// Singleton holder for lock-free initialization
struct SingletonHolder {
    std::once_flag   flag;       // 保证只执行一次
    IAxObject*       instance = nullptr;
    std::exception_ptr e_ptr;     // 构造失败时缓存异常
};

// Flat index entry: maps a global plugin index to (module, plugin-within-module)
struct PluginEntry {
    int moduleIndex;
    int pluginIndex;
};

// Plugin manager - singleton, manages all plugin loading and lifecycle
class AxPluginManager {
public:
    static AxPluginManager* Instance();

    // Initialize (set DLL search path)
    void Init(const char* mainAppDir);

    // Load all plugin DLLs from directory
    void LoadPlugins(const char* directory);

    // Tool: create new instance each time (string-based)
    IAxObject* CreateObject(const char* interfaceName);

    // Tool: create new instance (typeId-based)
    IAxObject* CreateObjectById(uint64_t typeId);

    // Tool: create new instance by typeId + implementation name (named binding)
    IAxObject* CreateObjectByIdNamed(uint64_t typeId, const char* implName);

    // Service: get or create named singleton (string-based)
    IAxObject* GetSingleton(const char* interfaceName, const char* serviceName);

    // Service: get or create named singleton (typeId-based)
    IAxObject* GetSingletonById(uint64_t typeId, const char* serviceName);

    // Release named singleton (string-based)
    void ReleaseSingleton(const char* interfaceName, const char* serviceName);

    // Release named singleton (typeId-based)
    void ReleaseSingletonById(uint64_t typeId, const char* serviceName);

    // Release object (call Destroy)
    void ReleaseObject(IAxObject* obj);

    // Query (flat plugin index)
    int GetPluginCount() const;
    const char* GetPluginInterfaceName(int index) const;
    const char* GetPluginFileName(int index) const;
    int GetPluginType(int index) const;
    bool IsPluginLoaded(int index) const;

    // Introspection API
    int FindPluginsByTypeId(uint64_t typeId, int* outIndices, int maxCount);

    // Error query (now routed through C API directly, see AxCoreDll.cpp)

private:
    AxPluginManager();
    ~AxPluginManager();
    AxPluginManager(const AxPluginManager&) = delete;
    AxPluginManager& operator=(const AxPluginManager&) = delete;

    void LoadOnePlugin(const std::string& path);

    // Internal: create object by typeId (caller must hold at least shared_lock)
    IAxObject* CreateObjectByIdInternal(uint64_t typeId);

    // Plugin registry: typeId -> flat plugin index (O(1) lookup, default impl)
    std::unordered_map<uint64_t, int> registry_;

    // Named implementation registry: (typeId, implName) -> flat plugin index
    std::map<std::pair<uint64_t, std::string>, int> namedImplRegistry_;

    // String-to-typeId map (for string-based API)
    std::unordered_map<std::string, uint64_t> nameToTypeId_;

    // All loaded modules (one per DLL)
    std::vector<PluginModule> modules_;

    // Flat plugin list for query API
    std::vector<PluginEntry> allPlugins_;

    // Service singleton cache: typeId -> SingletonHolder (default/unnamed singletons, hot path)
    std::unordered_map<uint64_t, SingletonHolder> defaultSingletonHolders_;

    // Named service singleton cache: (typeId, name) -> SingletonHolder
    std::map<std::pair<uint64_t, std::string>, SingletonHolder> namedSingletonHolders_;

    // LIFO shutdown stack: tracks singleton creation order for safe reverse teardown
    std::vector<std::shared_ptr<IAxObject>> shutdownStack_;

    // Read-write lock: shared_lock for reads, unique_lock for writes
    mutable std::shared_mutex mutex_;

    // Tracks directories already scanned to avoid redundant I/O
    std::vector<std::string> scannedDirs_;

    // Release all singletons in reverse creation order
    void ReleaseAllSingletons();
};
