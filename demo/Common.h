//
// Created by lorrion on 2026/9/20.
//

#ifndef VNAPRO_COMMON_H
#define VNAPRO_COMMON_H
#include <functional>

enum class SweepType {
    SWEP_LINEAR,
    SWEP_LOG,
    SWEP_POWER,
    SWEP_CWTIME,
    SWEP_SEGMENT
};

enum class TriggerSource {
    TRIG_SOURCE_INTERNAL,
    TRIG_SOURCE_MANUAL,
    TRIG_SOURCE_EXTERNAL, // 硬件不支持
};

enum class MeasType {
    MEAS_S11,
    MEAS_S12,
    MEAS_S13,
    MEAS_S14,
    MEAS_S21,
    MEAS_S22,
    MEAS_S23,
    MEAS_S24,
    MEAS_S31,
    MEAS_S32,
    MEAS_S33,
    MEAS_S34,
    MEAS_S41,
    MEAS_S42,
    MEAS_S43,
    MEAS_S44,

    MEAS_A1P1,
    MEAS_B1P1,
    MEAS_A2P1,
    MEAS_B2P1,
    MEAS_A3P1,
    MEAS_B3P1,
    MEAS_A4P1,
    MEAS_B4P1,
    MEAS_A1P2,
    MEAS_B1P2,
    MEAS_A2P2,
    MEAS_B2P2,
    MEAS_A3P2,
    MEAS_B3P2,
    MEAS_A4P2,
    MEAS_B4P2,
    MEAS_A1P3,
    MEAS_B1P3,
    MEAS_A2P3,
    MEAS_B2P3,
    MEAS_A3P3,
    MEAS_B3P3,
    MEAS_A4P3,
    MEAS_B4P3,
    MEAS_A1P4,
    MEAS_B1P4,
    MEAS_A2P4,
    MEAS_B2P4,
    MEAS_A3P4,
    MEAS_B3P4,
    MEAS_A4P4,
    MEAS_B4P4,

    SYN_S11,
    SYN_S12,
    SYN_S13,
    SYN_S14,
    SYN_S21,
    SYN_S22,
    SYN_S23,
    SYN_S24,
    SYN_S31,
    SYN_S32,
    SYN_S33,
    SYN_S34,
    SYN_S41,
    SYN_S42,
    SYN_S43,
    SYN_S44,
};

using FreqNum = unsigned long long int;

struct SegmentInfo {
    FreqNum start;
    FreqNum stop;
    int pointNum;
};

struct EventContext {
    std::vector<MeasType> measTypeList;
    int chnIndex;
    int startLoc;
    int updateLen;
    bool isComplete;
};

constexpr int ALL_CHAN_IQ_DATA_NUM = 16;

using EventContext = struct EventContext;
using NotifyEvent = std::function<void(EventContext)>;
using Complex = std::complex<double>;
using ComplexPtr = Complex*;

using TypeComplexMap = std::unordered_map<MeasType, ComplexPtr>;

using TraceInfoParam = struct TraceInfoParam;
using TraceInfoParamPtr = std::shared_ptr<TraceInfoParam>;

struct ChannelStimParam {
    int chnIndex;
    FreqNum start;
    FreqNum stop;
    int ifbw;
    int sweepPoint;
    TriggerSource triggerSource;
    std::vector<int> sendPortList;
    std::vector<int> recvPortList;
    std::unordered_map<int, std::vector<MeasType>> sendPortSParamMap;
    std::unordered_map<int, std::vector<MeasType>> sendPortWaveMap;
    std::vector<TraceInfoParamPtr> trcInfoList;
    std::vector<FreqNum> pointFreqList;
    bool isCalibration;
    bool isCalDone;
    bool isAvgOpen;
    int avgFactor;
    bool isDynamicExpand;
    bool isQuickSweep;
    double power;
    SweepType sweepType;
    bool isSegmentEnable;
    std::vector<SegmentInfo> segmentList;
};

using ChannelStimParam = struct ChannelStimParam;
using ChannelStimParamPtr = std::shared_ptr<ChannelStimParam>;

#endif //VNAPRO_COMMON_H
