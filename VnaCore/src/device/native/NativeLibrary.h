#ifndef VNAPRO_VNA_CORE_NATIVE_LIBRARY_H
#define VNAPRO_VNA_CORE_NATIVE_LIBRARY_H

#include <string>

#include "vna_native/NativeApiContract.h"

namespace vna_core {
namespace detail {

struct NativeFunctions {
    vna_native::DrvHwInitFn hwInit = nullptr;
    vna_native::DrvGetSweepDataFn getSweepData = nullptr;
    vna_native::DrvStartSweepFn startSweep = nullptr;
    vna_native::DrvActivateRxLinkFn activateRxLink = nullptr;
    vna_native::DrvSetRfPowerOnFn setRfPowerOn = nullptr;
    vna_native::DrvSetFastSweepModeFn setFastSweepMode = nullptr;
    vna_native::DrvGetIDNFn getIDN = nullptr;
    vna_native::DrvCheckSweepPortPowerFn checkSweepPortPower = nullptr;
    vna_native::DrvDataFrameDownloadFn dataFrameDownload = nullptr;
};

class NativeLibrary {
public:
    ~NativeLibrary();
    void open(const std::string& path);
    void close();
    const NativeFunctions& functions() const { return functions_; }

private:
    void* handle_ = nullptr;
    NativeFunctions functions_;
};

}  // namespace detail
}  // namespace vna_core

#endif
