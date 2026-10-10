#ifndef VNAPRO_VNA_CORE_NATIVE_LIBRARY_H
#define VNAPRO_VNA_CORE_NATIVE_LIBRARY_H

#include <string>

#include "device/native/NativeFunctions.h"

namespace vna_core {
namespace detail {

class NativeLibrary {
public:
    ~NativeLibrary();
    void open(const std::string &path);
    void close();

    const NativeFunctions &functions() const
    {
        return functions_;
    }

private:
    void *handle_ = nullptr;
    NativeFunctions functions_;
};

}  // namespace detail
}  // namespace vna_core

#endif
