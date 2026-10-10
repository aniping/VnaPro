#ifndef VNAPRO_VNACORE_INCLUDE_VNA_CORE_CORESTATE_H
#define VNAPRO_VNACORE_INCLUDE_VNA_CORE_CORESTATE_H

namespace vna_core {

enum class CoreState {
    Uninitialized,
    Ready,
    Running,
    Faulted,
    Closed
};
enum class RunMode {
    Manual,
    Continuous
};

}  // namespace vna_core

#endif
