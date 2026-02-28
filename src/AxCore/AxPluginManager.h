#pragma once

#include "AxPlug/IAxObject.h"
#include "AxPlug/AxPluginExport.h"
#include "AxPlug/AxEventBus.h"
#include <string>
#include <memory>
#include <cstdint>

// Forward declaration — full definition in AxPluginManagerImpl.h (Pimpl ABI isolation)
struct AxPluginManagerImpl;

// Plugin manager - singleton, manages all plugin loading and lifecycle
// Uses Pimpl idiom (inspired by z3y) to hide all private data behind
// AxPluginManagerImpl, ensuring internal changes never break ABI.
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

    // Risk 2: Acquire singleton with ref count (prevents UAF on ReleaseSingleton)
    IAxObject* AcquireSingletonById(uint64_t typeId, const char* serviceName);

    // Risk 2: Release external ref acquired by AcquireSingletonById
    void ReleaseSingletonRef(uint64_t typeId, const char* serviceName);

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

    // Event Bus API
    AxPlug::IEventBus* GetEventBus();
    void SetEventBus(AxPlug::IEventBus* externalBus);

    // Error query (now routed through C API directly, see AxCoreDll.cpp)

private:
    AxPluginManager();
    ~AxPluginManager();
    AxPluginManager(const AxPluginManager&) = delete;
    AxPluginManager& operator=(const AxPluginManager&) = delete;

    void LoadOnePlugin(const std::string& path);

    // Internal: create object by typeId (caller must hold at least shared_lock)
    IAxObject* CreateObjectByIdInternal(uint64_t typeId);

    // Release all singletons in reverse creation order
    void ReleaseAllSingletons();

    // Pimpl: all private data members live in AxPluginManagerImpl
    std::unique_ptr<AxPluginManagerImpl> pimpl_;
};
