#pragma once

#include <windows.h>

#include <string>

namespace km {

class App;

// Chooses which keyboards activate a profile when they connect. Lists the
// keyboards connected now plus any remembered by a profile, and can identify
// a keyboard by typing on it. Saves on OK.
class KeyboardsDialog {
public:
    static void Show(HWND owner, App& app, const std::string& profileId);
};

}  // namespace km
