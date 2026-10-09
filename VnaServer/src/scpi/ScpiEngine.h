#ifndef VNAPRO_SCPI_ENGINE_H
#define VNAPRO_SCPI_ENGINE_H

#include <memory>
#include <string>

namespace vna_service {
class InstrumentService;
}
namespace vna_scpi {

// One engine per admitted TCP connection. All methods run on the service executor.
// Keep polling after disconnect until canReleaseControl(), then destroy it.
class ScpiEngine {
public:
    explicit ScpiEngine(vna_service::InstrumentService& service);
    ~ScpiEngine();
    ScpiEngine(const ScpiEngine&) = delete;
    ScpiEngine& operator=(const ScpiEngine&) = delete;
    bool submitLine(const std::string& line);
    void poll();
    std::string takeOutput();  // One complete response message, including LF.
    bool hasPending() const;
    bool hasControl() const;
    void setTransportOutputPending(bool pending);  // MAV includes the network send buffer.
    void disconnect();
    bool canReleaseControl() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace vna_scpi
#endif
