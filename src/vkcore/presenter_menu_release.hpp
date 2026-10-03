#pragma once

// Letting go of the menu's held input when the XR worker stops updating the menu (presenter_menu.cpp).

#include "vkcore/xr_presenter.hpp"

namespace evr::vkcore {

// Sends the release of everything the menu router holds down (the Dossier map's button and W A S D, held
// keys), shows the game's cursor again and stops holding back gameplay input; `why` goes in the log line.
// Nothing happens when nothing is held. XR worker only.
void releaseMenuInput(XrPresenter::Impl& p, const char* why);

} // namespace evr::vkcore
