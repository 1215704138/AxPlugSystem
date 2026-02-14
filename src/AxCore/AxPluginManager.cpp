#include "AxPluginManager.h"
#include "AxPlug/OSUtils.hpp"
#include <filesystem>
#include <iostream>
#include <algorithm>
#include <cstring>

namespace fs = std::filesystem;

// ============================================================
// AxPluginManager implementation
// ============================================================

AxPluginManager::AxPluginManager() {}

AxPluginManager::~AxPluginManager() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    // Destroy all cached singletons
    for (auto& [key, obj] : singletonCache_) {
        if (obj) obj->Destroy();
    }
    singletonCache_.clear();

    // Unload all plugin DLLs
    for (auto it = modules_.rbegin(); it != modules_.rend(); ++it) {
        if (it->handle) {
            AxPlug::OSUtils::UnloadLibrary(static_cast<AxPlug::LibraryHandle>(it->handle));
        }
    }
    modules_.clear();
    registry_.clear();
}

AxPluginManager* AxPluginManager::Instance() {
    static AxPluginManager instance;
    return &instance;
}

void AxPluginManager::Init(const char* mainAppDir) {
    if (mainAppDir && strlen(mainAppDir) > 0) {
        AxPlug::OSUtils::SetLibrarySearchPath(mainAppDir);
    } else {
        std::string currentPath = AxPlug::OSUtils::GetCurrentModulePath();
        std::string currentDir = AxPlug::OSUtils::GetDirectoryPath(currentPath);
        AxPlug::OSUtils::SetLibrarySearchPath(currentDir);
    }
}

void AxPluginManager::LoadPlugins(const char* directory) {
    if (!directory) return;

    std::error_code ec;
    if (!fs::exists(directory, ec)) return;

    std::string ext = AxPlug::OSUtils::GetLibraryExtension();

    for (const auto& entry : fs::recursive_directory_iterator(directory, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ext) {
            std::string filename = entry.path().filename().u8string();
            // Skip AxCore.dll itself
            if (filename.find("AxCore") != std::string::npos) continue;

            LoadOnePlugin(entry.path().u8string());
        }
    }
}

void AxPluginManager::LoadOnePlugin(const std::string& path) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    // Normalize path and check for duplicates
    std::error_code ec;
    fs::path fsPath = fs::u8path(path);
    fs::path absFsPath = fs::absolute(fsPath, ec);
    if (ec) absFsPath = fsPath;

    std::string finalPath = absFsPath.u8string();
    std::string checkPath = AxPlug::OSUtils::NormalizePath(finalPath);

#ifdef _WIN32
    std::transform(checkPath.begin(), checkPath.end(), checkPath.begin(),
        [](unsigned char c) { return (c >= 'A' && c <= 'Z') ? (c + 32) : c; });
#endif

    // Check if already loaded
    for (const auto& mod : modules_) {
        std::string existingCheck = AxPlug::OSUtils::NormalizePath(mod.filePath);
#ifdef _WIN32
        std::transform(existingCheck.begin(), existingCheck.end(), existingCheck.begin(),
            [](unsigned char c) { return (c >= 'A' && c <= 'Z') ? (c + 32) : c; });
#endif
        if (existingCheck == checkPath) return;
    }

    PluginModule mod;
    mod.filePath = finalPath;
    mod.fileName = absFsPath.filename().u8string();

    // Load DLL
    auto handle = AxPlug::OSUtils::LoadLibrary(finalPath);
    if (!handle) {
        mod.errorMessage = AxPlug::OSUtils::GetLastError();
        modules_.push_back(mod);
        return;
    }

    mod.handle = handle;

    // Try multi-plugin entry point first (GetAxPlugins)
    auto multiFunc = (GetAxPluginsFunc)AxPlug::OSUtils::GetSymbol(handle, AX_PLUGINS_ENTRY_POINT);
    if (multiFunc) {
        int pluginCount = 0;
        const AxPluginInfo* plugins = multiFunc(&pluginCount);
        if (plugins && pluginCount > 0) {
            for (int i = 0; i < pluginCount; i++) {
                mod.plugins.push_back(plugins[i]);
            }
            mod.isLoaded = true;
            mod.errorMessage = "OK";

            int moduleIndex = static_cast<int>(modules_.size());
            for (int i = 0; i < pluginCount; i++) {
                if (mod.plugins[i].interfaceName) {
                    int flatIndex = static_cast<int>(allPlugins_.size());
                    allPlugins_.push_back({ moduleIndex, i });
                    registry_[mod.plugins[i].interfaceName] = flatIndex;
                }
            }
            modules_.push_back(std::move(mod));
            return;
        }
    }

    // Fallback: single-plugin entry point (GetAxPlugin)
    auto singleFunc = (GetAxPluginFunc)AxPlug::OSUtils::GetSymbol(handle, AX_PLUGIN_ENTRY_POINT);
    if (!singleFunc) {
        mod.errorMessage = "Missing GetAxPlugin/GetAxPlugins entry point";
        AxPlug::OSUtils::UnloadLibrary(handle);
        mod.handle = nullptr;
        modules_.push_back(std::move(mod));
        return;
    }

    mod.plugins.push_back(singleFunc());
    mod.isLoaded = true;
    mod.errorMessage = "OK";

    int moduleIndex = static_cast<int>(modules_.size());
    if (mod.plugins[0].interfaceName) {
        int flatIndex = static_cast<int>(allPlugins_.size());
        allPlugins_.push_back({ moduleIndex, 0 });
        registry_[mod.plugins[0].interfaceName] = flatIndex;
    }

    modules_.push_back(std::move(mod));
}

