#pragma once

#include "business/IMath.h"

// 数学计算插件实现类
class CMath : public IMath {
public:
    // 加法运算实现
    int Add(int a, int b) override;
    
    // 减法运算实现
    int Sub(int a, int b) override;

protected:
    void Destroy() override { delete this; }
};
