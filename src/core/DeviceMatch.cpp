#include "core/DeviceMatch.h"

#include "core/Utf.h"

#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <vector>

namespace km {
namespace {

bool IsHex(wchar_t c) { return (c >= L'0' && c <= L'9') || (c >= L'A' && c <= L'F'); }

// Hex digits following "VID_", "VID&", "PID_" or "PID&" (text is upper case).
std::wstring HexAfter(const std::wstring& text, const wchar_t* tag) {
    for (const wchar_t sep : {L'_', L'&'}) {
        std::wstring needle = std::wstring(tag) + sep;
        size_t pos = text.find(needle);
        if (pos == std::wstring::npos) continue;
        pos += needle.size();
        size_t end = pos;
        while (end < text.size() && IsHex(text[end])) ++end;
        if (end - pos >= 4) return text.substr(pos, end - pos);
    }
    return {};
}

std::vector<std::wstring> Split(const std::wstring& s, wchar_t sep) {
    std::vector<std::wstring> parts;
    size_t start = 0;
    while (true) {
        size_t p = s.find(sep, start);
        parts.push_back(s.substr(start, p == std::wstring::npos ? std::wstring::npos : p - start));
        if (p == std::wstring::npos) return parts;
        start = p + 1;
    }
}

}  // namespace

std::string KeyboardIdFromInterfacePath(std::wstring_view path) {
    std::wstring up(path);
    for (wchar_t& c : up) c = static_cast<wchar_t>(std::towupper(c));
    for (const wchar_t* prefix : {L"\\\\?\\", L"\\??\\", L"\\\\.\\"}) {
        if (up.rfind(prefix, 0) == 0) {
            up.erase(0, wcslen(prefix));
            break;
        }
    }
    const std::vector<std::wstring> parts = Split(up, L'#');
    if (parts.size() < 2 || parts[0].empty() || parts[1].empty()) return {};
    const std::wstring& enumerator = parts[0];
    const std::wstring& device = parts[1];
    if (enumerator == L"ROOT" || enumerator == L"TERMINPUT_BUS" || up.find(L"RDP_KBD") != std::wstring::npos)
        return {};

    const std::wstring vid = HexAfter(device, L"VID");
    const std::wstring pid = HexAfter(device, L"PID");
    if (!vid.empty() && !pid.empty()) {
        // Bluetooth prefixes the vendor id with its source (0002 / 02).
        return WideToUtf8(L"VID_" + vid.substr(vid.size() - 4) + L"&PID_" + pid.substr(0, 4));
    }
    // No vendor/product ids: use the hardware id without per-interface parts.
    std::wstring id;
    for (const std::wstring& piece : Split(device, L'&')) {
        if (piece.rfind(L"COL", 0) == 0 || piece.rfind(L"MI_", 0) == 0) continue;
        if (!id.empty()) id += L'&';
        id += piece;
    }
    return WideToUtf8(enumerator + L"\\" + id);
}

std::wstring DefaultKeyboardName(const std::string& id) {
    if (id.rfind("VID_", 0) == 0 && id.size() >= 17)
        return L"Keyboard (VID " + Utf8ToWide(id.substr(4, 4)) + L", PID " + Utf8ToWide(id.substr(13, 4)) + L")";
    if (id.rfind("ACPI\\", 0) == 0) return L"Built-in keyboard";
    return L"Keyboard (" + Utf8ToWide(id) + L")";
}

const Profile* ProfileForKeyboard(const Settings& s, const std::string& deviceId) {
    for (const Profile& p : s.profiles)
        for (const KeyboardRef& k : p.keyboards)
            if (k.id == deviceId) return &p;
    return nullptr;
}

namespace autoswitch {

bool OnConnected(Settings& s, const std::string& deviceId) {
    if (!s.autoSwitch) return false;
    const Profile* p = ProfileForKeyboard(s, deviceId);
    if (!p) return false;
    auto& stack = s.switchStack;
    stack.erase(std::remove_if(stack.begin(), stack.end(),
                               [&](const SwitchEntry& e) { return e.deviceId == deviceId; }),
                stack.end());
    stack.push_back({deviceId, p->id, s.activeProfileId});
    if (s.activeProfileId == p->id) return false;
    s.activeProfileId = p->id;
    return true;
}

bool OnDisconnected(Settings& s, const std::string& deviceId) {
    auto& stack = s.switchStack;
    auto it = std::find_if(stack.begin(), stack.end(), [&](const SwitchEntry& e) { return e.deviceId == deviceId; });
    if (it == stack.end()) return false;
    const SwitchEntry entry = *it;
    const bool wasLatest = it + 1 == stack.end();
    it = stack.erase(it);
    if (!wasLatest) {
        // A later switch replaced this one; hand it the profile to return to.
        it->previousProfileId = entry.previousProfileId;
        return false;
    }
    if (!s.autoSwitch || s.activeProfileId != entry.profileId || entry.previousProfileId == entry.profileId ||
        !s.find(entry.previousProfileId))
        return false;
    s.activeProfileId = entry.previousProfileId;
    return true;
}

bool Reconcile(Settings& s, const std::set<std::string>& connected) {
    if (!s.autoSwitch) {
        s.switchStack.clear();
        return false;
    }
    const std::string before = s.activeProfileId;
    // Newest first, so each removal restores the right profile.
    for (size_t i = s.switchStack.size(); i-- > 0;) {
        if (i < s.switchStack.size() && !connected.count(s.switchStack[i].deviceId))
            OnDisconnected(s, s.switchStack[i].deviceId);
    }
    for (const Profile& p : std::vector<Profile>(s.profiles)) {
        for (const KeyboardRef& k : p.keyboards) {
            const bool onStack = std::any_of(s.switchStack.begin(), s.switchStack.end(),
                                             [&](const SwitchEntry& e) { return e.deviceId == k.id; });
            if (connected.count(k.id) && !onStack) OnConnected(s, k.id);
        }
    }
    return s.activeProfileId != before;
}

void OnManualActivation(Settings& s) { s.switchStack.clear(); }

}  // namespace autoswitch

}  // namespace km
