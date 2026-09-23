#ifndef VNAPRO_TESTS_NATIVE_CONTRACT_NATIVE_LIBRARY_PROBE_H
#define VNAPRO_TESTS_NATIVE_CONTRACT_NATIVE_LIBRARY_PROBE_H

#include "vna_native/NativeApiContract.h"

#include <string>
#include <vector>

namespace vna_test {

struct NativeFunctions {
    NativeFunctions();

    vna_native::DrvHwInitFn hwInit;
    vna_native::DrvGetSweepDataFn getSweepData;
    vna_native::DrvStartSweepFn startSweep;
    vna_native::DrvActivateRxLinkFn activateRxLink;
    vna_native::DrvSetRfPowerOnFn setRfPowerOn;
    vna_native::DrvSetFastSweepModeFn setFastSweepMode;
    vna_native::DrvGetIDNFn getIDN;
    vna_native::DrvCheckSweepPortPowerFn checkSweepPortPower;
    vna_native::DrvDataFrameDownloadFn dataFrameDownload;
};

class NativeLibraryProbe {
public:
    NativeLibraryProbe();
    ~NativeLibraryProbe();

    NativeLibraryProbe(const NativeLibraryProbe&) = delete;
    NativeLibraryProbe& operator=(const NativeLibraryProbe&) = delete;

    bool Open(const std::string& absolutePath, std::string* errorMessage);
    void Close();

    bool IsOpen() const;
    bool ResolveAll(std::vector<std::string>* missingSymbols,
                    std::string* errorMessage);

    const NativeFunctions& Functions() const;

private:
    void* handle_;
    NativeFunctions functions_;
};

}  // namespace vna_test

#endif  // VNAPRO_TESTS_NATIVE_CONTRACT_NATIVE_LIBRARY_PROBE_H
