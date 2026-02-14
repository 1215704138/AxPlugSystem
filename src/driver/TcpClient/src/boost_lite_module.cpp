#include "../include/BoostTcpClientLite.h"
#include "../../../include/AxPlug/AxPluginExport.h"

// 轻量级插件导出函数
extern "C" AXPLUGIN_EXPORT IAxObject* CreateInstance() {
    return new BoostTcpClientLite();
}

extern "C" AXPLUGIN_EXPORT const char* GetInterfaceName() {
    return BoostTcpClientLite::ax_interface_name;
}

extern "C" AXPLUGIN_EXPORT const char* GetPluginName() {
    return "BoostTcpClientLite";
}

extern "C" AXPLUGIN_EXPORT const char* GetPluginVersion() {
    return "1.0.0";
}

extern "C" AXPLUGIN_EXPORT const char* GetPluginDescription() {
    return "Lightweight high-performance TCP client with minimal Boost dependencies";
}
