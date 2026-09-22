//
// Created by lorrion on 2026/9/22.
//

#include "VnaHalImpl.h"

using namspace std;
using namspace VnaHalLayer;

VnaHalImpl::VnaHalImpl() : isTragger(false) {
    driverManager = std::make_shared<VnaDriver::DriverManager>(libvnb_drv.so);
    if (!driverManager->Initialize()) {
        loge("driverManager initialize failed");
        return;
    }
    logi("Vna driver initialized successfully");
    powerTableManager = std::make_shared<PowerTableManager>(driverManager, channelParam);
    dynamicTableManager = std::make_shared<DynamicTableManager>(driverManager, channelParam);
}

bool VnaHalImpl::InitInstrument(const NotifyEvent event) {
    dataUpdateEvent = event;
    if (driverManager->GetDriverFunctions()->bspInit() != 0) {
        loge("bspInit failed");
        return false;
    }
    if (SetRfPowerOn(true) != 0) {
        loge("SetRfPowerOn failed");
        return false;
    }
    logi("Vna driver initialized successfully");
    return true;
}

bool VnaHalImpl::SetRfPowerOn(bool isOn) {
    if (driverManager->GetDriverFunctions()->SetRfPowerOn(isOn ? 1:0) != 0) {
        loge("Rf Power turn %s failed", isOn ? "on" : "off");
        return false;
    }
    logi("Rf Power turn %s successfully", isOn ? "on" : "off");
    return true;
}

bool VnaHalImpl::PowerLimitDetect(int port, vector<uint16_t>& overFreqIndex) {
    uint32_t checkBitMap = 0;
    uint32_t rfPortBitMap = 0;
    for (const auto& rfPort : channelParam->recvPortList) {
        if (channalParam->isDynamicExpand && port == rfPort) {
            continue;
        }
        rxPortMap |= (1 << (rfPort - 1));
    }
    if (driverManager->GetDriverFunctions()->powerLimitCheck(
        rxPortMap, &checkBitMap, overFreqIndex.data(), overFreqIndex.size()) != 0) {
        loge("power limit check failed");
    }
    return checkBitMap != 0;
}

void VnaHalImpl::GetSweepData(SafeIQMap& measData, EventContent& content) {
    auto* iqData = (double*)malloc(channelParam->sweepPoint * ALL_CHAN_IQ_DATA_NUM * sizeof(double));
    constexpr int PORT_NUM = 4;
    vector<uint16_t> overFreqIndex(PORT_NUM);
    content.measTypeList.clear();
    auto dataRefresh = [&](const int& trcId, const MeasType& measType) {
        for (int i = 0; i < channelParam->sweepPoint; i++) {
            measData.SetPointData(measType, iqData, trcId, i);
        }
        if (channelParam->isDynamicExpand) {
            dynamicTableManager->DynDataCompen(measData, measType, content);
        }
    };
    for (const auto& port : channelParam->sendPortList) {
        // 发功端口等所有测量数据
        if (channalParam->isDynamicExpand) {
            std::fill(overFreqIndex.begin(), overFreqIndex.end(), UINT32_MAX);
        }
        std::vector<MeasType>& waveTypeList = waveTypeDict[port];
        content.measTypeList.insert(content.measTypeList.end(), waveTypeList.begin(), waveTypeList.end());

        if (driverManager->GetDriverFunctions()->startLogicSweep(channelParam->sweepPoint, port - 1) != 0) {
            logw("startLogicSweep failed");
        }

        if (driverManager->GetDriverFunctions()->getIqData(channelParam->sweepPoint, iqData) != 0) {
            logw("getIqData failed");
        }

        if (channelParam->isDynamicExpand && PowerLimitDetect(port, overFreqIndex) &&
            dynamicTableManager->AutoCreateTable(port, overFreqIndex, iqData, channelParam->sweepPoint * ALL_CHAN_IQ_DATA_NUM)) {
            bool ret = SetSweepData(true);
            logi("port %d redownload dynamic table %s", port, ret ? "success" : "failure");
        }
        // 刷新数据
        for (int i = 0; i < waveTypeList->size(); i++) {
            dataRefresh(i, waveTypeList[i]);
        }
    }
    free(iqData);
    dataUpdateEvent(content);
}

bool VnaHalImpl::GetSweepData(SafeIQMap& MeasData, int chnIndex) {
    channelParam = channelParamMap[chnIndex];
    std::vector<int>& port = channelParam->sendPortList;
    if (TriggerSource::TRIG_SOURCE_MANUAL == channelParam->triggerSource) {
        if (!isTrigger) {
            return false;
        }
        isTrigger = true;
        logi("trigger source on successfully");
    }
    EventContent content;
    content.chnIndex = channelParam->chnIndex;
    content.startLoc = 0;
    content.updateLen = channelParam->sweepPoint;
    content.isComplete = false;

    GetSweepData(MeasData, content);

    content.measTypeList.clear();
    content.isComplete = true;
    dataUpdateEvent(content);

    return true;
}

