#pragma once

#include <string>

namespace km::autostart {

// Launch at sign-in through the per-user Run key
// (HKCU\Software\Microsoft\Windows\CurrentVersion\Run). The registry, not the
// portable settings file, is the source of truth: the entry belongs to this
// PC and points at this copy of Keymapper.exe.

constexpr const wchar_t* kValueName = L"Keymapper";

// True when the Run entry launches this executable and the user has not
// switched it off in Settings > Apps > Startup (or Task Manager).
bool IsEnabled(const wchar_t* valueName = kValueName);

// Adds or removes the Run entry. Enabling also clears a "disabled" mark left
// by Settings or Task Manager. On failure returns false with a message.
bool SetEnabled(bool enabled, std::wstring& error, const wchar_t* valueName = kValueName);

// The command stored in the Run entry: the quoted path of this executable.
std::wstring LaunchCommand();

}  // namespace km::autostart
