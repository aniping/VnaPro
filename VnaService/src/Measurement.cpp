#include "vna_service/Measurement.h"

namespace vna_service {

std::string Measurement::parameter() const
{
    return std::string(1, receiverKind) + std::to_string(receiverPort) + "," + std::to_string(sourcePort);
}

}  // namespace vna_service
