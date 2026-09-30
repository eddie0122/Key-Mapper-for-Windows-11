#pragma once

#include "core/Model.h"
#include "win/InputHook.h"

#include <windows.h>

#include <string>

namespace km {

// Modal dialog for choosing a mapping endpoint, either by pressing it (input
// capture suspends remapping) or by picking from a searchable key list plus
// modifier choices. Confirmation is mouse-driven while capturing, so Enter and
// Esc can themselves be captured.
class SelectKeyDialog {
public:
    enum class Mode {
        SingleKey,      // Source of "Remap a key".
        Shortcut,       // Source of "Remap a shortcut".
        KeyOrShortcut,  // Any destination.
    };

    static bool Show(HWND owner, InputHook& hook, Mode mode, const std::wstring& title, KeyCombo& value);
};

}  // namespace km
