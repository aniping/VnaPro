#ifndef VNAPRO_VNACORE_SRC_DEVICE_PLANPREPARATIONERROR_H
#define VNAPRO_VNACORE_SRC_DEVICE_PLANPREPARATIONERROR_H
#include <exception>

namespace vna_core {
namespace detail {

// This exception is reserved for failures before any plan-related device mutation.
class PlanPreparationError : public std::exception {
public:
    const char* what() const noexcept override
    {
        return "device plan buffer preparation failed";
    }
};

}  // namespace detail
}  // namespace vna_core

#endif
