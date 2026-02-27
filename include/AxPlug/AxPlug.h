#pragma once

#include "AxException.h"
#include "AxPluginExport.h"
#include "AxProfiler.h"
#include "IAxObject.h"
#include <cstring>


#ifdef _WIN32
#include <windows.h>
#endif

// AxCore DLL export/import control
#ifdef AX_CORE_EXPORTS
#define AX_CORE_API __declspec(dllexport)
#else
#ifdef _WIN32
#define AX_CORE_API __declspec(dllimport)
#else
#define AX_CORE_API
#endif
#endif

// C export functions from AxCore.dll
extern "C" {
// Initialization API
AX_CORE_API void Ax_Init(const char *pluginDir);
AX_CORE_API void Ax_LoadPlugins(const char *pluginDir);

// Object Lifecycle API (string-based)
AX_CORE_API IAxObject *Ax_CreateObject(const char *interfaceName);
AX_CORE_API IAxObject *Ax_GetSingleton(const char *interfaceName, const char *serviceName);
AX_CORE_API void Ax_ReleaseSingleton(const char *interfaceName, const char *serviceName);
AX_CORE_API void Ax_ReleaseObject(IAxObject *obj);

// Object Lifecycle API (typeId-based, fast path)
AX_CORE_API IAxObject *Ax_CreateObjectById(uint64_t typeId);
AX_CORE_API IAxObject *Ax_CreateObjectByIdNamed(uint64_t typeId, const char *implName);
AX_CORE_API IAxObject *Ax_GetSingletonById(uint64_t typeId, const char *serviceName);
AX_CORE_API void Ax_ReleaseSingletonById(uint64_t typeId, const char *serviceName);

// Query API
AX_CORE_API int Ax_GetPluginCount();
AX_CORE_API const char *Ax_GetPluginInterfaceName(int index);
AX_CORE_API const char *Ax_GetPluginFileName(int index);
AX_CORE_API int Ax_GetPluginType(int index);
AX_CORE_API bool Ax_IsPluginLoaded(int index);

// Introspection API
AX_CORE_API int Ax_FindPluginsByTypeId(uint64_t typeId, int* outIndices, int maxCount);

// Profiler API
AX_CORE_API void Ax_ProfilerBeginSession(const char *name, const char *filepath);
AX_CORE_API void Ax_ProfilerEndSession();
AX_CORE_API void Ax_ProfilerWriteProfile(const AxProfileResult *result);
AX_CORE_API int  Ax_ProfilerIsActive();

// Error Handling API (declarations in AxException.h)
}

// Plugin query info
struct AxPluginQueryInfo {
  const char *fileName;
  const char *interfaceName;
  bool isTool;
  bool isLoaded;
};

namespace AxPlug {

// ========== Initialize ==========

// Initialize plugin system and load plugins from directory
// If pluginDir is empty, auto-detect exe directory
inline void Init(const char *pluginDir = "") {
  if (pluginDir && strlen(pluginDir) > 0) {
    Ax_Init(pluginDir);
    Ax_LoadPlugins(pluginDir);
  } else {
#ifdef _WIN32
    wchar_t buf[1024];
    ::GetModuleFileNameW(NULL, buf, 1024);
    std::wstring wpath(buf);
    auto pos = wpath.find_last_of(L"\\/");
    if (pos != std::wstring::npos)
      wpath = wpath.substr(0, pos);
    int sz = WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), (int)wpath.size(),
                                 NULL, 0, NULL, NULL);
    std::string path(sz, 0);
    WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), (int)wpath.size(), &path[0],
                        sz, NULL, NULL);
    Ax_Init(path.c_str());
    Ax_LoadPlugins(path.c_str());
#else
    Ax_Init("./");
    Ax_LoadPlugins("./");
#endif
  }
}

// ========== Tool API (Smart Pointer) ==========

namespace internal {
    template <typename T, typename = void>
    struct has_ax_type_id : std::false_type {};
    
    template <typename T>
    struct has_ax_type_id<T, std::void_t<decltype(T::ax_type_id)>> : std::true_type {};
}

// Create a tool instance with automatic lifecycle management (shared_ptr)
// When the last shared_ptr goes out of scope, the object is automatically
// destroyed
template <typename T> inline std::shared_ptr<T> CreateTool() {
  static_assert(
      std::is_base_of_v<IAxObject, T>,
      "错误: T 必须继承自 IAxObject。"
      "请检查你的接口类是否使用了 AX_INTERFACE() 宏。"
  );
  static_assert(
      AxPlug::internal::has_ax_type_id<T>::value,
      "错误: T 缺少 ax_type_id 定义。"
      "请确保你的接口类使用了 AX_INTERFACE(InterfaceName) 宏。"
  );
  
  IAxObject *obj = Ax_CreateObjectById(T::ax_type_id);
  if (!obj)
    return nullptr;
  return std::shared_ptr<T>(static_cast<T *>(obj), [](T *p) {
    if (p)
      Ax_ReleaseObject(p);
  });
}

// Create a tool instance by named implementation (e.g. CreateTool<ITcpServer>("boost"))
template <typename T> inline std::shared_ptr<T> CreateTool(const char *implName) {
  static_assert(
      std::is_base_of_v<IAxObject, T>,
      "错误: T 必须继承自 IAxObject。"
      "请检查你的接口类是否使用了 AX_INTERFACE() 宏。"
  );
  static_assert(
      AxPlug::internal::has_ax_type_id<T>::value,
      "错误: T 缺少 ax_type_id 定义。"
      "请确保你的接口类使用了 AX_INTERFACE(InterfaceName) 宏。"
  );
  
  IAxObject *obj = Ax_CreateObjectByIdNamed(T::ax_type_id, implName);
  if (!obj)
    return nullptr;
  return std::shared_ptr<T>(static_cast<T *>(obj), [](T *p) {
    if (p)
      Ax_ReleaseObject(p);
  });
}

