#pragma once
#include "../AxPlug/IAxObject.h"
#include <string>
#include <vector>

// Boost功能插件接口 - 公共接口，不暴露实现细节
class IBoostFeatures : public IAxObject {
    AX_INTERFACE(IBoostFeatures)
public:
    virtual std::string GetCurrentPath() = 0;
    virtual bool FileExists(const std::string& path) = 0;
    virtual bool CreateDirectory(const std::string& path) = 0;
    virtual std::vector<std::string> ListFiles(const std::string& directory) = 0;
};
