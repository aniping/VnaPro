//
// Created by lorrion on 2026/9/20.
//

#ifndef VNAPRO_VNAHALBASE_H
#define VNAPRO_VNAHALBASE_H

#include "VnaHalBase.h"
#include "DriverManager.h"
#include "table/PowerTableManager.h"
#include "table/DynamicTableManager.h"

namespace VnaHalLayer {

    class VnaHalImpl : public VnaHalBase {
    public:
        VnaHalImpl();

        ~VnaHalImpl() = default;

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
        virtual bool GetDynamicExpandData(int chIndex) = 0;

        // 生成快扫寄存器表
        virtual bool GetQuickSweepRegData(int chIndex) = 0;

        // 获取IDN
        virtual bool GetIDN(std::string& idn) = 0;

        //单板固件升级
        virtual bool FirmwareUpgrade(const std::string& firmwareId, const std::string& path) = 0;

    private:

        VnaDriver::DriverManager driverManager;

        std::function<void(EventContent)> dataUpdateEvent;

        std::atomic<bool> isTrigger;

        std::map<int, ChannelStimParamPtr> channelParamMap;

        ChannelStimParamPtr channelParam;

        PowerTableManagerPtr powerTableManager;

        DynamicTableManagerPtr dynamicTableManager;

        std::string deviceInfo;

        bool PowerLimitDetect(int port, std::vector<uint16_t>& overFreqIndex);

        void GetSweepData(SafeIQMap& MeasData, EventContent content);

        bool SetSweepData(bool isDynamicExpand);
    };
}

#endif //VNAPRO_VNAHALBASE_H