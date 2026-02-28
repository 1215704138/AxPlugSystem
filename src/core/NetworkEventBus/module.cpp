#include "AxPlug/AxPluginExport.h"
#include "NetworkEventBusImpl.h"

AX_BEGIN_PLUGIN_MAP()
    AX_PLUGIN_SERVICE(NetworkEventBusImpl, AxPlug::INetworkEventBus)
AX_END_PLUGIN_MAP()
