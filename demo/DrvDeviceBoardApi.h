//
// Created by Administrator on 2026/10/12.
//

#ifndef DRVDEVICEBOARDAPI_H
#define DRVDEVICEBOARDAPI_H

// 硬件初始化
EXPORT_API uint32_t DrvHwInit();
// 下发扫描频点数组 仅下发不生效
EXPORT_API void DrvSetFreqList(uint64_t freqList[], uint32_t length);
// 下发扫描功率数组 仅下发不生效
EXPORT_API void DrvSetPowerList(uint64_t powerList[], uint32_t length);
// 下发扫描中频数组 仅下发不生效
EXPORT_API void DrvSetIfbwList(uint64_t ifbwList[], uint32_t length);
// 下发扫描工作的端口数组 仅下发不生效
EXPORT_API void DrvSetWorkPorts(uint32_t ports[], uint32_t number);
// 组帧生效
EXPORT_API uint32_t DrvDataFrameDownload();
// 下发外部射频端口是否打开 立即生效
EXPORT_API uint32_t DrvSetRfPowerOn(uint32_t isOn);
// 下发开始扫描指令 立即生效 不阻塞，不允许多线程调用，必须等上一次扫描完成才允许调用
EXPORT_API uint32_t DrvStartSweep();
// 下发获取数据指令 可以取回部分完成数据点 若未扫描完部分数据点则会阻塞 直到请求数据全部采集
EXPORT_API uint32_t DrvGetSweepData(uint32_t start_index, uint32_t fNum, double data[], int dataLen);




#endif //DRVDEVICEBOARDAPI_H
