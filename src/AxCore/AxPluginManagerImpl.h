#pragma once

// ============================================================
// AxPluginManagerImpl - Pimpl (Pointer to Implementation) for AxPluginManager
//
// Inspired by z3y's PluginManagerPimpl pattern for ABI isolation.
// All private data members are hidden here, so changes to internal
// data structures (maps, vectors, etc.) do not affect the public
// header's binary layout. External code never needs to recompile
// when internals change.
// ============================================================

#include "AxPlug/IAxObject.h"
#include "AxPlug/AxPluginExport.h"
#include "AxPlug/AxException.h"
#include "AxPlug/AxEventBus.h"
#include "AxPlug/OSUtils.hpp"
#include <string>
#include <unordered_map>
#include <map>
#include <vector>
#include <deque>
#include <shared_mutex>
#include <memory>
#include <mutex>
#include <atomic>

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
    std::shared_ptr<IAxObject> instance;  // Risk 2: shared_ptr prevents UAF when external refs exist
    std::exception_ptr e_ptr;     // 构造失败时缓存异常
    std::atomic<int> externalRefs{0};    // Risk 2: external reference count
    std::atomic<bool> pendingRelease{false}; // Risk 2: deferred destruction flag
};

// Flat index entry: maps a global plugin index to (module, plugin-within-module)
struct PluginEntry {
    int moduleIndex;
    int pluginIndex;
};

// Pimpl struct: all private data of AxPluginManager lives here
struct AxPluginManagerImpl {
    // Plugin registry: typeId -> flat plugin index (O(1) lookup, default impl)
    std::unordered_map<uint64_t, int> registry_;

    // Named implementation registry: (typeId, implName) -> flat plugin index
    std::map<std::pair<uint64_t, std::string>, int> namedImplRegistry_;

    // String-to-typeId map (for string-based API)
    std::unordered_map<std::string, uint64_t> nameToTypeId_;

    // All loaded modules (one per DLL) - deque guarantees element address stability on push_back
    std::deque<PluginModule> modules_;

    // Flat plugin list for query API
    std::vector<PluginEntry> allPlugins_;

    // Service singleton cache: typeId -> SingletonHolder (default/unnamed singletons, hot path)
    // Risk 1: Use shared_ptr<SingletonHolder> so holder survives map erasure during concurrent access
    std::unordered_map<uint64_t, std::shared_ptr<SingletonHolder>> defaultSingletonHolders_;

    // Named service singleton cache: (typeId, name) -> SingletonHolder
    std::map<std::pair<uint64_t, std::string>, std::shared_ptr<SingletonHolder>> namedSingletonHolders_;

    // LIFO shutdown stack: tracks singleton creation order for safe reverse teardown
    std::vector<std::shared_ptr<IAxObject>> shutdownStack_;

    // Read-write lock: shared_lock for reads, unique_lock for writes
    mutable std::shared_mutex mutex_;

    // Tracks directories already scanned to avoid redundant I/O
    std::vector<std::string> scannedDirs_;

    // Shutdown guard: prevents new singleton creation during teardown
    std::atomic<bool> isShuttingDown_{false};

    // Event bus: owned default + optional external override
    std::unique_ptr<AxPlug::IEventBus> defaultEventBus_;
    AxPlug::IEventBus* externalEventBus_ = nullptr;
};
