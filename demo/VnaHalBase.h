//
// Created by lorrion on 2026/9/20.
//

#ifndef VNAPRO_VNAHALBASE_H
#define VNAPRO_VNAHALBASE_H

#include "Common.h"
#include "SafeIQMap.h"

namespace VnaHalLayer {
    enum HalType {
        HAL_TYPE_IMPL,
        HAL_TYPE_SIM,
        HAL_TYPE_FILTER,
    };

    class VnaHalBase {
    public:
        // 初始化仪表
        virtual bool InitInstrument(const NotifyEvent event) = 0;

        // 是否使能射频功率
        virtual bool SetRfPowerOn(bool isOn) = 0;

        // 获取测量数据
        virtual bool GetMeasData(SafeIQMap& MeasData, int chnIndex) = 0;

        // 手动触发一次
        virtual bool ManualTriggerOnce() = 0;

        // 多通道激励下发 这里问题最大 应该只下发需要的参数而不是这一堆ChannelStimParamPtr
        virtual bool MultipleChannelSet(std::vector<ChannelStimParamPtr> paramList) = 0;

        // 获取大动态扩展数据
        virtual bool GetDynamicExpandData(int chnIndex) = 0;

        // 生成快扫寄存器表
        virtual bool GetQuickSweepRegData(int chnIndex) = 0;

        // 获取IDN
        virtual bool GetIDN(std::string& idn) = 0;

        //单板固件升级
        virtual bool FirmwareUpgrade(const std::string& firmwareId, const std::string& path) = 0;

        // 具体的实现（没有对上层暴露）
        /*
        bool SetSweepData(bool isDynamicExpand) {
            uint32_t tableDataLen = 9 * channelParam->pointFreqList.size();
            uint32_t paramDataLen = 6 * channelParam->pointFreqList.size() + 1;
            auto * tableData = new double[tableDataLen];
            auto * paramData = new uint32_t[tableDataLen];
            // tableData 是功率校准表，读取就行，你不用管
            bool ret = true;
            for (const int& port: channelParam->sendPortList) {
                int count = 0;
                paramData[count++] = channelParam->isQuickSweep ? 1:0;
                for (int i=0; i < channelParam->pointFreqList.size(); i++) {
                    constexpr int FREQ_DATA_HIGH_32BIT = 32;
                    paramData[count++] = channelParam->pointFreqList[i] >> FREQ_DATA_HIGH_32BIT;
                    paramData[count++] = channelParam->pointFreqList[i] & 0xFFFFFFFF;
                    paramData[count++] = port - 1;
                    paramData[count++] = isDynamicExpand ? GetLinkConfig(port, i) : 0; // 你不用管，占位就行
                    paramData[count++] = channelParam->ifbw;
                    paramData[count++] = isDynamicExpand ? 1 : 0;
                }
            }
            SweepDataDownload(paramData, paramDataLen, tableData, tableDataLen);
        }
        */

    };

    using VnaHalBasePtr = std::shared_ptr<VnaHalBase>;
}

#endif //VNAPRO_VNAHALBASE_H
