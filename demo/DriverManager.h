//
// Created by lorrion on 2026/9/22.
//

#ifndef VNAPRO_DRIVERMANAGER_H
#define VNAPRO_DRIVERMANAGER_H
#include <cstdint>

#if defined(WIN32) || defined(WIN64)
#include <windows.h>
#define FIND_ENTRY(HANDLE, name) GetProcAddress((HINSTANCE)HANDLE, name)
#endif

namespace VnaDriver {
    using BdInit = uint32_t(*)(uint32_t, uint32_t, uint32_t);
    using BspInit = uint32_t(*)();
    using GetIqData = uint32_t(*)(uint32_t, double[]);
    using SetSweepFreq = uint32_t(*)(uint64_t, uint64_t);
    using SetSweepPoints = uint32_t(*)(uint32_t);
    using SetSweepPort = uint32_t(*)(uint32_t);
    using SetSweepIfbw = uint32_t(*)(uint32_t);
    using StartLogicSweep = uint32_t(*)(uint32_t, uint32_t);
    using SetRxLink = uint32_t(*)(uint32_t);
    using SetRfPowerOn = uint32_t(*)(uint32_t);
    using PowerTableRegSet = uint32_t(*)(uint32_t, uint32_t, double[], uint32_t);
    using DynTableRegSet = uint32_t(*)(uint32_t, uint32_t, double[], uint32_t);
    using FastSweepModeSet = uint32_t(*)(uint32_t);
    using LogicLmxTableSet = uint32_t(*)(uint64_t, uint64_t, uint32_t);
    using GetIDN = uint32_t(*)(char [], int);
    using PowerLimitCheck = uint32_t(*)(uint32_t, uint32_t*, uint16_t [], uint32_t);
    using SweepDataDownload = uint32_t(*)(uint32_t, uint32_t[], uint32_t, double[], uint32_t);

    struct DriverFuncCall {
        BdInit bdInit;
        BspInit bspInit;
        GetIqData getIqData;
        SetSweepFreq setSweepFreq;
        SetSweepPoints setSweepPoints;
        SetSweepPort setSweepPort;
        SetSweepIfbw setSweepIfbw;
        StartLogicSweep startLogicSweep;
        SetRxLink setRxLink;
        SetRfPowerOn setRfPowerOn;
        PowerTableRegSet powerTableRegSet;
        DynTableRegSet dynTableRegSet;
        FastSweepModeSet fastSweepModeSet;
        LogicLmxTableSet logicLmxTableSet;
        GetIDN getIDN;
        PowerLimitCheck powerLimitCheck;
        SweepDataDownload sweepDataDownload;
    };

    struct FuncEntry {
        const char* name;
        void** funcPtr;
    };

    class DriverManager {
        bool LoadDriverFunctions() {
            if (!moduleHandle) {
                return false;
            }
            FuncEntry* funcTable[] = {
                {"DrvHwInit", (void**)&driverFuncs->bspInit},
                {"DrvGetSweepData", (void**)&driverFuncs->getIqData},
                {"DrvStartSweep", (void**)&driverFuncs->startLogicSweep},
                {"DrvActivateRxLink", (void**)&driverFuncs->setRxLink},
                {"DrvSetRfPowerOn", (void**)&driverFuncs->setRfPowerOn},
                {"DrvSetFastSweepMode", (void**)&driverFuncs->fastSweepModeSet},
                {"DrvGetIDN", (void**)&driverFuncs->getIDN},
                {"DrvCheckSweepPortPower", (void**)&driverFuncs->powerLimitCheck},
                {"DrvDataFrameDownload", (void**)&driverFuncs->sweepDataDownload},
            };
            for (auto& entry : funcTable) {
                *entry.funcPtr = (void*)FIND_ENTRY(moduleHandle, entry.name);
                if (*entry.funcPtr == nullptr) {
                    loge("Failed to find driver function %s", entry.name);
                    return false;
                }
            }
            logi("All driver functions loaded");
            return true;
        }
    }
}

#endif //VNAPRO_DRIVERMANAGER_H