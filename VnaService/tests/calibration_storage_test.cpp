/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

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
    const auto si = Set(0U, 880U);
    const auto tdr = Set(0U, 4000U);
    const auto mixedBytes = 108U * ExpectedBytes(*si) + 96U * ExpectedBytes(*tdr);
    Require(mixedBytes < VnaService::MAX_CALIBRATION_STORAGE_BYTES &&
                204U * ExpectedBytes(*tdr) > VnaService::MAX_CALIBRATION_STORAGE_BYTES,
        "the SI/TDR production mix fits; 204 full 4000-point arrays must not be advertised as fitting");
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
    return failed == 0U ? 0 : 1;
}
