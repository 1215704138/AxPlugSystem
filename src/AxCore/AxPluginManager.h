#pragma once

#include "AxPlug/IAxObject.h"
#include "AxPlug/AxPluginExport.h"
#include <string>
#include <map>
#include <vector>
#include <mutex>

// Internal plugin module info (one per DLL)
struct PluginModule {
    std::string filePath;
    std::string fileName;
    std::vector<AxPluginInfo> plugins;    // one or more plugins per DLL
    void* handle;
    bool isLoaded;
    std::string errorMessage;

    PluginModule() : handle(nullptr), isLoaded(false) {}
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

    // Tool: create new instance each time
    IAxObject* CreateObject(const char* interfaceName);

    // Service: get or create named singleton
    IAxObject* GetSingleton(const char* interfaceName, const char* serviceName);

    // Release named singleton
    void ReleaseSingleton(const char* interfaceName, const char* serviceName);

    // Release object (call Destroy)
    void ReleaseObject(IAxObject* obj);

    // Query (flat plugin index)
    int GetPluginCount() const;
    const char* GetPluginInterfaceName(int index) const;
    const char* GetPluginFileName(int index) const;
    int GetPluginType(int index) const;
    bool IsPluginLoaded(int index) const;

private:
    AxPluginManager();
    ~AxPluginManager();
    AxPluginManager(const AxPluginManager&) = delete;
    AxPluginManager& operator=(const AxPluginManager&) = delete;

    void LoadOnePlugin(const std::string& path);

    // Plugin registry: interfaceName -> flat plugin index
    std::map<std::string, int> registry_;

    // All loaded modules (one per DLL)
    std::vector<PluginModule> modules_;

    // Flat plugin list for query API
    std::vector<PluginEntry> allPlugins_;

    // Service singleton cache: (interfaceName, serviceName) -> IAxObject*
    std::map<std::pair<std::string, std::string>, IAxObject*> singletonCache_;

    mutable std::recursive_mutex mutex_;
};
