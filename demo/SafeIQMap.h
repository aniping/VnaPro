//
// Created by lorrion on 2026/9/20.
//

#ifndef VNAPRO_SAFEIQMAP_H
#define VNAPRO_SAFEIQMAP_H
#include <complex>

#include "Common.h"

class SafeIQMap {
public:
    void SetPointData(MeasType tMeas, double* iqData, int trcId, int pointId) {
        std::lock_guard<std::mutex> lock(mutex);
        measData[tMeas][pointId].real(iqData[pointId * ALL_CHAN_IQ_DATA_NUM + trcId * 2]);
        measData[tMeas][pointId].imag(iqData[pointId * ALL_CHAN_IQ_DATA_NUM + trcId * 2 + 1]);
    }

    Complex GetIQ(MeasType tMeas, int pId) {
        std::lock_guard<std::mutex> lock(mutex);
        return measData[tMeas][pId];
    }

    void SetIQ(MeasType tMeas, int pId, double real, double imag) {
        std::lock_guard<std::mutex> lock(mutex);
        measData[tMeas][pId].real(real);
        measData[tMeas][pId].imag(imag);
    }

    void Clean() {
        std::lock_guard<std::mutex> lock(mutex);
        for (const auto& kv : measData) {
            delete[] kv.second;
        }
        measData.clear();
    }

    void Put(MeasType tMeas, Complex* ptr) {
        std::lock_guard<std::mutex> lock(mutex);
        measData[tMeas] = ptr;
    }

    void Copy(MeasType tMeas, TypeComplexMap& target, int from, int to) {
        std::lock_guard<std::mutex> lock(mutex);
        std::copy(measData[tMeas] + from, measData[tMeas] + to, target[tMeas] + from);
    }

private:
    std::mutex mutex;

    std::unordered_map<MeasType, ComplexPtr> measData;
};

#endif //VNAPRO_SAFEIQMAP_H
