#include "win/Autostart.h"

#include "core/SettingsStore.h"

#include <windows.h>

#include <vector>

namespace km::autostart {
namespace {

constexpr const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr const wchar_t* kApprovedKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";

std::wstring ExecutablePath() {
    std::wstring path(MAX_PATH, L'\0');
    while (true) {
        DWORD n = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (n < path.size()) {
            path.resize(n);
            return path;
        }
        path.resize(path.size() * 2);
    }
}

bool ReadString(const wchar_t* key, const wchar_t* name, std::wstring& out) {
    DWORD bytes = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, key, name, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, nullptr,
                     nullptr, &bytes) != ERROR_SUCCESS)
        return false;
    out.assign(bytes / sizeof(wchar_t) + 1, L'\0');
    if (RegGetValueW(HKEY_CURRENT_USER, key, name, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, nullptr,
                     out.data(), &bytes) != ERROR_SUCCESS)
        return false;
    out.resize(wcslen(out.c_str()));
    return true;
}

// Settings and Task Manager record their Startup toggle here: the first byte
// is even when the entry is allowed to run and odd when it is switched off.
bool DisabledByUser(const wchar_t* name) {
    BYTE data[16] = {};
    DWORD bytes = sizeof data;
    if (RegGetValueW(HKEY_CURRENT_USER, kApprovedKey, name, RRF_RT_REG_BINARY, nullptr, data, &bytes) != ERROR_SUCCESS)
        return false;
    return bytes > 0 && (data[0] & 1) != 0;
}

bool SameCommand(const std::wstring& a, const std::wstring& b) {
    return CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()), b.c_str(), static_cast<int>(b.size()), TRUE) ==
           CSTR_EQUAL;
}

}  // namespace

std::wstring LaunchCommand() { return L"\"" + ExecutablePath() + L"\""; }

bool IsEnabled(const wchar_t* valueName) {
    std::wstring command;
    if (!ReadString(kRunKey, valueName, command)) return false;
    return SameCommand(command, LaunchCommand()) && !DisabledByUser(valueName);
}

bool SetEnabled(bool enabled, std::wstring& error, const wchar_t* valueName) {
    LSTATUS rc;
    if (enabled) {
        const std::wstring command = LaunchCommand();
        rc = RegSetKeyValueW(HKEY_CURRENT_USER, kRunKey, valueName, REG_SZ, command.c_str(),
                             static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
        if (rc == ERROR_SUCCESS) {
            // Undo an earlier "Disabled" choice in Settings > Apps > Startup.
            LSTATUS approved = RegDeleteKeyValueW(HKEY_CURRENT_USER, kApprovedKey, valueName);
            if (approved != ERROR_SUCCESS && approved != ERROR_FILE_NOT_FOUND) rc = approved;
        }
    } else {
        rc = RegDeleteKeyValueW(HKEY_CURRENT_USER, kRunKey, valueName);
        if (rc == ERROR_FILE_NOT_FOUND) rc = ERROR_SUCCESS;
        RegDeleteKeyValueW(HKEY_CURRENT_USER, kApprovedKey, valueName);
    }
    if (rc != ERROR_SUCCESS) {
        error = SettingsStore::ErrorText(static_cast<unsigned long>(rc));
        return false;
    }
    return true;
}

}  // namespace km::autostart
