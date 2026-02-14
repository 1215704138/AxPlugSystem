#include "AxPluginManager.h"
#include "AxPlug/AxProfiler.h"
#include "AxPlug/OSUtils.hpp"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iostream>


namespace fs = std::filesystem;

// ============================================================
// AxPluginManager v3 implementation
// Features: shared_mutex, typeId hot path, shared_ptr cache,
//           exception handling, profiler integration
// ============================================================

AxPluginManager::AxPluginManager() {}

AxPluginManager::~AxPluginManager() {
  std::unique_lock<std::shared_mutex> lock(mutex_);

  // Release all cached singletons (shared_ptr handles Destroy via custom
  // deleter)
  defaultSingletons_.clear();
  namedSingletons_.clear();

  // Unload all plugin DLLs
  for (auto it = modules_.rbegin(); it != modules_.rend(); ++it) {
    if (it->handle) {
      AxPlug::OSUtils::UnloadLibrary(
          static_cast<AxPlug::LibraryHandle>(it->handle));
    }
  }
  modules_.clear();
  registry_.clear();
  nameToTypeId_.clear();
}

AxPluginManager *AxPluginManager::Instance() {
  static AxPluginManager instance;
  return &instance;
}

void AxPluginManager::Init(const char *mainAppDir) {
  AX_PROFILE_FUNCTION();
  if (mainAppDir && strlen(mainAppDir) > 0) {
    AxPlug::OSUtils::SetLibrarySearchPath(mainAppDir);
  } else {
    std::string currentPath = AxPlug::OSUtils::GetCurrentModulePath();
    std::string currentDir = AxPlug::OSUtils::GetDirectoryPath(currentPath);
    AxPlug::OSUtils::SetLibrarySearchPath(currentDir);
  }
}

void AxPluginManager::LoadPlugins(const char *directory) {
  AX_PROFILE_FUNCTION();
  if (!directory)
    return;

  std::error_code ec;
  if (!fs::exists(directory, ec))
    return;

  std::string ext = AxPlug::OSUtils::GetLibraryExtension();

  for (const auto &entry : fs::recursive_directory_iterator(directory, ec)) {
    if (entry.is_regular_file() && entry.path().extension() == ext) {
      std::string filename = entry.path().filename().u8string();
      // Skip AxCore.dll itself
      if (filename.find("AxCore") != std::string::npos)
        continue;

      LoadOnePlugin(entry.path().u8string());
    }
  }
}

