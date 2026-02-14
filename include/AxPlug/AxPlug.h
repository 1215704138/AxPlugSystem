#pragma once

#include "IAxObject.h"
#include "AxPluginExport.h"
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
    AX_CORE_API void Ax_Init(const char* pluginDir);
    AX_CORE_API void Ax_LoadPlugins(const char* pluginDir);
    AX_CORE_API IAxObject* Ax_CreateObject(const char* interfaceName);
    AX_CORE_API IAxObject* Ax_GetSingleton(const char* interfaceName, const char* serviceName);
    AX_CORE_API void Ax_ReleaseSingleton(const char* interfaceName, const char* serviceName);
    AX_CORE_API void Ax_ReleaseObject(IAxObject* obj);
    AX_CORE_API int Ax_GetPluginCount();
    AX_CORE_API const char* Ax_GetPluginInterfaceName(int index);
    AX_CORE_API const char* Ax_GetPluginFileName(int index);
    AX_CORE_API int Ax_GetPluginType(int index);
    AX_CORE_API bool Ax_IsPluginLoaded(int index);
}

// Plugin query info
struct AxPluginQueryInfo {
    const char* fileName;
    const char* interfaceName;
    bool isTool;
    bool isLoaded;
};

namespace AxPlug {

    // ========== Initialize ==========

    // Initialize plugin system and load plugins from directory
    // If pluginDir is empty, auto-detect exe directory
    inline void Init(const char* pluginDir = "") {
        if (pluginDir && strlen(pluginDir) > 0) {
            Ax_Init(pluginDir);
            Ax_LoadPlugins(pluginDir);
        } else {
#ifdef _WIN32
            wchar_t buf[1024];
            ::GetModuleFileNameW(NULL, buf, 1024);
            std::wstring wpath(buf);
            auto pos = wpath.find_last_of(L"\\/");
            if (pos != std::wstring::npos) wpath = wpath.substr(0, pos);
            int sz = WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), (int)wpath.size(), NULL, 0, NULL, NULL);
            std::string path(sz, 0);
            WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), (int)wpath.size(), &path[0], sz, NULL, NULL);
            Ax_Init(path.c_str());
            Ax_LoadPlugins(path.c_str());
#else
            Ax_Init("./");
            Ax_LoadPlugins("./");
#endif
        }
    }

    // ========== Tool API ==========

    // Create a tool instance (multi-instance, user manages lifecycle)
    // Returns raw pointer, user must call DestroyTool() when done
    template<typename T>
    inline T* CreateTool() {
        IAxObject* obj = Ax_CreateObject(T::ax_interface_name);
        return static_cast<T*>(obj);
    }

    // Destroy a tool instance
    inline void DestroyTool(IAxObject* tool) {
        if (tool) {
            Ax_ReleaseObject(tool);
        }
    }

    // ========== Service API ==========

    // Get or create a named service instance (singleton per name)
    // Default name "" = global singleton
    // Different names create independent instances of the same service type
    template<typename T>
    inline T* GetService(const char* name = "") {
        IAxObject* obj = Ax_GetSingleton(T::ax_interface_name, name);
        return static_cast<T*>(obj);
    }

    // Release a named service instance
    template<typename T>
    inline void ReleaseService(const char* name = "") {
        Ax_ReleaseSingleton(T::ax_interface_name, name);
    }

    // ========== Query API ==========

    // Get number of loaded plugin modules
    inline int GetPluginCount() {
        return Ax_GetPluginCount();
    }

    // Get plugin info by index
    inline AxPluginQueryInfo GetPluginInfo(int index) {
        AxPluginQueryInfo info = {};
        info.fileName = Ax_GetPluginFileName(index);
        info.interfaceName = Ax_GetPluginInterfaceName(index);
        info.isTool = (Ax_GetPluginType(index) == 0);
        info.isLoaded = Ax_IsPluginLoaded(index);
        return info;
    }

} // namespace AxPlug

// Host startup convenience macro
#define AX_HOST_INIT() AxPlug::Init();
