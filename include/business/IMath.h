#pragma once

#include "AxPlug/IAxObject.h"

// 数学计算接口
class IMath : public IAxObject {
    AX_INTERFACE(IMath)

public:
    // 加法运算
    virtual int Add(int a, int b) = 0;
    
    // 减法运算
    virtual int Sub(int a, int b) = 0;
};
