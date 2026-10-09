#ifndef VNAPRO_VNA_SERVICE_INSTRUMENT_SERVICE_H
#define VNAPRO_VNA_SERVICE_INSTRUMENT_SERVICE_H

#include <cstdint>
#include <future>
#include <memory>
#include <string>
#include <vector>

namespace vna_core {
class AcquisitionCore;
}

namespace vna_service {

enum class TriggerSource { Immediate, Manual };
enum class SweepMode { Hold, Continuous, Single };

struct Measurement {
    std::string name;
    char receiverKind = 'a';
    unsigned receiverPort = 1U;
    unsigned sourcePort = 1U;
    std::string parameter() const;
};

struct InstrumentSettings {
    double startHz = 1000000.0;
    double stopHz = 100000000.0;
    unsigned points = 201U;
    double ifBandwidthHz = 1000.0;
    TriggerSource triggerSource = TriggerSource::Immediate;
    SweepMode sweepMode = SweepMode::Hold;
    bool currentTriggerScope = false;
    std::vector<Measurement> measurements;
    std::string selectedMeasurement;
    std::uint64_t revision = 0U;
};

struct OperationResult {
    bool ok = false;
    int errorCode = 0;
    std::string message;
};
using Operation = std::shared_future<OperationResult>;

struct InstrumentError {
    int code = 0;
    std::string message = "No error";
};

struct DataResult {
    bool ok = false;
    std::string error;
    std::vector<double> values;
};

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
    bool tryAcquireControl();
    void releaseControl();

    const InstrumentSettings& settings() const;
    std::string identity() const;
    Operation setStartHz(double value, std::uint64_t expectedRevision = 0U);
    Operation setStopHz(double value, std::uint64_t expectedRevision = 0U);
    Operation setPoints(unsigned value, std::uint64_t expectedRevision = 0U);
    Operation setIfBandwidthHz(double value, std::uint64_t expectedRevision = 0U);
    Operation defineMeasurement(Measurement measurement, std::uint64_t expectedRevision = 0U);
    Operation deleteMeasurement(const std::string& name, std::uint64_t expectedRevision = 0U);
    Operation setTriggerSource(TriggerSource source);
    Operation setSweepMode(SweepMode mode);
    Operation trigger();
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
