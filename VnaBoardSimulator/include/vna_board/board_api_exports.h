/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_VNA_BOARD_BOARD_API_EXPORTS_H
#define VNAPRO_VNA_BOARD_BOARD_API_EXPORTS_H

#include "vna_board/board_api_contract.h"

// 原生导出独立声明，避免新旧驱动同名但不同签名的 C 函数污染同一个加载器编译单元。
extern "C" {

// 初始化后关闭射频、清空暂存和已提交计划；无效环境配置不破坏原状态。
VNA_BOARD_EXPORT std::uint32_t VNA_BOARD_CALL DrvHwInit();
// 数组立即复制，仅更新暂存项；非法暂存项会导致后续组帧失败。
VNA_BOARD_EXPORT void VNA_BOARD_CALL DrvSetFreqList(std::uint64_t freqList[], std::uint32_t length);
VNA_BOARD_EXPORT void VNA_BOARD_CALL DrvSetPowerList(double powerList[], std::uint32_t length);
VNA_BOARD_EXPORT void VNA_BOARD_CALL DrvSetIfBwList(std::uint32_t ifbwList[], std::uint32_t length);
VNA_BOARD_EXPORT void VNA_BOARD_CALL DrvSetWorkPorts(std::uint32_t ports[], std::uint32_t number);
// 统一校验并提交全部列表；扫描期间拒绝组帧，失败时保留旧计划。
VNA_BOARD_EXPORT std::uint32_t VNA_BOARD_CALL DrvDataFrameDownload();
// 关闭射频会取消扫描、唤醒读者并回收工作线程；卸载 DLL 前必须先调用关闭射频。
VNA_BOARD_EXPORT std::uint32_t VNA_BOARD_CALL DrvSetRfPowerOn(std::uint32_t isOn);
// 异步启动整个已提交计划；必须串行调用，上一扫描完成后才可再次启动。
VNA_BOARD_EXPORT std::uint32_t VNA_BOARD_CALL DrvStartSweep();
// 排列为 [工作源端口顺序][本次请求的频点][接收槽索引 0..3][A实,A虚,B实,B虚]，接收槽对应物理端口 1..4。
// dataLen 是 double 元素容量；失败不修改输出；同一扫描可分段、重复读取。
VNA_BOARD_EXPORT std::uint32_t VNA_BOARD_CALL DrvGetSweepData(
    std::uint32_t start_index, std::uint32_t fNum, double data[], int dataLen);

}  // C 链接接口
#endif  // VNAPRO_VNA_BOARD_BOARD_API_EXPORTS_H
