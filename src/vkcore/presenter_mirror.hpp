#pragma once

// The desktop mirror under Route S (ETERNALVR_MIRROR, docs/VR_STEREO.md, Desktop window): both presents of
// a stereo tick reach the game's window, which would show the eyes in turn. In the present hook's ring copy
// the shown eye's image is kept in a private image and copied over the other eye's image before its present,
// so the window shows one eye steadily (or black with `off`). Two image copies per tick, on the GPU only.
//
// With ETERNALVR_MIRROR_CROP (virtual_client::mirrorCropAspect) an image that reaches the window shows only
// the eye's centred band: the band is stretched over the whole image, and the driver stretches the image
// into the window, which has the band's shape. One blit per present the window shows (plus a copy into the
// private image when the step keeps nothing).
//
// While a menu is up over a head-tracked frame the eye image holds the world without the GUI (black behind
// the pause menu): the GUI is on the headset's menu panel. The window then shows the panel's image, the
// game's GUI target: a present that carries it keeps it in the private image (keepPanel, one copy or blit,
// only for a present whose image the window needs), and a Load puts it over the image the window gets,
// cut to the band as an eye would be (stereo_seq::panelMirror decides which present does what).
//
// A blit needs a graphics queue. A present from a family without one (the game has presented from its
// compute queue) gets no crop, and no panel for a swapchain that is not RGBA8: the window shows the eye.

#include "stereo_seq/desktop_window.hpp"
#include "vkcore/dispatch.hpp"

#include <cstdint>

namespace evr::vkcore {

class DesktopMirror {
public:
    // Records `step` into `cb` (of queue family `family`) for the swapchain image `source`
    // (TRANSFER_SRC_OPTIMAL on entry), then the crop when the image reaches the window (`toWindow`). Returns
    // the layout `source` is left in: TRANSFER_SRC_OPTIMAL, or TRANSFER_DST_OPTIMAL after a load, a clear or
    // a crop.
    VkImageLayout record(DeviceData& dev,
                         VkCommandBuffer cb,
                         std::uint32_t family,
                         VkImage source,
                         VkFormat format,
                         VkExtent2D extent,
                         stereo_seq::MirrorStep step,
                         bool toWindow);
    // What a present does for the window: `eyeStep` in gameplay; while `menu` is up over a head-tracked frame
    // (the GUI not in the eye image), the panel's steps (stereo_seq::panelMirror). Each change of menu is
    // logged, and a panel kept for an earlier menu (or an eye kept before one) is never loaded after it.
    stereo_seq::PanelMirror plan(bool menu,
                                 stereo_seq::Mirror mirror,
                                 bool gated,
                                 stereo_seq::PresentKind kind,
                                 bool toWindow,
                                 stereo_seq::MirrorStep eyeStep);
    // Keeps the game's GUI target `gui` (R8G8B8A8, TRANSFER_SRC_OPTIMAL, `guiExtent`) in the private image,
    // for a swapchain of `format` and `extent`, so that a Load shows it. False (nothing kept) when the
    // sizes differ, the format cannot take it, or it needs a blit and `cb`'s queue family has no graphics.
    bool keepPanel(DeviceData& dev,
                   VkCommandBuffer cb,
                   std::uint32_t family,
                   VkImage gui,
                   VkExtent2D guiExtent,
                   VkFormat format,
                   VkExtent2D extent);
    void destroy(DeviceData& dev);

    struct Counters {
        std::uint64_t stores = 0;
        std::uint64_t loads = 0;
        std::uint64_t clears = 0;
        std::uint64_t skipped = 0; // a load with nothing kept, or no private image
        std::uint64_t crops = 0;
        std::uint64_t panels = 0; // the menu panel's image kept
    };
    [[nodiscard]] const Counters& counters() const { return counters_; }

private:
    bool ensureImage(DeviceData& dev, VkFormat format, VkExtent2D extent);
    // The eye's rows [y, y + height) of the private image stretched over the whole of `source`.
    void blitBand(DeviceData& dev,
                  VkCommandBuffer cb,
                  VkImage source,
                  VkExtent2D extent,
                  std::uint32_t y,
                  std::uint32_t height);
    bool canBlit(DeviceData& dev, VkFormat format);
    // A blit needs a graphics queue; the game has presented from a compute-only family.
    bool blitsOn(DeviceData& dev, std::uint32_t family);
    bool canTakePanel(DeviceData& dev, VkFormat format);
    void setMenu(bool menu);

    VkImage image_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    VkExtent2D extent_{};
    bool inGeneral_ = false; // the private image has left UNDEFINED
    bool kept_ = false;      // it holds an eye image of this size and format
    bool failed_ = false;
    VkFormat blitChecked_ = VK_FORMAT_UNDEFINED;
    bool blitOk_ = false;
    bool noGraphicsLogged_ = false;
    VkFormat panelChecked_ = VK_FORMAT_UNDEFINED;
    bool panelOk_ = false;
    bool menu_ = false;
    bool panelLogged_ = false; // this menu's first panel was logged
    Counters counters_;
};

} // namespace evr::vkcore
