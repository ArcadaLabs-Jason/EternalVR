#include "vkcore/view_shared_state.hpp"

#include "vkcore/view_query_copy.hpp"
#include "vkcore/view_shadow_cache.hpp"
#include "vkcore/view_skin_alloc.hpp"
#include "vkcore/view_water.hpp"

namespace evr::vkcore {

bool prepareViewSharedState(const std::byte* base) {
    return prepareViewSkinAlloc(base) && prepareViewQueryCopy(base) && prepareViewShadowCache(base);
}

bool installViewSharedState(const std::byte* base) {
    return installViewSkinAlloc(base) && installViewQueryCopy(base) && installViewShadowCache(base);
}

void viewSharedStateFrameStart(bool view0Dispatched) {
    viewSkinAllocFrameStart(view0Dispatched);
    viewShadowCacheFrameStart(view0Dispatched);
    viewWaterFrameStart(view0Dispatched);
}

void viewSharedStateLogCounts() {
    viewSkinAllocLogCounts();
    viewQueryCopyLogCounts();
    viewShadowCacheLogCounts();
}

} // namespace evr::vkcore
