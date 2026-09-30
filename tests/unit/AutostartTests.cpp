// Uses a separate value name so the real "Keymapper" startup entry is never touched.

#include "Test.h"

#include "win/Autostart.h"

#include <windows.h>

using namespace km;

namespace {

constexpr const wchar_t* kTestValue = L"KeymapperTest";
constexpr const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr const wchar_t* kApprovedKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";

std::wstring RunValue() {
    wchar_t buf[1024] = {};
    DWORD bytes = sizeof buf;
    if (RegGetValueW(HKEY_CURRENT_USER, kRunKey, kTestValue, RRF_RT_REG_SZ, nullptr, buf, &bytes) != ERROR_SUCCESS)
        return L"<none>";
    return buf;
}

struct Cleanup {
    ~Cleanup() {
        RegDeleteKeyValueW(HKEY_CURRENT_USER, kRunKey, kTestValue);
        RegDeleteKeyValueW(HKEY_CURRENT_USER, kApprovedKey, kTestValue);
    }
};

}  // namespace

TEST(AutostartEnableAndDisable) {
    Cleanup cleanup;
    std::wstring err;
    CHECK(autostart::SetEnabled(false, err, kTestValue));  // Disabling when absent is fine.
    CHECK(!autostart::IsEnabled(kTestValue));

    CHECK(autostart::SetEnabled(true, err, kTestValue));
    CHECK(autostart::IsEnabled(kTestValue));
    CHECK(RunValue() == autostart::LaunchCommand());
    CHECK(autostart::LaunchCommand().front() == L'"');  // Quoted: paths may contain spaces.

    CHECK(autostart::SetEnabled(false, err, kTestValue));
    CHECK(!autostart::IsEnabled(kTestValue));
    CHECK(RunValue() == L"<none>");
}

TEST(AutostartIgnoresOtherCopies) {
    Cleanup cleanup;
    // An entry for a copy of Keymapper in another folder is not "this" copy.
    const wchar_t other[] = L"\"C:\\Elsewhere\\Keymapper.exe\"";
    RegSetKeyValueW(HKEY_CURRENT_USER, kRunKey, kTestValue, REG_SZ, other, sizeof other);
    CHECK(!autostart::IsEnabled(kTestValue));
    std::wstring err;
    CHECK(autostart::SetEnabled(true, err, kTestValue));
    CHECK(RunValue() == autostart::LaunchCommand());
}

TEST(AutostartHonoursStartupAppsToggle) {
    Cleanup cleanup;
    std::wstring err;
    CHECK(autostart::SetEnabled(true, err, kTestValue));
    // What Settings > Apps > Startup writes when the user switches an entry off.
    const BYTE disabled[12] = {0x03};
    RegSetKeyValueW(HKEY_CURRENT_USER, kApprovedKey, kTestValue, REG_BINARY, disabled, sizeof disabled);
    CHECK(!autostart::IsEnabled(kTestValue));
    // Turning it on again from Keymapper clears the "off" mark.
    CHECK(autostart::SetEnabled(true, err, kTestValue));
    CHECK(autostart::IsEnabled(kTestValue));
}
