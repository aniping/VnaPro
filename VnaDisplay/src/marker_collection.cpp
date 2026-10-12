/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "vna_display/marker_collection.h"

#include <algorithm>
#include <utility>

#include "marker_resolver.h"

namespace VnaDisplay {
namespace {

bool EqualSettings(const MarkerSettings &left, const MarkerSettings &right) noexcept
{
    return left.enabled == right.enabled && left.hasFrequency == right.hasFrequency &&
           (!left.hasFrequency || left.frequencyHz == right.frequencyHz) &&
           left.hasSampleIndex == right.hasSampleIndex &&
           (!left.hasSampleIndex ||
               (left.sampleIndex == right.sampleIndex && left.acquisitionRevision == right.acquisitionRevision)) &&
           left.interpolate == right.interpolate;
}

bool HasUpdate(const MarkerChange &change) noexcept
{
    return change.hasEnabled || change.hasFrequency || change.hasSampleIndex || change.hasStep ||
           change.hasInterpolation;
}

}  // 匿名命名空间

unsigned MarkerCollection::Selected() const noexcept
{
    return selected_;
}

MarkerSettings &MarkerCollection::Active() noexcept
{
    return items_[selected_ - 1U];
}

const MarkerSettings &MarkerCollection::Active() const noexcept
{
    return items_[selected_ - 1U];
}

const MarkerSettings *MarkerCollection::Find(unsigned id) const noexcept
{
    return id >= 1U && id <= MAX_MARKERS ? &items_[id - 1U] : nullptr;
}

const std::array<MarkerSettings, MAX_MARKERS> &MarkerCollection::Items() const noexcept
{
    return items_;
}

bool MarkerCollection::SameSettings(const MarkerCollection &other) const noexcept
{
    return selected_ == other.selected_ &&
           std::equal(items_.begin(), items_.end(), other.items_.begin(), EqualSettings);
}

bool MarkerCollection::Apply(const MarkerChange &change, const FrameContext &context, std::string &error)
{
    if (change.action != MarkerAction::UPDATE && change.action != MarkerAction::ADD &&
        change.action != MarkerAction::SELECT && change.action != MarkerAction::ALL_OFF) {
        error = "Unsupported marker action";
        return false;
    }
    if (change.id > MAX_MARKERS) {
        error = "Marker number must be between 1 and 10";
        return false;
    }
    if (change.checkAcquisitionRevision && change.expectedAcquisitionRevision != context.acquisitionRevision) {
        error = "Acquisition revision changed";
        return false;
    }
    if (change.action != MarkerAction::UPDATE && HasUpdate(change)) {
        error = "Marker selection, addition and All Off cannot include position or state updates";
        return false;
    }
    auto candidate = *this;
    if (change.action == MarkerAction::ALL_OFF) {
        for (auto &marker : candidate.items_) {
            marker.enabled = false;
        }
        *this = std::move(candidate);
        return true;
    }
    auto update = change;
    unsigned id = change.id == 0U ? candidate.selected_ : change.id;
    if (change.action == MarkerAction::ADD) {
        if (change.id != 0U) {
            error = "Add Marker chooses the next unused marker number";
            return false;
        }
        const auto free =
            std::find_if(candidate.items_.begin(), candidate.items_.end(), [](const MarkerSettings &marker) {
                return !marker.enabled;
            });
        if (free == candidate.items_.end()) {
            error = "All ten markers are enabled";
            return false;
        }
        id = static_cast<unsigned>(free - candidate.items_.begin()) + 1U;
    }
    if (change.action == MarkerAction::ADD || change.action == MarkerAction::SELECT) {
        update.hasEnabled = true;
        update.enabled = true;
    }
    if (!Detail::MarkerResolver::Apply(candidate.items_[id - 1U], update, context, error)) {
        return false;
    }
    candidate.selected_ = id;
    *this = std::move(candidate);
    return true;
}

void MarkerCollection::ReconcileAxis(const std::vector<double> &axis, bool preserveSampleIndexes) noexcept
{
    for (auto &marker : items_) {
        Detail::MarkerResolver::Reconcile(marker, axis, preserveSampleIndexes);
    }
}

void MarkerCollection::BindAcquisitionRevision(std::uint64_t acquisitionRevision) noexcept
{
    for (auto &marker : items_) {
        if (marker.hasSampleIndex) {
            marker.acquisitionRevision = acquisitionRevision;
        }
    }
}

}  // 命名空间 VnaDisplay
