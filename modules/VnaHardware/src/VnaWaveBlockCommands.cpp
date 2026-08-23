#include "VnaSessionController.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#include "VnaFrameTracker.h"
#include "VnaLogger.h"

namespace vna::detail {
namespace {

    VnaStatusV1 blockStatus(std::uint32_t code, const char *description)
    {
        VnaStatusV1 result{};
        result.code = code;
        result.message = VnaStringViewV1{description, std::char_traits<char>::length(description)};
        return result;
    }

    const char *frameRejection(VnaFrameReservationResult result)
    {
        static constexpr const char *descriptions[]{"reserved",         "unknown layout",  "invalid point range",
                                                    "unexpected point", "duplicate frame", "out-of-order frame"};
        const std::size_t index = static_cast<std::size_t>(result);
        return index < (sizeof(descriptions) / sizeof(descriptions[0])) ? descriptions[index] : "unknown reason";
    }

}  // namespace

VnaStatusV1 VnaSessionController::acceptBlock(VnaWaveBlock block)
{
    std::shared_ptr<VnaFrameTracker> tracker;
    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        // 扫描完成标志与 sweepId 双重过滤，阻止旧驱动线程把迟到数据注入下一次扫描。
        if (completionEmitted_ || (block.sweepId() != activeSweep_) || !frameTracker_) {
            return blockStatus(VNA_STATUS_INVALID_STATE, "sweep is not accepting wave blocks");
        }
        tracker = frameTracker_;
    }
    const LayoutId layoutId = block.layoutId();
    const std::uint32_t firstPoint = block.firstPointIndex();
    const std::uint32_t pointCount = block.pointCount();
    const VnaFrameReservationResult reservation = tracker->reserve(layoutId, firstPoint, pointCount);
    if (reservation != VnaFrameReservationResult::Reserved) {
        recordSweepError(VnaErrorCode::ProtocolError);
        VnaLogger::write(
            VnaLogLevel::Error,
            "驱动提交非法波帧，deviceId=" + descriptor_.deviceId + "，layoutId=" + std::to_string(layoutId) +
                "，firstPoint=" + std::to_string(firstPoint) + "，pointCount=" + std::to_string(pointCount) +
                "，原因=" + frameRejection(reservation));
        return blockStatus(VNA_STATUS_PROTOCOL_ERROR, "wave block does not match the prepared frame layout");
    }
    // 发布发生在 dataMutex_ 之外，订阅回调重入查询会话状态时不会死锁。
    if (!dispatcher_->postWaveBlock(std::move(block))) {
        tracker->release(layoutId, firstPoint, pointCount);
        recordSweepError(VnaErrorCode::DataLoss);
        return blockStatus(VNA_STATUS_DATA_LOSS, "wave dispatch queue is full");
    }
    return blockStatus(VNA_STATUS_OK, "");
}

bool VnaSessionController::recordSweepError(VnaErrorCode code)
{
    std::lock_guard<std::mutex> lock(dataMutex_);
    if (sweepErrorCode_ != VnaErrorCode::None) {
        return false;
    }
    sweepErrorCode_ = code;
    return true;
}

}  // namespace vna::detail
