#include "../../../include/AxPlug/AxPluginExport.h"
#include "../include/BoostTcpServer.h"
#include "../include/TcpServer.h"


AX_BEGIN_PLUGIN_MAP()
AX_PLUGIN_TOOL(TcpServer, ITcpServer)
AX_PLUGIN_TOOL(BoostTcpServer, ITcpServer)
AX_END_PLUGIN_MAP()
