#include "../include/BoostTcpClient.h"
#include "../include/TcpClient.h"
#include "AxPlug/AxAutoRegister.h"

AX_AUTO_REGISTER_TOOL(TcpClient, ITcpClient)
AX_AUTO_REGISTER_TOOL_NAMED(BoostTcpClient, ITcpClient, "boost")
AX_DEFINE_PLUGIN_ENTRY()
