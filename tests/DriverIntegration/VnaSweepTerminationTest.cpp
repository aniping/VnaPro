#include <gtest/gtest.h>

#include <array>

#include "VnaSweepTermination.h"

namespace {

struct TerminationCase {
    vna::VnaErrorCode workerError;
    vna::VnaErrorCode recordedError;
    vna::SweepTerminationReason expected;
};

TEST(VnaSweepTerminationTest, ConcreteFaultTakesPriorityOverOrdinaryWorkerResult)
{
    using Error = vna::VnaErrorCode;
    using Reason = vna::SweepTerminationReason;
    const std::array<TerminationCase, 6> cases{
        TerminationCase{Error::ConnectionLost, Error::None, Reason::ConnectionLost},
        TerminationCase{Error::Timeout, Error::None, Reason::Timeout},
        TerminationCase{Error::DataLoss, Error::None, Reason::DataLoss},
        TerminationCase{Error::HardwareFault, Error::None, Reason::HardwareFault},
        TerminationCase{Error::Cancelled, Error::ProtocolError, Reason::HardwareFault},
        TerminationCase{Error::None, Error::DataLoss, Reason::DataLoss}};

    for (const auto &item : cases) {
        EXPECT_EQ(vna::detail::normalizeSweepTermination(item.workerError, item.recordedError, 5, 5), item.expected);
    }
}

TEST(VnaSweepTerminationTest, OnlyCancelledWorkerResultMeansActuallyAborted)
{
    using Error = vna::VnaErrorCode;
    using Reason = vna::SweepTerminationReason;
    // RequestAbort 是否曾返回 OK 不参与终因归一，避免竞态窗口虚报中止成功。
    EXPECT_EQ(vna::detail::normalizeSweepTermination(Error::Cancelled, Error::None, 4, 5), Reason::Aborted);
    EXPECT_EQ(vna::detail::normalizeSweepTermination(Error::None, Error::None, 4, 5), Reason::DataLoss);
    EXPECT_EQ(vna::detail::normalizeSweepTermination(Error::None, Error::None, 5, 5), Reason::Completed);
}

TEST(VnaSweepTerminationTest, DefersAcceptedAndStartHandoffAbortResults)
{
    using Error = vna::VnaErrorCode;
    // InvalidState 在 Worker 未进入 Execute 时不是终态；其他驱动错误可立即失败。
    EXPECT_TRUE(vna::detail::abortRequestAwaitsSweep(Error::None));
    EXPECT_TRUE(vna::detail::abortRequestAwaitsSweep(Error::InvalidState));
    EXPECT_FALSE(vna::detail::abortRequestAwaitsSweep(Error::Timeout));
    EXPECT_FALSE(vna::detail::abortRequestAwaitsSweep(Error::Cancelled));
}

}  // namespace