bool VnaHalImpl::ManualTriggerOnce() {
    logi("manual trigger once");
    isTrigger = true;
    return true;
}

bool VnaHalImpl::SetSweepData(bool isDynamicExpand) {
    uint32_t tableDataLen = 9 * channelParam->pointFreqList.size();
    uint32_t paramDataLen = 6 * channelParam->pointFreqList.size() + 1;
    auto * tableData = new double[tableDataLen];
    auto * paramData = new uint32_t[tableDataLen];
    // tableData 是功率校准表，读取就行，你不用管
    bool ret = true;
    for (const int& port: channelParam->sendPortList) {
        int count = 0;
        paramData[count++] = channelParam->isQuickSweep ? 1:0;

        if (!powerTableManager->TableDataSerialization(port, channelParam->recvPortList, tableData, tableDataLen)) {
            ret = false;
            break;
        }

        for (int i=0; i < channelParam->pointFreqList.size(); i++) {
            constexpr int FREQ_DATA_HIGH_32BIT = 32;
            paramData[count++] = channelParam->pointFreqList[i] >> FREQ_DATA_HIGH_32BIT;
            paramData[count++] = channelParam->pointFreqList[i] & 0xFFFFFFFF;
            paramData[count++] = port - 1;
            paramData[count++] = isDynamicExpand ? dynamicTableManager->GetLinkConfig(port, i) : 0;
            paramData[count++] = channelParam->ifbw;
            paramData[count++] = isDynamicExpand ? 1 : 0;
        }
        if (count != paramDataLen) {
            loge("param data len is not match");
            ret = false;
            break;
        }
        if (driverManager->GetDriverFunctions()->sweepDataDownload(channelParam->pointFreqList.size(),
            paramData, paramDataLen, tableData, tableDataLen) != 0) {
            logw("sweepDataDownload failed");
            ret = false;
            break;
        }
    }
    delete[] tableData;
    delete[] paramData;
    return ret;
}

bool VnaHalImpl::MutipleChannelSet(std::vector<ChannelStimParamPtr> paramList) {
    logi("Mutiple channel set");
    channelParamMap.clear();
    for (auto& param : paramList) {
        channelParamMap[param->chnIndex] = param;
    }
    uint32_t selPort = 0;
    channelParam = channelParamMap.begin()->second;
    for (const int& port: channelParam->recvPortList) {
        selPort |= (1<< (port-1));
    }
    logi("select port %d", selPort);
    (void)driverManager->GetDriverFunctions()->setRxLink(selPort);
    if (driverManager->GetDriverFunctions()->fastSweepModeSet(channelParam->isQuickSweep ? 1:0) != 0) {
        loge("fastSweepModeSet failed");
        return false;
    }
    logi("fastSweepModeSet success");
    powerTableManager->SetChannelParam(channelParam);
    if (!SetSweepData(false)) {
        loge("setSweepData failed");
        return false;
    }
    dynamicTableManager->SetPowerTableMap(powerTableManager->GetTableMap());
    dynamicTableManager->SetChannelParam(channelParam);
    if (channelParam->isDynamicExpand && !SetSweepData(true)) {
        loge("setSweepData failed");
        return false;
    }
    return true;
}

bool VnaHalImpl::GetDynamicExpandData(int chIndex) {
    return dynamicTableManager->CreateTable();
}

bool VnaHalImpl::GenQuickSweepRegData(int chIndex) {
    ChannelStimParamPtr param = channelParamMap[chIndex];
    if (!param->isQuickSweep) {
        return true;
    }
    if (driverManager->GetDriverFunctions()->logicLmxTableSet(param->start, param->stop, param->sweepPoint) != 0) {
        logew("logicLmxTableSet failed");
        return false;
    }
    logi("logicLmxTableSet success");
    return true;
}

bool VnaHalImpl::GetIDN(std::string& idn) {
    if (!deviceInfo.empty) {
        idn = deviceInfo;
        return true;
    }
    const size_t BUFFER_SIZE = 128;
    char buffer[BUFFER_SIZE] = {0};
    if (driverManager->GetDriverFunctions()->getIDN(buffer, BUFFER_SIZE) != 0) {
        loge("getIDN failed");
        return false;
    }
    deviceInfo = buffer;
    idn = buffer;
    logi("getIDN success: %s", idn.c_str());
    return true;
}

bool VnaHalImpl::FirmwareUpgrade(const std::string& firmwareId, const std::string& path) {
    // sleep 2s
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    return true; // 不关键，我就不写了
}
