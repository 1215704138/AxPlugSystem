#include "../include/MathPlugin.h"
#include "AxPlug/AxPluginExport.h"

// 导出为 Tool 插件（多实例，用户管理生命周期）
AX_EXPORT_TOOL(CMath, IMath)
