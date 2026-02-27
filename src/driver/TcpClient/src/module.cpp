#include "../../../include/AxPlug/AxPluginExport.h"
#include "../include/BoostTcpClient.h"
#include "../include/TcpClient.h"


AX_BEGIN_PLUGIN_MAP()
AX_PLUGIN_TOOL(TcpClient, ITcpClient)
AX_PLUGIN_TOOL_NAMED(BoostTcpClient, ITcpClient, "boost")
AX_END_PLUGIN_MAP()