// Explicitly reset a smart pointer tool (triggers destruction if last
// reference)
template <typename T> inline void DestroyTool(std::shared_ptr<T> &tool) {
  tool.reset();
}

// ========== Tool API (Raw Pointer) ==========

// Create a tool instance with manual lifecycle management
// User MUST call DestroyTool(raw pointer) when done
template <typename T> inline T *CreateToolRaw() {
  static_assert(
      std::is_base_of_v<IAxObject, T>,
      "错误: T 必须继承自 IAxObject。"
      "请检查你的接口类是否使用了 AX_INTERFACE() 宏。"
  );
  static_assert(
      AxPlug::internal::has_ax_type_id<T>::value,
      "错误: T 缺少 ax_type_id 定义。"
      "请确保你的接口类使用了 AX_INTERFACE(InterfaceName) 宏。"
  );
  
  IAxObject *obj = Ax_CreateObjectById(T::ax_type_id);
  return static_cast<T *>(obj);
}

// Create a raw pointer tool by named implementation
template <typename T> inline T *CreateToolRaw(const char *implName) {
  static_assert(
      std::is_base_of_v<IAxObject, T>,
      "错误: T 必须继承自 IAxObject。"
      "请检查你的接口类是否使用了 AX_INTERFACE() 宏。"
  );
  static_assert(
      AxPlug::internal::has_ax_type_id<T>::value,
      "错误: T 缺少 ax_type_id 定义。"
      "请确保你的接口类使用了 AX_INTERFACE(InterfaceName) 宏。"
  );
  
  IAxObject *obj = Ax_CreateObjectByIdNamed(T::ax_type_id, implName);
  return static_cast<T *>(obj);
}

// Destroy a raw pointer tool instance
inline void DestroyTool(IAxObject *tool) {
  if (tool) {
    Ax_ReleaseObject(tool);
  }
}

// ========== Service API ==========

// Get or create a named service instance (singleton per name)
// Default name "" = global singleton
// Different names create independent instances of the same service type
template <typename T> inline T *GetService(const char *name = "") {
  static_assert(
      std::is_base_of_v<IAxObject, T>,
      "错误: T 必须继承自 IAxObject。"
      "请检查你的接口类是否使用了 AX_INTERFACE() 宏。"
  );
  static_assert(
      AxPlug::internal::has_ax_type_id<T>::value,
      "错误: T 缺少 ax_type_id 定义。"
      "请确保你的接口类使用了 AX_INTERFACE(InterfaceName) 宏。"
  );
  
  IAxObject *obj = Ax_GetSingletonById(T::ax_type_id, name);
  return static_cast<T *>(obj);
}

// Release a named service instance
template <typename T> inline void ReleaseService(const char *name = "") {
  Ax_ReleaseSingletonById(T::ax_type_id, name);
}

// Noexcept service getter, returns (pointer, error_code) pair
// Suitable for use in destructors or cleanup paths
template <typename T> 
[[nodiscard]] inline std::pair<T*, AxInstanceError> TryGetService(const char *name = "") noexcept {
  static_assert(
      std::is_base_of_v<IAxObject, T>,
      "错误: T 必须继承自 IAxObject。"
      "请检查你的接口类是否使用了 AX_INTERFACE() 宏。"
  );
  static_assert(
      AxPlug::internal::has_ax_type_id<T>::value,
      "错误: T 缺少 ax_type_id 定义。"
      "请确保你的接口类使用了 AX_INTERFACE(InterfaceName) 宏。"
  );
  
  IAxObject* obj = Ax_GetSingletonById(T::ax_type_id, name);
  AxInstanceError err = obj ? AxInstanceError::kSuccess : AxInstanceError::kErrorNotFound;
  return { static_cast<T*>(obj), err };
}

// ========== Query API ==========

// Get number of loaded plugin modules
inline int GetPluginCount() { return Ax_GetPluginCount(); }

// Get plugin info by index
inline AxPluginQueryInfo GetPluginInfo(int index) {
  AxPluginQueryInfo info = {};
  info.fileName = Ax_GetPluginFileName(index);
  info.interfaceName = Ax_GetPluginInterfaceName(index);
  info.isTool = (Ax_GetPluginType(index) == 0);
  info.isLoaded = Ax_IsPluginLoaded(index);
  return info;
}

// Introspection API: Find all implementations of a specific interface
template<typename T>
std::vector<AxPluginQueryInfo> FindImplementations() {
    int maxCount = 64; 
    std::vector<int> indices(maxCount);
    int count = Ax_FindPluginsByTypeId(T::ax_type_id, indices.data(), maxCount);
    
    std::vector<AxPluginQueryInfo> results;
    for(int i = 0; i < count; ++i) {
        results.push_back(GetPluginInfo(indices[i]));
    }
    return results;
}

// ========== Profiler API ==========

// Start a profiling session (generates Chrome trace format)
// Open the output file in chrome://tracing to visualize
inline void ProfilerBegin(const char *name = "AxPlug",
                          const char *filepath = "trace.json") {
  Ax_ProfilerBeginSession(name, filepath);
}

// End the profiling session and flush results to file
inline void ProfilerEnd() { Ax_ProfilerEndSession(); }

// ========== Error Handling API ==========

// Get the last error message (thread-local)
inline const char *GetLastError() { return Ax_GetLastError(); }

// Clear the last error (thread-local)
inline void ClearLastError() { Ax_ClearLastError(); }

// Check if the last operation produced an error
inline bool HasError() { return Ax_HasErrorState(); }

} // namespace AxPlug

// Host startup convenience macro
#define AX_HOST_INIT() AxPlug::Init();
