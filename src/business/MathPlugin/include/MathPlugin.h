#pragma once

#include "business/IMath.h"

// 数学计算插件实现类
class CMath : public IMath {
public:
    // 加法运算实现
    int Add(int a, int b) override;
    
    // 减法运算实现
    int Sub(int a, int b) override;

    // 生命周期钩子
    void OnInit() override {
        // 初始化逻辑（如果需要）
    }
    
    void OnShutdown() override {
        // 清理逻辑（如果需要）
    }

protected:
    void Destroy() override { delete this; }
};
