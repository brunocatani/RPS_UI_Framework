#pragma once
#include "RPSUIInputApi.h"

namespace rpsui::input {
bool start() noexcept;
bool installed() noexcept;
void sessionReady(bool ready) noexcept;
bool rawButton(unsigned hand,unsigned button) noexcept;
// The panel host uses capture slot zero, independent of consumer registrations.
bool captureHost(unsigned hand,bool active,bool configNavigation) noexcept;
}
