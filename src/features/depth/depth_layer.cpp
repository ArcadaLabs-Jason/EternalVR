#include "features/depth/depth_layer.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <string>

namespace evr::depth {

namespace {

std::string lower(std::string_view text) {
    std::string out;
    for (const char c : text) {
        if (c != ' ' && c != '\t') {
            out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
    }
    return out;
}

bool finite(double v) {
    return std::isfinite(v);
}

// a values from here up to kNearZeroA are taken as an infinite far plane: a float m10 of -1 holds a far plane
// past about 10^6 near planes only to its rounding.
constexpr double kInfiniteFarA = -1e-7;
constexpr double kNearZeroA = 1e-6;

} // namespace

std::optional<bool> parseSwitch(std::string_view text) {
    const std::string t = lower(text);
    if (t.empty() || t == "0" || t == "off" || t == "false" || t == "no") {
        return false;
    }
    if (t == "1" || t == "on" || t == "true" || t == "yes") {
        return true;
    }
    return std::nullopt;
}

std::optional<std::int64_t> chooseFormat(const std::vector<std::int64_t>& offered) {
    for (const std::int64_t f : {kDxgiD32Float, kDxgiD32FloatS8X24}) {
        if (std::find(offered.begin(), offered.end(), f) != offered.end()) {
            return f;
        }
    }
    return std::nullopt;
}

const char* formatName(std::int64_t dxgi) {
    switch (dxgi) {
    case kDxgiD32Float:
        return "D32_FLOAT";
    case kDxgiD32FloatS8X24:
        return "D32_FLOAT_S8X24_UINT";
    case kDxgiD24UnormS8:
        return "D24_UNORM_S8_UINT";
    case kDxgiD16Unorm:
        return "D16_UNORM";
    default:
        return "another format";
    }
}

std::optional<DepthModel> modelFromEngineRow(float m10, float m11) {
    if (!std::isfinite(m10) || !std::isfinite(m11)) {
        return std::nullopt;
    }
    DepthModel m;
    m.a = 1.0 + static_cast<double>(m10);
    m.b = -static_cast<double>(m11);
    if (!(m.b > 0.0) || !(m.a < 1.0) || m.a > kNearZeroA) {
        return std::nullopt;
    }
    if (m.a >= kInfiniteFarA) {
        m.a = 0.0; // an infinite far plane
    }
    return m;
}

double depthAt(const DepthModel& model, double units) {
    if (!(units > 0.0)) {
        return std::numeric_limits<double>::infinity();
    }
    return model.a + model.b / units;
}

double unitsAt(const DepthModel& model, double depth) {
    if (!(depth > model.a)) {
        return std::numeric_limits<double>::infinity();
    }
    return model.b / (depth - model.a);
}

std::optional<Planes> planesOf(const DepthModel& model, float unitsPerMetre) {
    if (!std::isfinite(unitsPerMetre) || !(unitsPerMetre > 0.0f) || !(model.b > 0.0) || !(model.a < 1.0) ||
        model.a > 0.0) {
        return std::nullopt;
    }
    const double scale = static_cast<double>(unitsPerMetre);
    Planes p;
    p.nearZ = model.a == 0.0 ? kMaxPlaneMetres
                             : static_cast<float>(std::min((-model.b / model.a) / scale,
                                                           static_cast<double>(kMaxPlaneMetres)));
    p.farZ = static_cast<float>((model.b / (1.0 - model.a)) / scale);
    if (!(p.farZ > 0.0f) || !(p.nearZ > p.farZ) || !std::isfinite(p.farZ)) {
        return std::nullopt;
    }
    return p;
}

float nearestDepth(const DepthModel& model, float unitsPerMetre) {
    const double units = static_cast<double>(kNearestMetres) * static_cast<double>(unitsPerMetre);
    const double d = depthAt(model, units);
    if (!finite(d) || d >= 1.0) {
        return 1.0f;
    }
    return static_cast<float>(std::max(d, 0.0));
}

bool isDepthFormat(std::int32_t vkFormat) {
    switch (vkFormat) {
    case vk::kFormatD16:
    case vk::kFormatX8D24:
    case vk::kFormatD32:
    case vk::kFormatD16S8:
    case vk::kFormatD24S8:
    case vk::kFormatD32S8:
        return true;
    default:
        return false;
    }
}

std::uint32_t barrierAspects(std::int32_t vkFormat) {
    const bool stencil =
        vkFormat == vk::kFormatD16S8 || vkFormat == vk::kFormatD24S8 || vkFormat == vk::kFormatD32S8;
    return vk::kAspectDepth | (stencil ? vk::kAspectStencil : 0u);
}

bool isCandidate(const ImageDesc& desc) {
    return desc.imageType == vk::kImageType2D && isDepthFormat(desc.format) && desc.depth == 1 &&
           desc.mipLevels == 1 && desc.arrayLayers == 1 && desc.samples == 1 &&
           (desc.usage & vk::kUsageDepthStencilAttachment) != 0 && (desc.usage & vk::kUsageSampled) != 0 &&
           (desc.usage & vk::kUsageTransientAttachment) == 0 && desc.width >= 256 && desc.height != 0;
}

const char* toString(SourceCheck check) {
    switch (check) {
    case SourceCheck::Ok:
        return "ok";
    case SourceCheck::NoImage:
        return "no depth image in the render system";
    case SourceCheck::UnknownImage:
        return "a VkImage the layer kept no record of";
    case SourceCheck::NotDepth:
        return "not a depth format";
    case SourceCheck::NotSampled:
        return "made without SAMPLED";
    case SourceCheck::NoLayout:
        return "layout not known yet";
    case SourceCheck::BadLayout:
        return "in a layout the copy cannot read";
    case SourceCheck::OtherQueueFamily:
        return "not shared with the copying queue family";
    case SourceCheck::SizeMismatch:
        return "not the eye image's size";
    }
    return "?";
}

SourceCheck checkSource(const SourceRecord* record,
                        std::optional<std::int32_t> layout,
                        std::optional<std::uint32_t> lastFamily,
                        std::uint32_t copyFamily,
                        bool graphicsQueue,
                        std::uint32_t eyeWidth,
                        std::uint32_t eyeHeight) {
    if (!record) {
        return SourceCheck::UnknownImage;
    }
    if (!isDepthFormat(record->format)) {
        return SourceCheck::NotDepth;
    }
    if ((record->usage & vk::kUsageSampled) == 0) {
        return SourceCheck::NotSampled;
    }
    if (record->width != eyeWidth || record->height != eyeHeight) {
        return SourceCheck::SizeMismatch;
    }
    if (!layout) {
        return SourceCheck::NoLayout;
    }
    if (!readLayoutFor(*layout, graphicsQueue)) {
        return SourceCheck::BadLayout;
    }
    if (record->sharingMode == vk::kSharingConcurrent) {
        if (std::find(record->queueFamilies.begin(), record->queueFamilies.end(), copyFamily) ==
            record->queueFamilies.end()) {
            return SourceCheck::OtherQueueFamily;
        }
    } else if (!lastFamily || *lastFamily != copyFamily) {
        return SourceCheck::OtherQueueFamily;
    }
    return SourceCheck::Ok;
}

std::optional<ReadLayout> readLayoutFor(std::int32_t layout, bool graphicsQueue) {
    switch (layout) {
    case vk::kLayoutDepthStencilReadOnly:
    case vk::kLayoutShaderReadOnly:
        return ReadLayout{layout, false};
    case vk::kLayoutDepthStencilAttachment:
        // A depth layout change on a queue without graphics is not taken (the game presents from its compute
        // queue in a level).
        return graphicsQueue ? std::optional<ReadLayout>(ReadLayout{vk::kLayoutDepthStencilReadOnly, true})
                             : std::nullopt;
    default:
        return std::nullopt;
    }
}

const char* toString(Off off) {
    switch (off) {
    case Off::None:
        return "on";
    case Off::Switch:
        return "off (ETERNALVR_DEPTH_LAYER is not on)";
    case Off::NoExtension:
        return "off (the runtime does not list XR_KHR_composition_layer_depth)";
    case Off::NotRouteS:
        return "off (only with stereo pairs: Route S is not running)";
    case Off::AlternateEyes:
        return "off (not with alternate eyes, on or auto)";
    case Off::ParallelEyes:
        return "off (not with Parallel Eye Rendering)";
    case Off::NoTracking:
        return "off (needs the UI layer, whose image hooks follow the game's depth image)";
    case Off::NoFormat:
        return "off (the runtime offers no D32_FLOAT or D32_FLOAT_S8X24_UINT swapchain)";
    }
    return "off";
}

Off whyOff(const StartState& s) {
    if (!s.switchOn) {
        return Off::Switch;
    }
    if (!s.extensionEnabled) {
        return Off::NoExtension;
    }
    if (s.parallelEyes) {
        return Off::ParallelEyes;
    }
    if (s.alternateEyes) {
        return Off::AlternateEyes;
    }
    if (!s.routeS) {
        return Off::NotRouteS;
    }
    if (!s.tracking) {
        return Off::NoTracking;
    }
    if (!s.formatOffered) {
        return Off::NoFormat;
    }
    return Off::None;
}

bool refusesDepth(int xrResult) {
    constexpr int kValidationFailure = -1;
    constexpr int kLayerInvalid = -23;
    constexpr int kSwapchainRectInvalid = -25;
    constexpr int kSwapchainFormatUnsupported = -26;
    return xrResult == kValidationFailure || xrResult == kLayerInvalid || xrResult == kSwapchainRectInvalid ||
           xrResult == kSwapchainFormatUnsupported;
}

void SwapImageState::onAcquired(std::int64_t index) {
    acquired_ = index;
    waited_ = false;
    pending_ = false;
}

void SwapImageState::onCopyRecorded(std::optional<Planes> planes) {
    pending_ = acquired_ >= 0 && waited_;
    pendingPlanes_ = planes;
}

bool SwapImageState::onNewImage() {
    if (!pending_) {
        shown_ = false; // a new image without depth: never another image's
        return false;
    }
    pending_ = false;
    acquired_ = -1;
    waited_ = false;
    shown_ = pendingPlanes_.has_value();
    if (pendingPlanes_) {
        planes_ = *pendingPlanes_;
    }
    return true;
}

void SwapImageState::onDetached() {
    *this = SwapImageState{};
}

WaitTimeouts::Step WaitTimeouts::onTimeout() {
    ++inRow;
    ++total;
    Step step;
    step.log = total == 1 || total % kWaitLogEvery == 0;
    step.stop = inRow >= kMaxWaitTimeouts;
    return step;
}

EyeStats statsOf(const float* texels,
                 std::size_t rowTexels,
                 std::size_t x0,
                 std::size_t width,
                 std::size_t height,
                 std::size_t step,
                 const DepthModel& model,
                 float unitsPerMetre,
                 float nearestValue) {
    EyeStats s;
    if (!texels || width == 0 || height == 0 || step == 0 || !(unitsPerMetre > 0.0f)) {
        return s;
    }
    const double scale = static_cast<double>(unitsPerMetre);
    std::vector<double> metres;
    for (std::size_t y = 0; y < height; y += step) {
        for (std::size_t x = 0; x < width; x += step) {
            const float d = texels[y * rowTexels + x0 + x];
            ++s.samples;
            if (!std::isfinite(d) || d < 0.0f || d > 1.0f) {
                ++s.invalid;
                continue;
            }
            if (d == 0.0f) {
                ++s.sky;
                continue;
            }
            if (d >= nearestValue) {
                ++s.clamped;
            }
            const double m = unitsAt(model, d) / scale;
            if (finite(m)) {
                metres.push_back(m);
            }
        }
    }
    if (!metres.empty()) {
        const auto [lo, hi] = std::minmax_element(metres.begin(), metres.end());
        s.minMetres = *lo;
        s.maxMetres = *hi;
        auto mid = metres.begin() + static_cast<std::ptrdiff_t>(metres.size() / 2);
        std::nth_element(metres.begin(), mid, metres.end());
        s.medianMetres = *mid;
    }
    const float c = texels[(height / 2) * rowTexels + x0 + width / 2];
    s.centreMetres = std::isfinite(c) ? unitsAt(model, c) / scale : std::numeric_limits<double>::quiet_NaN();
    return s;
}

double sameShare(
    const float* texels, std::size_t rowTexels, std::size_t eyeWidth, std::size_t height, std::size_t step) {
    if (!texels || eyeWidth == 0 || height == 0 || step == 0) {
        return 0.0;
    }
    std::uint64_t counted = 0;
    std::uint64_t same = 0;
    for (std::size_t y = 0; y < height; y += step) {
        for (std::size_t x = 0; x < eyeWidth; x += step) {
            const float l = texels[y * rowTexels + x];
            const float r = texels[y * rowTexels + eyeWidth + x];
            if (l == 0.0f && r == 0.0f) {
                continue; // the sky is the same in both eyes
            }
            ++counted;
            same += l == r ? 1 : 0;
        }
    }
    return counted == 0 ? 0.0 : static_cast<double>(same) / static_cast<double>(counted);
}

} // namespace evr::depth
