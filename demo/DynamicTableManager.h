//
// Created by lorrion on 2026/9/22.
//

#ifndef VNAPRO_DYNAMICTABLEMANAGER_H
#define VNAPRO_DYNAMICTABLEMANAGER_H

namespace VnaHalLayer {
    constexpr uint64_t FREQ_ADC_SMP = 61440000;
    constexpr double ADC_UNIT_POWER_DB = -80.308734;

    class DynamicTableManager {

    private:
        bool DynRxPowerTableMake(int sendPort, double *iqData, int length);

    };
}


#endif //VNAPRO_DYNAMICTABLEMANAGER_H