void AxPluginManager::LoadOnePlugin(const std::string &path) {
  std::unique_lock<std::shared_mutex> lock(mutex_);

  // Normalize path and check for duplicates
  std::error_code ec;
  fs::path fsPath = fs::u8path(path);
  fs::path absFsPath = fs::absolute(fsPath, ec);
  if (ec)
    absFsPath = fsPath;

  std::string finalPath = absFsPath.u8string();
  std::string checkPath = AxPlug::OSUtils::NormalizePath(finalPath);

#ifdef _WIN32
  std::transform(
      checkPath.begin(), checkPath.end(), checkPath.begin(),
      [](unsigned char c) { return (c >= 'A' && c <= 'Z') ? (c + 32) : c; });
#endif

  // Check if already loaded
  for (const auto &mod : modules_) {
    std::string existingCheck = AxPlug::OSUtils::NormalizePath(mod.filePath);
#ifdef _WIN32
    std::transform(
        existingCheck.begin(), existingCheck.end(), existingCheck.begin(),
        [](unsigned char c) { return (c >= 'A' && c <= 'Z') ? (c + 32) : c; });
#endif
    if (existingCheck == checkPath)
      return;
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

  // Helper lambda: register a plugin into both registries
  auto registerPlugin = [&](const AxPluginInfo &info, int moduleIndex,
                            int pluginIndex) {
    if (!info.interfaceName)
      return;
    int flatIndex = static_cast<int>(allPlugins_.size());
    allPlugins_.push_back({moduleIndex, pluginIndex});
    registry_[info.typeId] = flatIndex;
    nameToTypeId_[info.interfaceName] = info.typeId;
  };

  // Try multi-plugin entry point first (GetAxPlugins)
  auto multiFunc = (GetAxPluginsFunc)AxPlug::OSUtils::GetSymbol(
      handle, AX_PLUGINS_ENTRY_POINT);
  if (multiFunc) {
    int pluginCount = 0;
    const AxPluginInfo *plugins = multiFunc(&pluginCount);
    if (plugins && pluginCount > 0) {
      for (int i = 0; i < pluginCount; i++) {
        mod.plugins.push_back(plugins[i]);
      }
      mod.isLoaded = true;
      mod.errorMessage = "OK";

      int moduleIndex = static_cast<int>(modules_.size());
      for (int i = 0; i < pluginCount; i++) {
        registerPlugin(mod.plugins[i], moduleIndex, i);
      }
      modules_.push_back(std::move(mod));
      return;
    }
  }

  // Fallback: single-plugin entry point (GetAxPlugin)
  auto singleFunc = (GetAxPluginFunc)AxPlug::OSUtils::GetSymbol(
      handle, AX_PLUGIN_ENTRY_POINT);
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
  registerPlugin(mod.plugins[0], moduleIndex, 0);

  modules_.push_back(std::move(mod));
}

// Internal: create object by typeId (caller must hold at least shared_lock)
IAxObject *AxPluginManager::CreateObjectByIdInternal(uint64_t typeId) {
  auto it = registry_.find(typeId);
  if (it == registry_.end()) {
    AxErrorState::Set(AxErrorCode::PluginNotFound,
                      "No plugin found for the given typeId", "CreateObject");
    return nullptr;
  }

  const auto &entry = allPlugins_[it->second];
  const auto &mod = modules_[entry.moduleIndex];
  if (!mod.isLoaded) {
    AxErrorState::Set(AxErrorCode::PluginNotLoaded,
                      "Plugin module is not loaded", "CreateObject");
    return nullptr;
  }

  const auto &pluginInfo = mod.plugins[entry.pluginIndex];
  if (!pluginInfo.createFunc) {
    AxErrorState::Set(AxErrorCode::FactoryFailed,
                      "Plugin factory function is null", "CreateObject");
    return nullptr;
  }

  return pluginInfo.createFunc();
}

IAxObject *AxPluginManager::CreateObject(const char *interfaceName) {
  AX_PROFILE_FUNCTION();
  return AxExceptionGuard::SafeCallPtr(
      [&]() -> IAxObject * {
        if (!interfaceName) {
          AxErrorState::Set(AxErrorCode::InvalidArgument,
                            "interfaceName is null", "CreateObject");
          return nullptr;
        }

        std::shared_lock<std::shared_mutex> lock(mutex_);

        // Resolve string name to typeId
        auto nameIt = nameToTypeId_.find(interfaceName);
        if (nameIt == nameToTypeId_.end()) {
          AxErrorState::Set(
              AxErrorCode::PluginNotFound,
              (std::string("No plugin found for interface: ") + interfaceName)
                  .c_str(),
              "CreateObject");
          return nullptr;
        }

        return CreateObjectByIdInternal(nameIt->second);
      },
      "Ax_CreateObject");
}

IAxObject *AxPluginManager::CreateObjectById(uint64_t typeId) {
  AX_PROFILE_FUNCTION();
  return AxExceptionGuard::SafeCallPtr(
      [&]() -> IAxObject * {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return CreateObjectByIdInternal(typeId);
      },
      "Ax_CreateObjectById");
}

IAxObject *AxPluginManager::GetSingleton(const char *interfaceName,
                                         const char *serviceName) {
  AX_PROFILE_FUNCTION();
  return AxExceptionGuard::SafeCallPtr(
      [&]() -> IAxObject * {
        if (!interfaceName) {
          AxErrorState::Set(AxErrorCode::InvalidArgument,
                            "interfaceName is null", "GetSingleton");
          return nullptr;
        }

        // Resolve string to typeId
        uint64_t typeId;
        {
          std::shared_lock<std::shared_mutex> lock(mutex_);
          auto nameIt = nameToTypeId_.find(interfaceName);
          if (nameIt == nameToTypeId_.end()) {
            AxErrorState::Set(
                AxErrorCode::PluginNotFound,
                (std::string("No plugin found for interface: ") + interfaceName)
                    .c_str(),
                "GetSingleton");
            return nullptr;
          }
          typeId = nameIt->second;
        }

        return GetSingletonById(typeId, serviceName);
      },
      "Ax_GetSingleton");
}

IAxObject *AxPluginManager::GetSingletonById(uint64_t typeId,
                                             const char *serviceName) {
  AX_PROFILE_FUNCTION();
  return AxExceptionGuard::SafeCallPtr(
      [&]() -> IAxObject * {
        std::string name = serviceName ? serviceName : "";
        bool isDefault = name.empty();

        // Fast path: read-only check for existing singleton (shared_lock)
        {
          std::shared_lock<std::shared_mutex> lock(mutex_);
          if (isDefault) {
            auto it = defaultSingletons_.find(typeId);
            if (it != defaultSingletons_.end())
              return it->second.get();
          } else {
            auto key = std::make_pair(typeId, name);
            auto it = namedSingletons_.find(key);
            if (it != namedSingletons_.end())
              return it->second.get();
          }
        }

        // Slow path: need to create (unique_lock)
        std::unique_lock<std::shared_mutex> lock(mutex_);

        // Double-check after acquiring write lock
        if (isDefault) {
          auto it = defaultSingletons_.find(typeId);
          if (it != defaultSingletons_.end())
            return it->second.get();
        } else {
          auto key = std::make_pair(typeId, name);
          auto it = namedSingletons_.find(key);
          if (it != namedSingletons_.end())
            return it->second.get();
        }

        // Create new instance
        IAxObject *raw = CreateObjectByIdInternal(typeId);
        if (!raw)
          return nullptr;

        // Wrap in shared_ptr with custom deleter that calls Destroy()
        auto sp = std::shared_ptr<IAxObject>(raw, [](IAxObject *p) {
          if (p)
            p->Destroy();
        });

        IAxObject *result = sp.get();

        if (isDefault) {
          defaultSingletons_[typeId] = std::move(sp);
        } else {
          namedSingletons_[std::make_pair(typeId, name)] = std::move(sp);
        }

        return result;
      },
      "Ax_GetSingletonById");
}

void AxPluginManager::ReleaseSingleton(const char *interfaceName,
                                       const char *serviceName) {
  AX_PROFILE_FUNCTION();
  AxExceptionGuard::SafeCallVoid(
      [&]() {
        if (!interfaceName)
          return;

        uint64_t typeId;
        {
          std::shared_lock<std::shared_mutex> lock(mutex_);
          auto nameIt = nameToTypeId_.find(interfaceName);
          if (nameIt == nameToTypeId_.end())
            return;
          typeId = nameIt->second;
        }

        ReleaseSingletonById(typeId, serviceName);
      },
      "Ax_ReleaseSingleton");
}

void AxPluginManager::ReleaseSingletonById(uint64_t typeId,
                                           const char *serviceName) {
  AX_PROFILE_FUNCTION();
  AxExceptionGuard::SafeCallVoid(
      [&]() {
        std::unique_lock<std::shared_mutex> lock(mutex_);

        std::string name = serviceName ? serviceName : "";
        if (name.empty()) {
          // shared_ptr destructor calls Destroy() via custom deleter
          defaultSingletons_.erase(typeId);
        } else {
          namedSingletons_.erase(std::make_pair(typeId, name));
        }
      },
      "Ax_ReleaseSingletonById");
}

void AxPluginManager::ReleaseObject(IAxObject *obj) {
  if (obj)
    obj->Destroy();
}

int AxPluginManager::GetPluginCount() const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  return static_cast<int>(allPlugins_.size());
}

const char *AxPluginManager::GetPluginInterfaceName(int index) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  if (index < 0 || index >= static_cast<int>(allPlugins_.size()))
    return nullptr;
  const auto &entry = allPlugins_[index];
  return modules_[entry.moduleIndex].plugins[entry.pluginIndex].interfaceName;
}

const char *AxPluginManager::GetPluginFileName(int index) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  if (index < 0 || index >= static_cast<int>(allPlugins_.size()))
    return nullptr;
  return modules_[allPlugins_[index].moduleIndex].fileName.c_str();
}

int AxPluginManager::GetPluginType(int index) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  if (index < 0 || index >= static_cast<int>(allPlugins_.size()))
    return -1;
  const auto &entry = allPlugins_[index];
  return static_cast<int>(
      modules_[entry.moduleIndex].plugins[entry.pluginIndex].type);
}

bool AxPluginManager::IsPluginLoaded(int index) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  if (index < 0 || index >= static_cast<int>(allPlugins_.size()))
    return false;
  return modules_[allPlugins_[index].moduleIndex].isLoaded;
}

const char *AxPluginManager::GetLastError() const {
  return AxErrorState::GetErrorMessage();
}

void AxPluginManager::ClearLastError() { AxErrorState::Clear(); }
