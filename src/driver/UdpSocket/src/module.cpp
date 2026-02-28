#include "../include/BoostUdpSocket.h"
#include "../include/UdpSocket.h"
#include "AxPlug/AxAutoRegister.h"

AX_AUTO_REGISTER_TOOL(UdpSocket, IUdpSocket)
AX_AUTO_REGISTER_TOOL_NAMED(BoostUdpSocket, IUdpSocket, "boost")
AX_DEFINE_PLUGIN_ENTRY()
