#include "features/input/usercmd_hook_use.hpp"

namespace evr::input {

CommandHookUse commandHookUse(bool controllers, InputPath path, bool bodyFollow) {
    if (controllers && path != InputPath::XInput) {
        return CommandHookUse::Controllers;
    }
    return bodyFollow ? CommandHookUse::BodyFollow : CommandHookUse::None;
}

} // namespace evr::input
