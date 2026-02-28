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

// AxCore - C export functions

// Global shutdown flag — outlives AxPluginManager's Meyer's singleton
// so shared_ptr deleters can safely check it during static destruction (SIOF guard)
static std::atomic<bool> g_shuttingDown{false};

void Ax_Internal_SetShuttingDown() {
    g_shuttingDown.store(true, std::memory_order_release);
}

// Internal thread_local error storage — single canonical location for all DLLs
namespace {
    struct ThreadLocalError {
        int code = 0;
        std::string message;
        std::string source;
    };
    ThreadLocalError& GetTLError() {
        thread_local ThreadLocalError err;
        return err;
    }
}

extern "C" {

    // ========== Initialization API ==========

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

    // ========== TypeId Fast Path API ==========

    AX_CORE_API IAxObject* Ax_CreateObjectById(uint64_t typeId) {
        return AxPluginManager::Instance()->CreateObjectById(typeId);
    }

    AX_CORE_API IAxObject* Ax_CreateObjectByIdNamed(uint64_t typeId, const char* implName) {
        return AxPluginManager::Instance()->CreateObjectByIdNamed(typeId, implName);
    }

    AX_CORE_API IAxObject* Ax_GetSingletonById(uint64_t typeId, const char* serviceName) {
        return AxPluginManager::Instance()->GetSingletonById(typeId, serviceName);
    }

    AX_CORE_API void Ax_ReleaseSingletonById(uint64_t typeId, const char* serviceName) {
        AxPluginManager::Instance()->ReleaseSingletonById(typeId, serviceName);
    }

    AX_CORE_API IAxObject* Ax_AcquireSingletonById(uint64_t typeId, const char* serviceName) {
        return AxPluginManager::Instance()->AcquireSingletonById(typeId, serviceName);
    }

    AX_CORE_API void Ax_ReleaseSingletonRef(uint64_t typeId, const char* serviceName) {
        AxPluginManager::Instance()->ReleaseSingletonRef(typeId, serviceName);
    }

    // ========== Introspection API ==========

    AX_CORE_API int Ax_FindPluginsByTypeId(uint64_t typeId, int* outIndices, int maxCount) {
        return AxPluginManager::Instance()->FindPluginsByTypeId(typeId, outIndices, maxCount);
    }

    // ========== Profiler API ==========
    // Implemented in AxProfiler.cpp (compiled into AxCore.dll)

    // ========== Error Handling API (canonical thread_local in AxCore.dll) ==========

    AX_CORE_API void Ax_SetError(int code, const char* message, const char* source) {
        auto& err = GetTLError();
        err.code = code;
        err.message = message ? message : "";
        err.source = source ? source : "";
    }

    AX_CORE_API int Ax_GetErrorCode() {
        return GetTLError().code;
    }

    AX_CORE_API const char* Ax_GetLastError() {
        return GetTLError().message.c_str();
    }

    AX_CORE_API const char* Ax_GetErrorSource() {
        return GetTLError().source.c_str();
    }

    AX_CORE_API bool Ax_HasErrorState() {
        return GetTLError().code != 0;
    }

    AX_CORE_API void Ax_ClearLastError() {
        auto& err = GetTLError();
        err.code = 0;
        err.message.clear();
        err.source.clear();
    }

    AX_CORE_API bool Ax_IsShuttingDown() {
        return g_shuttingDown.load(std::memory_order_acquire);
    }

} // extern "C"

// DLL entry point
#ifdef _WIN32
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    return TRUE;
}
#endif
