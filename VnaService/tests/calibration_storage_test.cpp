/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "vna_core/frame_layout.h"
#include "vna_service/calibration_workspace.h"

namespace {
using VnaService::CalibrationChange;
using VnaService::CalibrationChangeKind;
using VnaService::CalibrationSet;
using VnaService::CalibrationSetPtr;
using VnaService::CalibrationWorkspace;
const std::string IDENTITY = "VnaPro,NativeSimulator,4PORT,1.0";

void Require(bool value, const std::string &message)
{
    if (!value) {
        throw std::runtime_error(message);
    }
}

std::shared_ptr<CalibrationSet> Set(unsigned index, unsigned points = 800U)
{
    auto data = std::make_shared<VnaCalibration::CalibrationData>();
    data->method = VnaCalibration::CalibrationMethod::FOUR_PORT;
    data->port1 = 1U;
    data->port2 = 4U;
    VnaCalibration::FourPortTerms terms;
    for (unsigned cell = 0U; cell < 16U; ++cell) {
        terms[cell].offset = cell / 4U == cell % 4U ? VnaCalibration::Complex{0.01, 0.002} : VnaCalibration::Complex{};
        terms[cell].match = {0.002 * (cell + 1U), 0.001};
        terms[cell].tracking = {1.1 + 0.001 * index + 0.01 * cell, 0.01};
    }
    data->fourPortTerms.assign(points, terms);
    for (unsigned point = 0U; point < points; ++point) {
        data->frequencyHz.push_back(10000000.0 + point * 1000.0);
    }
    auto set = std::make_shared<CalibrationSet>();
    set->id = index + 1U;
    set->name = "Stored_" + std::to_string(index);
    set->deviceIdentity = IDENTITY;
    set->ifBandwidthHz = 1000.0;
    set->powerDbm.assign(points, 0.0);
    set->data = std::move(data);
    return set;
}

std::size_t ExpectedBytes(const CalibrationSet &set)
{
    const auto &data = *set.data;
    return sizeof(CalibrationSet) + sizeof(VnaCalibration::CalibrationData) + sizeof(CalibrationSetPtr) +
           set.name.size() + set.deviceIdentity.size() + 2U +
           sizeof(double) * (set.powerDbm.size() + data.frequencyHz.size()) +
           sizeof(VnaCalibration::OnePortTerms) * data.onePortTerms.size() +
           sizeof(VnaCalibration::TwoPortTerms) * data.twoPortTerms.size() +
           sizeof(VnaCalibration::FourPortTerms) * data.fourPortTerms.size();
}

bool Import(CalibrationWorkspace &workspace, const CalibrationSetPtr &set, std::string &error)
{
    CalibrationChange change;
    change.kind = CalibrationChangeKind::IMPORT_SET;
    change.importedSet = set;
    return workspace.ApplySaved(change, VnaService::InstrumentProfile{}, error);
}

VnaCore::MeasurementFrame StandardFrame(const VnaCore::SweepPlan &plan, const VnaCalibration::TwoPortNetwork &network)
{
    VnaCore::MeasurementFrame frame;
    frame.plan = plan;
    for (const auto sourcePort : plan.sourcePorts) {
        VnaCore::PortFrame source;
        source.sourcePort = sourcePort;
        source.iq.assign(plan.frequencyHz.size() * VnaCore::IQ_VALUES_PER_POINT, 0.0);
        for (std::size_t point = 0U; point < plan.frequencyHz.size(); ++point) {
            const auto offset = point * VnaCore::IQ_VALUES_PER_POINT;
            const auto first = sourcePort == 1U ? network.s11 : network.s12;
            const auto second = sourcePort == 1U ? network.s21 : network.s22;
            source.iq[offset] = 1.0;
            source.iq[offset + 2U] = first.real();
            source.iq[offset + 3U] = first.imag();
            source.iq[offset + 4U] = 1.0;
            source.iq[offset + 6U] = second.real();
            source.iq[offset + 7U] = second.imag();
        }
        frame.ports.push_back(std::move(source));
    }
    return frame;
}

void Real204SetsAndStableRestore()
{
    CalibrationWorkspace workspace;
    std::string error;
    std::size_t expectedBytes = 0U;
    for (unsigned index = 0U; index < 204U; ++index) {
        const auto input = Set(index);
        expectedBytes += ExpectedBytes(*input);
        Require(Import(workspace, input, error), error);
        const auto stored = workspace.Find(index + 1U);
        Require(stored && stored->name == input->name && stored->data != input->data &&
                    stored->data->fourPortTerms.size() == 800U,
            "each of the 204 named sets must own actual independent 800-point coefficients");
    }
    Require(workspace.SavedSets().size() == 204U && workspace.StorageBytes() == expectedBytes &&
                expectedBytes <= VnaService::MAX_CALIBRATION_STORAGE_BYTES && expectedBytes > 120U * 1024U * 1024U,
        "204 real four-port sets must fit the declared channel budget with exact logical accounting");
    std::vector<CalibrationSetPtr> saved = workspace.SavedSets();
    std::reverse(saved.begin(), saved.end());
    CalibrationWorkspace restored;
    Require(restored.RestoreSaved(saved, 137U, VnaService::InstrumentProfile{}, error), error);
    Require(restored.StorageBytes() == expectedBytes && restored.SelectedSetId() == 137U &&
                !restored.Snapshot(IDENTITY, {}).enabled && restored.SavedSets().front()->id == 204U,
        "restore must preserve stable IDs/order/selection and start with correction disabled");
    for (const auto &set : saved) {
        const auto copy = restored.Find(set->id);
        Require(copy && copy->name == set->name && copy->data != set->data &&
                    copy->data->fourPortTerms[799U][15U].tracking == set->data->fourPortTerms[799U][15U].tracking,
            "restored coefficients and names must retain their original IDs without mutable aliases");
    }
    Require(Import(restored, Set(999U, 2U), error) && restored.Find(205U) && !restored.Find(1000U),
        "the next imported ID must follow the restored maximum rather than the input document ID");
    Require(!restored.RestoreSaved({}, 0U, VnaService::InstrumentProfile{}, error), "restore must be startup-only");
}

void ByteBudgetRejectsRestoreBeforeCopy()
{
    const auto large = Set(0U, 10001U);
    std::vector<CalibrationSetPtr> excessive;
    std::size_t bytes = 0U;
    for (unsigned index = 0U; bytes <= VnaService::MAX_CALIBRATION_STORAGE_BYTES; ++index) {
        auto item = std::make_shared<CalibrationSet>(*large);
        item->id = index + 1U;
        item->name = "Shared input " + std::to_string(index);
        bytes += ExpectedBytes(*item);
        excessive.push_back(std::move(item));
    }
    Require(excessive.size() < VnaService::MAX_CALIBRATION_SETS,
        "large-array accounting must exceed bytes before the item-count limit");
    CalibrationWorkspace workspace;
    std::string error;
    Require(!workspace.RestoreSaved(excessive, 1U, VnaService::InstrumentProfile{}, error) &&
                error.find("byte budget") != std::string::npos && workspace.StorageBytes() == 0U &&
                workspace.SavedSets().empty() && workspace.Revision() == 1U,
        "over-budget restore must fail in preflight before deep-copying the shared input payloads");
    const auto tdr = Set(0U, 4000U);
    const auto dense = Set(0U, 6700U);
    Require(256U * ExpectedBytes(*tdr) <= VnaService::MAX_CALIBRATION_STORAGE_BYTES &&
                204U * ExpectedBytes(*dense) <= VnaService::MAX_CALIBRATION_STORAGE_BYTES &&
                205U * ExpectedBytes(*dense) > VnaService::MAX_CALIBRATION_STORAGE_BYTES,
        "4000-point capacity must fit all 204 paths through the 256-set limit; 6700-point capacity stops at 204");
}

void CountLimitAndRestoreValidation()
{
    CalibrationWorkspace bounded;
    std::string error;
    for (unsigned index = 0U; index < VnaService::MAX_CALIBRATION_SETS; ++index) {
        Require(Import(bounded, Set(index, 2U), error), error);
    }
    const auto revision = bounded.Revision();
    Require(
        !Import(bounded, Set(300U, 2U), error) && bounded.Revision() == revision && bounded.SavedSets().size() == 256U,
        "the independent 256-set limit remains enforced for small payloads");
    CalibrationChange remove;
    remove.kind = CalibrationChangeKind::DELETE_SET;
    remove.setId = 1U;
    const auto firstBytes = ExpectedBytes(*bounded.Find(1U));
    const auto beforeDelete = bounded.StorageBytes();
    Require(bounded.ApplySaved(remove, VnaService::InstrumentProfile{}, error) &&
                bounded.StorageBytes() == beforeDelete - firstBytes && Import(bounded, Set(300U, 2U), error) &&
                bounded.Find(257U),
        "delete returns budget and a rejected import does not consume the next stable ID");
    const auto rejected = [](const std::vector<CalibrationSetPtr> &sets, std::uint64_t selected,
                              VnaService::InstrumentProfile profile = {}) {
        CalibrationWorkspace candidate;
        std::string message;
        Require(!candidate.RestoreSaved(sets, selected, profile, message) && !message.empty() &&
                    candidate.SavedSets().empty() && candidate.SelectedSetId() == 0U &&
                    candidate.StorageBytes() == 0U && candidate.Revision() == 1U,
            "invalid restore must leave the entire fresh workspace unchanged");
    };
    auto first = Set(0U, 2U);
    auto second = Set(1U, 2U);
    second->id = first->id;
    rejected({first, second}, 0U);
    first->id = 0U;
    rejected({first}, 0U);
    first->id = (std::numeric_limits<std::uint64_t>::max)();
    rejected({first}, 0U);
    first->id = 7U;
    rejected({first}, 8U);
    VnaService::InstrumentProfile twoPort;
    twoPort.portCount = 2U;
    rejected({first}, 7U, twoPort);
    auto malformed = std::make_shared<VnaCalibration::CalibrationData>(*first->data);
    malformed->fourPortTerms.pop_back();
    auto invalid = std::make_shared<CalibrationSet>(*first);
    invalid->data = malformed;
    rejected({invalid}, 0U);
    malformed = std::make_shared<VnaCalibration::CalibrationData>(*first->data);
    malformed->fourPortTerms[0U][0U].match = {std::nan(""), 0.0};
    invalid->data = malformed;
    rejected({invalid}, 0U);
    std::vector<CalibrationSetPtr> excessive;
    for (unsigned index = 0U; index <= VnaService::MAX_CALIBRATION_SETS; ++index) {
        excessive.push_back(Set(index, 2U));
    }
    rejected(excessive, 0U);
}

void DuplicateNamesAliasesAndIdExhaustion()
{
    auto first = Set(0U, 2U);
    auto second = Set(1U, 2U);
    first->id = 10U;
    second->id = 20U;
    first->name = second->name = "Duplicate names remain explicit";
    auto mutableData = std::make_shared<VnaCalibration::CalibrationData>(*first->data);
    first->data = mutableData;
    CalibrationWorkspace workspace;
    std::string error;
    Require(workspace.RestoreSaved({first, second}, 20U, VnaService::InstrumentProfile{}, error), error);
    const auto original = workspace.Find(10U)->data->fourPortTerms[0U][0U].tracking;
    mutableData->fourPortTerms[0U][0U].tracking = {};
    first->name = "Mutated external alias";
    Require(workspace.Find(10U)->name == second->name &&
                workspace.Find(10U)->data->fourPortTerms[0U][0U].tracking == original,
        "restored set metadata and coefficients must not retain writable caller aliases");
    CalibrationChange activate;
    activate.kind = CalibrationChangeKind::ACTIVATE;
    activate.name = second->name;
    Require(!workspace.ApplySaved(activate, VnaService::InstrumentProfile{}, error) &&
                workspace.SelectedSetId() == 20U && error.find("ambiguous") != std::string::npos,
        "duplicate stored names must keep the existing ambiguous-activation rejection");
    auto finalId = Set(2U, 2U);
    finalId->id = (std::numeric_limits<std::uint64_t>::max)() - 1U;
    CalibrationWorkspace exhausted;
    Require(exhausted.RestoreSaved({finalId}, finalId->id, VnaService::InstrumentProfile{}, error) &&
                !Import(exhausted, second, error) && exhausted.SavedSets().size() == 1U,
        "a restored maximum ID must not wrap when the next ID is exhausted");
}

void ActivationUsesFrozenCoefficientsAndRetainsProfileChecks()
{
    auto input = Set(0U, 3U);
    auto callerData = std::make_shared<VnaCalibration::CalibrationData>(*input->data);
    input->data = callerData;
    CalibrationWorkspace workspace;
    std::string error;
    Require(Import(workspace, input, error), error);
    const auto stored = workspace.Find(1U);
    const auto originalTracking = stored->data->fourPortTerms[0U][0U].tracking;
    const auto originalBytes = workspace.StorageBytes();
    callerData->fourPortTerms[0U][0U].tracking = {};
    callerData->frequencyHz[0U] = -1.0;
    input->powerDbm[0U] = std::numeric_limits<double>::quiet_NaN();
    input->name = "Changed caller metadata";
    CalibrationChange activate;
    activate.kind = CalibrationChangeKind::ACTIVATE;
    activate.name = stored->name;
    for (unsigned attempt = 0U; attempt < 3U; ++attempt) {
        Require(workspace.ApplySaved(activate, VnaService::InstrumentProfile{}, error) &&
                    workspace.SelectedSetId() == 1U && workspace.Find(1U) == stored &&
                    stored->data->fourPortTerms[0U][0U].tracking == originalTracking &&
                    workspace.StorageBytes() == originalBytes,
            "repeated activation uses the validated frozen data and never caller aliases or replacement coefficients");
    }
    const auto revision = workspace.Revision();
    auto narrow = VnaService::InstrumentProfile{};
    narrow.portCount = 2U;
    Require(!workspace.ApplySaved(activate, narrow, error) && workspace.Revision() == revision &&
                workspace.SelectedSetId() == 1U,
        "activation must still reject a four-port set for a narrower port profile without changing selection");
    narrow = {};
    narrow.minFrequencyHz = stored->data->frequencyHz.front() + 1.0;
    Require(!workspace.ApplySaved(activate, narrow, error) && workspace.Revision() == revision,
        "activation must still check the current profile minimum frequency");
    narrow = {};
    narrow.maxFrequencyHz = stored->data->frequencyHz.back() - 1.0;
    Require(!workspace.ApplySaved(activate, narrow, error) && workspace.Revision() == revision,
        "activation must still check the current profile maximum frequency");
    CalibrationWorkspace copied = workspace;
    Require(copied.ApplySaved(activate, VnaService::InstrumentProfile{}, error) && copied.Find(1U) == stored,
        "transaction candidates may share immutable validated coefficient ownership");
    auto invalid = Set(1U, 3U);
    auto invalidData = std::make_shared<VnaCalibration::CalibrationData>(*invalid->data);
    invalidData->fourPortTerms[1U][7U].tracking = {};
    invalid->data = invalidData;
    Require(
        !Import(workspace, invalid, error) && workspace.Revision() == revision && workspace.SavedSets().size() == 1U,
        "full coefficient validation remains mandatory when importing into the trusted pool");
    CalibrationWorkspace restored;
    Require(!restored.RestoreSaved({invalid}, invalid->id, VnaService::InstrumentProfile{}, error) &&
                restored.SavedSets().empty() && restored.Revision() == 1U,
        "restoring a persisted zero tracking coefficient must still reject the entire pool atomically");
}

void DirectWorkspaceCompletionStillRequiresValidActivationMetadata()
{
    CalibrationWorkspace workspace;
    VnaCore::SweepPlan plan;
    plan.frequencyHz = {10000000.0, 10001000.0};
    plan.frequencyRaw = {10000000U, 10001000U};
    plan.powerDbm = {0.0, 0.0};
    plan.ifBandwidthRaw = 123U;
    plan.receiverMask = 1U;
    plan.sourcePorts = {1U};
    CalibrationChange begin;
    std::string error;
    Require(workspace.Begin(begin, IDENTITY, plan, VnaService::InstrumentProfile{}, error), error);
    const std::vector<std::pair<std::string, double>> standards{{"open-1", 1.0}, {"short-1", -1.0}, {"load-1", 0.0}};
    for (const auto &standard : standards) {
        VnaCalibration::TwoPortNetwork network;
        network.s11 = standard.second;
        Require(workspace.Capture(standard.first, StandardFrame(plan, network), error), error);
    }
    Require(workspace.Complete("Direct workspace", error), error);
    CalibrationChange activate;
    activate.kind = CalibrationChangeKind::ACTIVATE;
    activate.setId = 1U;
    const auto revision = workspace.Revision();
    Require(!workspace.ApplySaved(activate, VnaService::InstrumentProfile{}, error) &&
                workspace.Revision() == revision && error.find("IF bandwidth") != std::string::npos,
        "internally solved coefficients do not bypass the original unsupported-metadata activation rejection");
}

void ProveSolverCorrectionOverflow(const std::array<VnaCalibration::Complex, 3U> &defined,
    const std::array<VnaCalibration::Complex, 3U> &measured, const VnaCalibration::TwoPortNetwork &thru)
{
    using VnaCalibration::CalibrationError;
    using VnaCalibration::Complex;
    VnaCalibration::OnePortTerms extreme;
    Complex corrected;
    Require(VnaCalibration::SolveThreeStandards(measured, defined, extreme) == CalibrationError::NONE &&
                std::isfinite(extreme.reflectionTracking.real()) && std::isfinite(extreme.reflectionTracking.imag()) &&
                !std::isfinite(std::abs(extreme.reflectionTracking)) &&
                VnaCalibration::CorrectOnePort(extreme.directivity, extreme, corrected) != CalibrationError::NONE,
        "one-port counterexample solves finite components but its correction scale overflows");
    VnaCalibration::OnePortTerms ideal;
    ideal.reflectionTracking = 1.0;
    VnaCalibration::TwoPortNetwork definition;
    definition.s21 = definition.s12 = 1.0;
    VnaCalibration::TwoPortTerms coupled;
    VnaCalibration::TwoPortNetwork correctedPair;
    Require(VnaCalibration::SolveKnownThru(thru, definition, ideal, ideal, {}, {}, coupled) == CalibrationError::NONE &&
                VnaCalibration::CorrectTwoPort({}, coupled, correctedPair) != CalibrationError::NONE,
        "two-port counterexample solves but its coupled load-match product overflows during correction");
}

void RejectUnusableCompletionAtomically(bool twoPort, const std::array<VnaCalibration::Complex, 3U> &defined,
    const std::array<VnaCalibration::Complex, 3U> &measured, const VnaCalibration::TwoPortNetwork &thru)
{
    CalibrationWorkspace workspace;
    std::string error;
    Require(Import(workspace, Set(0U, 2U), error), error);
    CalibrationChange activate;
    activate.kind = CalibrationChangeKind::ACTIVATE;
    activate.setId = 1U;
    Require(workspace.ApplySaved(activate, VnaService::InstrumentProfile{}, error), error);
    const auto retained = workspace.Find(1U);
    VnaCore::SweepPlan plan;
    plan.frequencyHz = {10000000.0, 10001000.0};
    plan.frequencyRaw = {10000000U, 10001000U};
    plan.powerDbm = {0.0, 0.0};
    plan.ifBandwidthRaw = 1000U;
    plan.receiverMask = twoPort ? 3U : 1U;
    plan.sourcePorts = twoPort ? std::vector<std::uint32_t>{1U, 2U} : std::vector<std::uint32_t>{1U};
    CalibrationChange begin;
    begin.method = twoPort ? VnaCalibration::CalibrationMethod::TWO_PORT : VnaCalibration::CalibrationMethod::ONE_PORT;
    begin.port2 = twoPort ? 2U : 0U;
    Require(workspace.Begin(begin, IDENTITY, plan, VnaService::InstrumentProfile{}, error), error);
    const auto session = workspace.Snapshot(IDENTITY, plan).sessionId;
    const std::array<const char *, 3U> kinds{{"open-", "short-", "load-"}};
    for (unsigned port = 1U; port <= (twoPort ? 2U : 1U); ++port) {
        for (unsigned standard = 0U; standard < 3U; ++standard) {
            VnaCalibration::TwoPortNetwork network;
            const auto reflection = twoPort ? defined[standard] : measured[standard];
            (port == 1U ? network.s11 : network.s22) = reflection;
            Require(workspace.Capture(
                        std::string(kinds[standard]) + std::to_string(port), StandardFrame(plan, network), error),
                error);
        }
    }
    if (twoPort) {
        Require(workspace.Capture("thru-1-2", StandardFrame(plan, thru), error), error);
    }
    const auto before = workspace.Snapshot(IDENTITY, plan);
    const auto bytes = workspace.StorageBytes();
    Require(!workspace.Complete("Must not publish", error) && error.find("coefficient") != std::string::npos &&
                workspace.Revision() == before.revision && workspace.Collecting() && workspace.SelectedSetId() == 1U &&
                workspace.Find(1U) == retained && workspace.SavedSets().size() == 1U &&
                workspace.StorageBytes() == bytes,
        "completion validates actual correction once before publishing, retaining the old pool and recapturable "
        "session");
    Require(workspace.Cancel(session, error) && Import(workspace, Set(1U, 2U), error) && workspace.Find(2U),
        "failed coefficient validation does not consume a stored ID");
}

void SolvedButUnusableCoefficientsCannotEnterThePool()
{
    using VnaCalibration::Complex;
    const std::array<Complex, 3U> defined{{1.0, -1.0, 0.0}};
    const std::array<Complex, 3U> measured{{0.1, -0.1, Complex{4e153, 2e153}}};
    VnaCalibration::TwoPortNetwork thru;
    thru.s11 = thru.s22 = 1e200;
    thru.s21 = thru.s12 = 1.0;
    ProveSolverCorrectionOverflow(defined, measured, thru);
    RejectUnusableCompletionAtomically(false, defined, measured, thru);
    RejectUnusableCompletionAtomically(true, defined, measured, thru);
}

void Run(const char *name, void (*test)(), unsigned &failed)
{
    try {
        test();
        std::cout << "PASS " << name << '\n';
    } catch (const std::exception &error) {
        ++failed;
        std::cerr << "FAIL " << name << ": " << error.what() << '\n';
    }
}
}  // 匿名命名空间

int main()
{
    unsigned failed = 0U;
    Run("204 real named 800-point sets and stable restore", Real204SetsAndStableRestore, failed);
    Run("byte-budget restore preflight and mixed production capacity", ByteBudgetRejectsRestoreBeforeCopy, failed);
    Run("count limit and transactional restore validation", CountLimitAndRestoreValidation, failed);
    Run("duplicate names, aliases and ID exhaustion", DuplicateNamesAliasesAndIdExhaustion, failed);
    Run("activation uses immutable validated data and rechecks current profile",
        ActivationUsesFrozenCoefficientsAndRetainsProfileChecks, failed);
    Run("direct workspace completion retains activation metadata rejection",
        DirectWorkspaceCompletionStillRequiresValidActivationMetadata, failed);
    Run("solved but unusable coefficients cannot be published", SolvedButUnusableCoefficientsCannotEnterThePool,
        failed);
    return failed == 0U ? 0 : 1;
}
