#pragma once

#include <windows.h>

#include <string>

namespace km {

// Modal single-line text prompt used for profile names. Returns false when
// cancelled; the OK button is disabled while the text is blank.
bool PromptText(HWND owner, const std::wstring& title, const std::wstring& label, std::wstring& value);

}  // namespace km
