#include "SimulatorDevice.h"

#include <utility>

namespace vna::simulator {

SimulatorDevice::PrepareTransaction::PrepareTransaction(SimulatorDevice &device)
    : device_(device),
      wasPrepared_(device.prepared_),
      points_(std::move(device.points_)),
      layoutWaves_(std::move(device.layoutWaves_)),
      layoutExcitations_(std::move(device.layoutExcitations_)),
      layoutViews_(std::move(device.layoutViews_))
{
    // 移走旧配置而不是复制大计划；事务存活期间 device 暂时处于未准备状态。
    device_.prepared_ = false;
}

SimulatorDevice::PrepareTransaction::~PrepareTransaction()
{
    if (!committed_) {
        // 任一校验返回或异常都会走到这里，把完整旧配置按原所有权恢复。
        device_.points_ = std::move(points_);
        device_.layoutWaves_ = std::move(layoutWaves_);
        device_.layoutExcitations_ = std::move(layoutExcitations_);
        device_.layoutViews_ = std::move(layoutViews_);
        device_.prepared_ = wasPrepared_;
    }
}

void SimulatorDevice::PrepareTransaction::commit()
{
    // 新布局全部建好后才发布 prepared_，随后析构器不再回滚旧配置。
    device_.prepared_ = true;
    committed_ = true;
}

}  // namespace vna::simulator
