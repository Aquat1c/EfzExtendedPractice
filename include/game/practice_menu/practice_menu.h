#pragma once
// Reroutes the title PRACTICE entry (index 3) into a native submenu. The title menu count, wrap
// bounds, geometry, jump table and vtable stay unpatched so InGameNetplay's NETPLAY entry coexists.

#include "mission_title_screen.h"

namespace PracticeMenu {

bool Install();

void Uninstall();

bool IsSubmenuActive();

void RequestTitleReopen(TitleScreen::Screen screen);

}
