#include "AxPluginManager.h"

#ifdef _WIN32
#include <windows.h>
#endif

// AxCore DLL export macro
#ifdef AX_CORE_EXPORTS
    #define AX_CORE_API __declspec(dllexport)
#else
    #ifdef _WIN32
        #define AX_CORE_API __declspec(dllimport)
    #else
        #define AX_CORE_API
    #endif
#endif

// AxCore v2 - C export functions
extern "C" {

    AX_CORE_API void Ax_Init(const char* pluginDir) {
        AxPluginManager::Instance()->Init(pluginDir);
    }

    AX_CORE_API void Ax_LoadPlugins(const char* pluginDir) {
        AxPluginManager::Instance()->LoadPlugins(pluginDir);
    }

    AX_CORE_API IAxObject* Ax_CreateObject(const char* interfaceName) {
        return AxPluginManager::Instance()->CreateObject(interfaceName);
    }

    AX_CORE_API IAxObject* Ax_GetSingleton(const char* interfaceName, const char* serviceName) {
        return AxPluginManager::Instance()->GetSingleton(interfaceName, serviceName);
    }

    AX_CORE_API void Ax_ReleaseSingleton(const char* interfaceName, const char* serviceName) {
        AxPluginManager::Instance()->ReleaseSingleton(interfaceName, serviceName);
    }

    AX_CORE_API void Ax_ReleaseObject(IAxObject* obj) {
        AxPluginManager::Instance()->ReleaseObject(obj);
    }

    AX_CORE_API int Ax_GetPluginCount() {
        return AxPluginManager::Instance()->GetPluginCount();
    }

    AX_CORE_API const char* Ax_GetPluginInterfaceName(int index) {
        return AxPluginManager::Instance()->GetPluginInterfaceName(index);
    }

    AX_CORE_API const char* Ax_GetPluginFileName(int index) {
        return AxPluginManager::Instance()->GetPluginFileName(index);
    }

    AX_CORE_API int Ax_GetPluginType(int index) {
        return AxPluginManager::Instance()->GetPluginType(index);
    }

    AX_CORE_API bool Ax_IsPluginLoaded(int index) {
        return AxPluginManager::Instance()->IsPluginLoaded(index);
    }

} // extern "C"

// DLL entry point
#ifdef _WIN32
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    return TRUE;
}
#endif
