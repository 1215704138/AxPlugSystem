#pragma once

#include "business/IMath.h"
#include "AxPlug/AxPluginImpl.h"

// 数学计算插件实现类 (CRTP: Destroy/OnInit/OnShutdown auto-provided)
class CMath : public AxPluginImpl<CMath, IMath> {
public:
    // 加法运算实现
    int Add(int a, int b) override;
    
    // 减法运算实现
    int Sub(int a, int b) override;
};
