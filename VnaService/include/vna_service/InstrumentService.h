#ifndef VNAPRO_VNA_SERVICE_INSTRUMENT_SERVICE_H
#define VNAPRO_VNA_SERVICE_INSTRUMENT_SERVICE_H

#include <cstdint>
#include <future>
#include <memory>
#include <string>
#include <vector>

#include "vna_service/ConfigPatch.h"
#include "vna_service/DataResult.h"
#include "vna_service/InstrumentError.h"
#include "vna_service/InstrumentSettings.h"
#include "vna_service/InstrumentSnapshot.h"
#include "vna_service/Measurement.h"
#include "vna_service/OperationResult.h"

namespace vna_core {
class AcquisitionCore;
}

namespace vna_service {

// Thread confinement: all calls after startup belong to the service executor.
// The referenced core must outlive this service. Protocol state survives clients.
class InstrumentService {
public:
    explicit InstrumentService(vna_core::AcquisitionCore& core);
    ~InstrumentService();
    InstrumentService(const InstrumentService&) = delete;
    InstrumentService& operator=(const InstrumentService&) = delete;

    // Startup only, before accepting connections. This deliberately waits for I/O.
    // The profile is valid only for VnaPro NativeSimulator, not arbitrary drivers.
    bool bootstrapMockProfile(std::string& error);
    void poll();
    bool hasPending() const;
    std::vector<Operation> pendingOperations() const;
    bool tryAcquireControl(const std::string& owner = "SCPI");
    void beginReleaseControl(const std::string& owner);
    void releaseControl(const std::string& owner = "SCPI");
    std::string controlOwner() const;
    bool controlReleasing() const;
    InstrumentSnapshot snapshot() const;

    const InstrumentSettings& settings() const;
    std::string identity() const;
    Operation setStartHz(double value, std::uint64_t expectedRevision = 0U);
    Operation setStopHz(double value, std::uint64_t expectedRevision = 0U);
    Operation setPoints(unsigned value, std::uint64_t expectedRevision = 0U);
    Operation setIfBandwidthHz(double value, std::uint64_t expectedRevision = 0U);
    Operation applyConfiguration(ConfigPatch patch, std::uint64_t expectedRevision, const std::string& owner);
    Operation defineMeasurement(
        Measurement measurement, std::uint64_t expectedRevision = 0U, const std::string& owner = "SCPI");
    Operation deleteMeasurement(
        const std::string& name, std::uint64_t expectedRevision = 0U, const std::string& owner = "SCPI");
    Operation setTriggerSource(TriggerSource source, const std::string& owner = "SCPI");
    Operation setSweepMode(SweepMode mode, const std::string& owner = "SCPI");
    Operation runSingle(const std::string& owner);
    Operation runContinuous(const std::string& owner);
    Operation trigger(const std::string& owner = "SCPI");
    bool selectMeasurement(const std::string& name);
    bool setCurrentTriggerScope(bool current);
    DataResult stimulusAxis() const;
    DataResult measurementData() const;

    void recordError(int code, const std::string& message);
    InstrumentError popError();
    unsigned errorCount() const;
    void clearStatus();
    unsigned statusGeneration() const;
    void setEventEnable(unsigned value);
    unsigned eventEnable() const;
    void setServiceRequestEnable(unsigned value);
    unsigned serviceRequestEnable() const;
    unsigned readEventRegister();
    void markOperationComplete();
    unsigned statusByte(bool outputAvailable) const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace vna_service
#endif
