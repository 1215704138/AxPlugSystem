#pragma once

#include "IAxObject.h"

// Plugin type
enum class AxPluginType : int { Tool, Service };

// Plugin info structure - returned by each plugin DLL's entry point
struct AxPluginInfo {
    const char* interfaceName;           // Interface type key, e.g. "IMath"
    uint64_t typeId;                     // FNV-1a hash of interfaceName (Hot Path key)
    AxPluginType type;                   // Tool or Service
    IAxObject* (*createFunc)();          // Object creation function pointer
    const char* implName;                // Implementation name tag, e.g. "boost", "" for default
};

// Plugin entry point names
constexpr const char* AX_PLUGIN_ENTRY_POINT = "GetAxPlugin";
constexpr const char* AX_PLUGINS_ENTRY_POINT = "GetAxPlugins";
using GetAxPluginFunc = AxPluginInfo(*)();
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

// Export a Tool plugin (multi-instance, user manages lifecycle)
#define AX_EXPORT_TOOL(TClass, InterfaceType) \
    extern "C" AX_PLUGIN_EXPORT AxPluginInfo GetAxPlugin() { \
        static AxPluginInfo info = { \
            InterfaceType::ax_interface_name, \
            InterfaceType::ax_type_id, \
            AxPluginType::Tool, \
            []() -> IAxObject* { return new TClass(); }, \
            "" \
        }; \
        return info; \
    }

#define AX_EXPORT_TOOL_NAMED(TClass, InterfaceType, ImplName) \
    extern "C" AX_PLUGIN_EXPORT AxPluginInfo GetAxPlugin() { \
        static AxPluginInfo info = { \
            InterfaceType::ax_interface_name, \
            InterfaceType::ax_type_id, \
            AxPluginType::Tool, \
            []() -> IAxObject* { return new TClass(); }, \
            ImplName \
        }; \
        return info; \
    }

// Export a Service plugin (named singleton, framework managed)
#define AX_EXPORT_SERVICE(TClass, InterfaceType) \
    extern "C" AX_PLUGIN_EXPORT AxPluginInfo GetAxPlugin() { \
        static AxPluginInfo info = { \
            InterfaceType::ax_interface_name, \
            InterfaceType::ax_type_id, \
            AxPluginType::Service, \
            []() -> IAxObject* { return new TClass(); }, \
            "" \
        }; \
        return info; \
    }

#define AX_EXPORT_SERVICE_NAMED(TClass, InterfaceType, ImplName) \
    extern "C" AX_PLUGIN_EXPORT AxPluginInfo GetAxPlugin() { \
        static AxPluginInfo info = { \
            InterfaceType::ax_interface_name, \
            InterfaceType::ax_type_id, \
            AxPluginType::Service, \
            []() -> IAxObject* { return new TClass(); }, \
            ImplName \
        }; \
        return info; \
    }

// ============ Multi-plugin export macros ============
// Use these to export multiple plugins from a single DLL.
//
// Example:
//   AX_BEGIN_PLUGIN_MAP()
//       AX_PLUGIN_TOOL(CMath, IMath)
//       AX_PLUGIN_SERVICE(CLoggerService, ILoggerService)
//   AX_END_PLUGIN_MAP()

#define AX_PLUGIN_TOOL(TClass, InterfaceType) \
    { InterfaceType::ax_interface_name, InterfaceType::ax_type_id, AxPluginType::Tool, []() -> IAxObject* { return new TClass(); }, "" },

#define AX_PLUGIN_TOOL_NAMED(TClass, InterfaceType, ImplName) \
    { InterfaceType::ax_interface_name, InterfaceType::ax_type_id, AxPluginType::Tool, []() -> IAxObject* { return new TClass(); }, ImplName },

#define AX_PLUGIN_SERVICE(TClass, InterfaceType) \
    { InterfaceType::ax_interface_name, InterfaceType::ax_type_id, AxPluginType::Service, []() -> IAxObject* { return new TClass(); }, "" },

#define AX_PLUGIN_SERVICE_NAMED(TClass, InterfaceType, ImplName) \
    { InterfaceType::ax_interface_name, InterfaceType::ax_type_id, AxPluginType::Service, []() -> IAxObject* { return new TClass(); }, ImplName },

#define AX_BEGIN_PLUGIN_MAP() \
    extern "C" AX_PLUGIN_EXPORT const AxPluginInfo* GetAxPlugins(int* count) { \
        static const AxPluginInfo plugins[] = {

#define AX_END_PLUGIN_MAP() \
        }; \
        if (count) *count = static_cast<int>(sizeof(plugins) / sizeof(plugins[0])); \
        return plugins; \
    }
