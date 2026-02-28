#include "../include/BoostTcpServer.h"
#include "../include/TcpServer.h"
#include "AxPlug/AxAutoRegister.h"

AX_AUTO_REGISTER_TOOL(TcpServer, ITcpServer)
AX_AUTO_REGISTER_TOOL_NAMED(BoostTcpServer, ITcpServer, "boost")
AX_DEFINE_PLUGIN_ENTRY()
