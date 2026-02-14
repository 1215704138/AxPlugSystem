#pragma once

// Forward declaration
class AxPluginManager;

// Interface type key macro
// Declares a compile-time string literal as the type key for cross-DLL matching
// Usage: class IMath : public IAxObject { AX_INTERFACE(IMath) public: ... };
#define AX_INTERFACE(InterfaceName) \
public: \
    static constexpr const char* ax_interface_name = #InterfaceName; \
private:

// Base object interface - all plugin interfaces must inherit from this
class IAxObject {
public:
    virtual ~IAxObject() = default;

protected:
    // Self-destruct interface, only AxPluginManager can call
    virtual void Destroy() = 0;

private:
    friend class AxPluginManager;
};