IAxObject* AxPluginManager::CreateObject(const char* interfaceName) {
    if (!interfaceName) return nullptr;

    std::lock_guard<std::recursive_mutex> lock(mutex_);

    auto it = registry_.find(interfaceName);
    if (it == registry_.end()) {
        std::cerr << "[AxPlug] No plugin found for interface: " << interfaceName << std::endl;
        return nullptr;
    }

    const auto& entry = allPlugins_[it->second];
    const auto& mod = modules_[entry.moduleIndex];
    if (!mod.isLoaded) return nullptr;

    const auto& pluginInfo = mod.plugins[entry.pluginIndex];
    if (!pluginInfo.createFunc) return nullptr;

    return pluginInfo.createFunc();
}

IAxObject* AxPluginManager::GetSingleton(const char* interfaceName, const char* serviceName) {
    if (!interfaceName) return nullptr;

    std::lock_guard<std::recursive_mutex> lock(mutex_);

    std::string name = serviceName ? serviceName : "";
    auto key = std::make_pair(std::string(interfaceName), name);

    // Return existing singleton
    auto it = singletonCache_.find(key);
    if (it != singletonCache_.end()) return it->second;

    // Create new instance
    IAxObject* obj = CreateObject(interfaceName);
    if (obj) {
        singletonCache_[key] = obj;
    }
    return obj;
}

void AxPluginManager::ReleaseSingleton(const char* interfaceName, const char* serviceName) {
    if (!interfaceName) return;

    std::lock_guard<std::recursive_mutex> lock(mutex_);

    std::string name = serviceName ? serviceName : "";
    auto key = std::make_pair(std::string(interfaceName), name);

    auto it = singletonCache_.find(key);
    if (it != singletonCache_.end()) {
        if (it->second) it->second->Destroy();
        singletonCache_.erase(it);
    }
}

void AxPluginManager::ReleaseObject(IAxObject* obj) {
    if (obj) obj->Destroy();
}

int AxPluginManager::GetPluginCount() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return static_cast<int>(allPlugins_.size());
}

const char* AxPluginManager::GetPluginInterfaceName(int index) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (index < 0 || index >= static_cast<int>(allPlugins_.size())) return nullptr;
    const auto& entry = allPlugins_[index];
    return modules_[entry.moduleIndex].plugins[entry.pluginIndex].interfaceName;
}

const char* AxPluginManager::GetPluginFileName(int index) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (index < 0 || index >= static_cast<int>(allPlugins_.size())) return nullptr;
    return modules_[allPlugins_[index].moduleIndex].fileName.c_str();
}

int AxPluginManager::GetPluginType(int index) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (index < 0 || index >= static_cast<int>(allPlugins_.size())) return -1;
    const auto& entry = allPlugins_[index];
    return static_cast<int>(modules_[entry.moduleIndex].plugins[entry.pluginIndex].type);
}

bool AxPluginManager::IsPluginLoaded(int index) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (index < 0 || index >= static_cast<int>(allPlugins_.size())) return false;
    return modules_[allPlugins_[index].moduleIndex].isLoaded;
}
