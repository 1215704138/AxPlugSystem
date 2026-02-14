#include "../../../include/AxPlug/AxPluginExport.h"
#include "../include/BoostUdpSocket.h"
#include "../include/UdpSocket.h"


AX_BEGIN_PLUGIN_MAP()
AX_PLUGIN_TOOL(UdpSocket, IUdpSocket)
AX_PLUGIN_TOOL(BoostUdpSocket, IUdpSocket)
AX_END_PLUGIN_MAP()
