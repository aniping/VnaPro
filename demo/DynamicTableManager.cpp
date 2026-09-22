//
// Created by lorrion on 2026/9/22.
//

#include "DynamicTableManager.h"

bool DynamicTableManager::DynRxPowerTableMake(int sendPort, double *iqData, int length) {
    std::vector<MeasType>& waveTypeList = waveTypeDict[sendPort];
    TypeComplexMap measData;
    if (iqData == nullptr || length <= 0 || length < channelParam->sweepPoint * ALL_CHAN_IQ_DATA_NUM) {
        loge("iq data is invalid");
        return false;
    }

    for (int i=0;i<waveTypeList.size();i++) {
        MeasType measType = waveTypeList[i];
        measData[measType] = new Complex[channelParam->sweepPoint];
        for (int j = 0; j < channelParam->sweepPoint; j++) {
            measData[measType][j].real(iqData[j*ALL_CHAN_IQ_DATA_NUM + i * 2]);
            measData[measType][j].imag(iqData[j*ALL_CHAN_IQ_DATA_NUM + i * 2 + 1]);
        }
    }
    bool ret = DynTableDataGen(sendport, measData);
    for (auto& data : measData) {
        delete[] data.second;
    }
    measData.clear();
    return ret;
}

bool DynamicTableManager::DynTableDataGen(int sendPort, TypeComplexMap &measData) {
    for (const int& port: channelParam->recvPortList) {
        if (port == sendPort) {
            continue
        }
        auto it = strToMeasType.find("B"+std::to_string(port) + "P" + std::to_string(sendPort));
        if (it == strToMeasType.end()) {
            return false;
        }
        MeasType measType = it->second;
        auto powerTableIt = powerTableMap.find(measType);
        if (powerTableIt == powerTableMap.end()) {
            return false;
        }
        std::vector<TableItem> tableMap;
        std::vector<TableItem> findTable;
        if (!overPowerFreq.empty() && curTableMap.find(measType) != curTableMap.end()) {
            findTable = std::move(curTableMap[measType]);
        }
        DynCompensationCalculate(port, measData[measType], findTable, powerTableIt->second, tableMap);
        curTableMap[measType] = std::move(tableMap);
        if (!overPowerFreq.empty()) {
            autoTableMap[measType] = std::move(findTable);
        }
    }
    return true;
}

bool DynamicTableManager::DynCompensationCalculate(int port, ComplexPtr data,
    std::vector<TableItem>& findTable,
    const std::vector<TableItem>& powerTable,
    std::vector<TableItem>& tableMap) {
    for (int j = 0; j < channelParam->sweepPoint; j++) {
        TableItem item = powerTable[j];
        TableItem dynItem;
        Complex iqData = data[j];
        iqData.real(iqData.real() / (static_cast<double>(FREQ_ADC_SMP) * channelParam->ifbw));
        iqData.imag(iqData.imag() / (static_cast<double>(FREQ_ADC_SMP) * channelParam->ifbw));
        double ifPower = 10 * log10(iqData.real() * iqData.real() + iqData.imag() * iqData.imag()) + ADC_UNIT_POWER_DB;
        if (!overPowerFreq.empty()) {
            findTable[j].compenValue = (j < overPowerFreq[port - 1] ? findTable[j].compenValue : 0);
            ifPower += findTable[j].compenValue;
        }
        if (ifPower < ADC_MIN_THRESH) {
            dynItem.compenValue = -1 *(LNA_AMP_FACTOR + item.value + item.compenValue);
            dynItem.isAmp = true;
            dynItem.value = 0;
        } else if (ifPower > ADC_MAX_THRESH) {
            dynItem.compenValue = ATT_MAX_THRESH;
            dynItem.isAmp = false;
            dynItem.value = ATT_MIN_VAL;
        } else {
            dynItem = item;
        }
        tableMap.push_back(dynItem);
    }
}

void DynamicTableManager::DynDataCompen(SafeIQMap& measData, MeasType measType, EventContent& content) {
    if (channelParam == nullptr || curTableMap.empty()) {
        return;
    }
    if (curTableMap.find(measType) == curTableMap.end()) {
        return;
    }
    std::vector<TableItem>* table = &curTableMap[measType];
    if ((*tabel).size() != channelParam->sweepPoint) {
        return;
    }
    if (dynTypeDict.find(measType) != dynTypeDict.end()) {
        return;
    }
    auto dynType = dynTypeDict.find(measType)->second;
    if (!overPowerFreq.empty() && autoTableMap.find(measType) != autoTableMap.end() &&
        autoTableMap[measType].size() == channelParam->sweepPoint) {
        table = &autoTableMap[measType];
    }
    for (int i = 0; i < channelParam->sweepPoint; i++) {
        measData.SetIQ(dynType, i, pow(10, (*table)[i].compenValue / 20), 0);
    }
    content.meastypeList.push_back(dynType);
    if (!overPowerFreq.empty() && autoTableMap.find(measType) != autoTableMap.end()) {
        autoTableMap.erase(measType);
    }
}