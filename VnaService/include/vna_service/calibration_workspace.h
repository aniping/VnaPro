/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNA_SERVICE_CALIBRATION_WORKSPACE_H
#define VNAPRO_VNA_SERVICE_CALIBRATION_WORKSPACE_H

#include <memory>
#include <string>
#include <vector>

#include "vna_core/measurement_frame.h"
#include "vna_service/calibration.h"
#include "vna_service/instrument_profile.h"

namespace VnaService {
namespace Detail {
struct CalibrationPlanCache;
}

// 单执行线程校准工作区，不执行设备 I/O；候选复制后可原子提交。
class CalibrationWorkspace final {
public:
    // 每通道会话身份独立命名；复制已完成校准集时不能复用源通道的未来会话 ID。
    void SetChannelId(unsigned channelId) noexcept;
    std::uint64_t Revision() const noexcept;
    bool Collecting() const noexcept;
    bool SessionMatches(std::uint64_t id) const noexcept;
    bool SessionCompatible(const std::string &identity, const VnaCore::SweepPlan &plan) const noexcept;
    // 事务提交前准备实际轴；只读快照及帧计算绝不隐式插值或读取文件。
    void PrepareForPlan(const std::string &identity, const VnaCore::SweepPlan &plan);
    CalibrationSnapshot Snapshot(const std::string &identity, const VnaCore::SweepPlan &plan) const;
    VnaCalibration::CalibrationPtr Correction(const std::string &identity, const VnaCore::SweepPlan &plan) const;
    CalibrationSetPtr Find(std::uint64_t id) const;
    std::size_t StorageBytes() const noexcept;
    const std::vector<CalibrationSetPtr> &SavedSets() const noexcept;
    std::uint64_t SelectedSetId() const noexcept;
    // 仅全新空工作区可恢复；冻结输入别名，保留存储 ID，恢复后校正始终关闭。
    bool RestoreSaved(std::vector<CalibrationSetPtr> sets, std::uint64_t selectedSetId,
        const InstrumentProfile &profile, std::string &error);
    CalibrationPreviewResult Preview(std::uint64_t sessionId, const std::string &stepId) const;
    // 需求始终包含当前会话和选中集，关闭校正不强行缩减硬件计划。
    void ExtendPlan(VnaCore::SweepPlan &plan) const;
    bool Begin(const CalibrationChange &change, const std::string &identity, const VnaCore::SweepPlan &plan,
        const InstrumentProfile &profile, std::string &error);
    bool ValidateStep(std::uint64_t sessionId, const std::string &stepId, std::string &error) const;
    void SetCapturePending(bool value) noexcept;
    void CaptureFailed(const std::string &error);
    // 必须由服务在新触发与 Stop 屏障后交入完整原始帧，不读取已有校正集。
    bool Capture(const std::string &stepId, const VnaCore::MeasurementFrame &frame, std::string &error);
    bool Complete(const std::string &name, std::string &error);
    bool Cancel(std::uint64_t sessionId, std::string &error);
    bool ApplySaved(const CalibrationChange &change, const InstrumentProfile &profile, std::string &error);

private:
    struct Sample final {
        CalibrationStep step;
        std::vector<VnaCalibration::TwoPortNetwork> network;
    };

    CalibrationSnapshot CollectionSnapshot() const;
    void RefreshPrepared();
    bool CanRevise(std::string &error) const;
    void InitializeSession(
        const CalibrationChange &change, const std::string &identity, const VnaCore::SweepPlan &plan);
    bool CanComplete(const std::string &name) const;
    bool UpdateSaved(const CalibrationChange &change, const InstrumentProfile &profile, std::string &error);
    bool DeleteSet(std::uint64_t id, std::string &error);
    bool ImportSet(const CalibrationSetPtr &set, const InstrumentProfile &profile, std::string &error);
    bool Solve(VnaCalibration::CalibrationData &data, std::string &error) const;
    bool SolveFourPort(VnaCalibration::CalibrationData &data, std::string &error) const;
    void AddSteps(unsigned port);
    void AddPair(unsigned first, unsigned second);
    const Sample *FindSample(CalibrationStandard kind, unsigned first, unsigned second = 0U) const noexcept;
    CalibrationSet session_;
    VnaCalibration::CalibrationMethod method_ = VnaCalibration::CalibrationMethod::ONE_PORT;
    unsigned port1_ = 1U;
    unsigned port2_ = 0U;
    bool includeIsolation_ = false;
    std::uint64_t revision_ = 1U;
    std::uint64_t nextSessionId_ = 1U;
    std::uint64_t sessionNamespace_ = 0U;
    std::uint64_t nextSetId_ = 1U;
    bool collecting_ = false;
    bool capturePending_ = false;
    bool enabled_ = false;
    std::string error_;
    std::vector<Sample> samples_;
    std::vector<CalibrationSetPtr> sets_;
    std::size_t storageBytes_ = 0U;
    std::uint64_t selectedSetId_ = 0U;
    std::shared_ptr<const Detail::CalibrationPlanCache> planCache_;
};

}  // 命名空间 VnaService
#endif
