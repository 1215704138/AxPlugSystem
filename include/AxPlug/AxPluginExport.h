#pragma once

#include "IAxObject.h"

// Plugin type
enum class AxPluginType : int { Tool, Service };

// Plugin ABI version - increase when breaking changes occur
constexpr uint32_t AX_PLUGIN_ABI_VERSION = 1;

// Plugin info structure - returned by each plugin DLL's entry point
struct AxPluginInfo {
    const char* interfaceName;           // Interface type key, e.g. "IMath"
    uint64_t typeId;                     // FNV-1a hash of interfaceName (Hot Path key)
    AxPluginType type;                   // Tool or Service
    IAxObject* (*createFunc)();          // Object creation function pointer
    const char* implName;                // Implementation name tag, e.g. "boost", "" for default
    uint32_t abiVersion;                 // ABI version for compatibility checking
};

// Plugin entry point
constexpr const char* AX_PLUGINS_ENTRY_POINT = "GetAxPlugins";
using GetAxPluginsFunc = const AxPluginInfo*(*)(int*);

// Plugin export control macros
#ifdef _WIN32
    #ifdef AX_PLUGIN_EXPORTS
        #define AX_PLUGIN_EXPORT __declspec(dllexport)
    #else
        #define AX_PLUGIN_EXPORT __declspec(dllimport)
    #endif
#else
    #ifdef AX_PLUGIN_EXPORTS
        #define AX_PLUGIN_EXPORT __attribute__((visibility("default")))
    #else
        #define AX_PLUGIN_EXPORT
    #endif
#endif

// ============ Plugin export macros ============
//
// Example (single plugin):
//   AX_BEGIN_PLUGIN_MAP()
//       AX_PLUGIN_TOOL(CMath, IMath)
//   AX_END_PLUGIN_MAP()
//
// Example (multiple plugins):
//   AX_BEGIN_PLUGIN_MAP()
//       AX_PLUGIN_TOOL(CMath, IMath)
//       AX_PLUGIN_SERVICE(CLoggerService, ILoggerService)
//       AX_PLUGIN_TOOL_NAMED(BoostTcpServer, ITcpServer, "boost")
//   AX_END_PLUGIN_MAP()

#define AX_PLUGIN_TOOL(TClass, InterfaceType) \
    { InterfaceType::ax_interface_name, InterfaceType::ax_type_id, AxPluginType::Tool, []() -> IAxObject* { return new TClass(); }, "", AX_PLUGIN_ABI_VERSION },

#define AX_PLUGIN_TOOL_NAMED(TClass, InterfaceType, ImplName) \
    { InterfaceType::ax_interface_name, InterfaceType::ax_type_id, AxPluginType::Tool, []() -> IAxObject* { return new TClass(); }, ImplName, AX_PLUGIN_ABI_VERSION },

#define AX_PLUGIN_SERVICE(TClass, InterfaceType) \
    { InterfaceType::ax_interface_name, InterfaceType::ax_type_id, AxPluginType::Service, []() -> IAxObject* { return new TClass(); }, "", AX_PLUGIN_ABI_VERSION },

#define AX_PLUGIN_SERVICE_NAMED(TClass, InterfaceType, ImplName) \
    { InterfaceType::ax_interface_name, InterfaceType::ax_type_id, AxPluginType::Service, []() -> IAxObject* { return new TClass(); }, ImplName, AX_PLUGIN_ABI_VERSION },

#define AX_BEGIN_PLUGIN_MAP() \
    extern "C" AX_PLUGIN_EXPORT const AxPluginInfo* GetAxPlugins(int* count) { \
        static const AxPluginInfo plugins[] = {

#define AX_END_PLUGIN_MAP() \
        }; \
        if (count) *count = static_cast<int>(sizeof(plugins) / sizeof(plugins[0])); \
        return plugins; \
    }
