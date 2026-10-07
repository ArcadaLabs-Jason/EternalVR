#include "features/flares/flare_book.hpp"

#include <algorithm>

namespace evr::flares {

bool plausible(const FlareRecord& record) {
    return record.model != 0 && record.vertices != 0 && record.quads > 0 && record.quads <= kMaxQuads &&
           record.vertices % 16 == 0;
}

std::uintptr_t vertexEnd(const FlareRecord& record) {
    return record.vertices + static_cast<std::uintptr_t>(record.quads) * kQuadBytes;
}

void FlareBook::startRender(const RenderKey& key) {
    if (hasKey_ && !taken_ && count_ > 0) {
        ++notTaken_;
    }
    key_ = key;
    hasKey_ = true;
    taken_ = false;
    count_ = 0;
}

AddResult FlareBook::add(const RenderKey& key, const FlareRecord& record) {
    if (hasKey_ && key.view == key_.view && static_cast<std::int32_t>(key.frame - key_.frame) < 0) {
        return AddResult::Late; // an older render of this view, after a newer one started
    }
    if (!hasKey_ || !(key == key_)) {
        startRender(key);
    }
    if (taken_) {
        return AddResult::Late;
    }
    const FlareRecord* const begin = records_;
    const FlareRecord* const end = begin + count_;
    if (std::find_if(begin, end, [&](const FlareRecord& r) { return r.model == record.model; }) != end) {
        return AddResult::Duplicate;
    }
    if (count_ == kCapacity) {
        return AddResult::Full;
    }
    records_[count_++] = record;
    return AddResult::Added;
}

TakeResult FlareBook::take(const RenderKey& key, std::span<FlareRecord> out) {
    TakeResult result;
    if (!hasKey_ || !(key == key_)) {
        result.otherRender = hasKey_ && !taken_ && count_ > 0;
        return result;
    }
    if (taken_) {
        return result;
    }
    taken_ = true;
    result.count = std::min(count_, out.size());
    std::copy_n(records_, result.count, out.begin());
    return result;
}

} // namespace evr::flares
