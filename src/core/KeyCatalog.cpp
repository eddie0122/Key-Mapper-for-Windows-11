#include "core/KeyCatalog.h"

#include <windows.h>

#include <array>
#include <cstdio>
#include <deque>

namespace km {
namespace {

struct Storage {
    std::vector<KeyInfo> keys;
    std::array<int, 256> index{};
    std::deque<std::string> ids;     // Stable storage for generated identifiers.
    std::deque<std::wstring> names;  // Stable storage for generated names.

    void add(uint16_t vk, const char* id, const wchar_t* name, const char* group) {
        keys.push_back({vk, id, name, group});
    }
    void addGenerated(uint16_t vk, std::string id, std::wstring name, const char* group) {
        ids.push_back(std::move(id));
        names.push_back(std::move(name));
        keys.push_back({vk, ids.back().c_str(), names.back().c_str(), group});
    }

    Storage() {
        index.fill(-1);
        // Modifiers. Generic entries match either side as a source and emit
        // the left key as a destination.
        add(VK_CONTROL, "Ctrl", L"Ctrl", "modifier");
        add(VK_LCONTROL, "LCtrl", L"Ctrl (Left)", "modifier");
        add(VK_RCONTROL, "RCtrl", L"Ctrl (Right)", "modifier");
        add(VK_MENU, "Alt", L"Alt", "modifier");
        add(VK_LMENU, "LAlt", L"Alt (Left)", "modifier");
        add(VK_RMENU, "RAlt", L"Alt (Right)", "modifier");
        add(VK_SHIFT, "Shift", L"Shift", "modifier");
        add(VK_LSHIFT, "LShift", L"Shift (Left)", "modifier");
        add(VK_RSHIFT, "RShift", L"Shift (Right)", "modifier");
        add(VK_WIN_GENERIC, "Win", L"Win", "modifier");
        add(VK_LWIN, "LWin", L"Win (Left)", "modifier");
        add(VK_RWIN, "RWin", L"Win (Right)", "modifier");

        // Common editing keys.
        add(VK_RETURN, "Enter", L"Enter", "editing");
        add(VK_ESCAPE, "Esc", L"Esc", "editing");
        add(VK_BACK, "Backspace", L"Backspace", "editing");
        add(VK_TAB, "Tab", L"Tab", "editing");
        add(VK_SPACE, "Space", L"Space", "editing");
        add(VK_CAPITAL, "CapsLock", L"Caps Lock", "lock");
        add(VK_NUMLOCK, "NumLock", L"Num Lock", "lock");
        add(VK_SCROLL, "ScrollLock", L"Scroll Lock", "lock");
        add(VK_APPS, "Menu", L"Menu (Apps)", "editing");
        add(VK_SNAPSHOT, "PrintScreen", L"Print Screen", "system");
        add(VK_PAUSE, "Pause", L"Pause", "system");
        add(VK_INSERT, "Insert", L"Insert", "navigation");
        add(VK_DELETE, "Delete", L"Delete", "navigation");
        add(VK_HOME, "Home", L"Home", "navigation");
        add(VK_END, "End", L"End", "navigation");
        add(VK_PRIOR, "PageUp", L"Page Up", "navigation");
        add(VK_NEXT, "PageDown", L"Page Down", "navigation");
        add(VK_LEFT, "Left", L"Left Arrow", "navigation");
        add(VK_RIGHT, "Right", L"Right Arrow", "navigation");
        add(VK_UP, "Up", L"Up Arrow", "navigation");
        add(VK_DOWN, "Down", L"Down Arrow", "navigation");

        for (char c = 'A'; c <= 'Z'; ++c)
            addGenerated(static_cast<uint16_t>(c), std::string(1, c), std::wstring(1, wchar_t(c)),
                         "letter");
        for (char c = '0'; c <= '9'; ++c)
            addGenerated(static_cast<uint16_t>(c), std::string(1, c), std::wstring(1, wchar_t(c)),
                         "digit number");
        for (int i = 1; i <= 24; ++i)
            addGenerated(static_cast<uint16_t>(VK_F1 + i - 1), "F" + std::to_string(i),
                         L"F" + std::to_wstring(i), "function");

        // Punctuation, named after the US layout.
        add(VK_OEM_1, "Semicolon", L"; (Semicolon)", "punctuation");
        add(VK_OEM_PLUS, "Equals", L"= (Equals)", "punctuation");
        add(VK_OEM_COMMA, "Comma", L", (Comma)", "punctuation");
        add(VK_OEM_MINUS, "Minus", L"- (Minus)", "punctuation");
        add(VK_OEM_PERIOD, "Period", L". (Period)", "punctuation");
        add(VK_OEM_2, "Slash", L"/ (Slash)", "punctuation");
        add(VK_OEM_3, "Backtick", L"` (Backtick)", "punctuation");
        add(VK_OEM_4, "LBracket", L"[ (Left Bracket)", "punctuation");
        add(VK_OEM_5, "Backslash", L"\\ (Backslash)", "punctuation");
        add(VK_OEM_6, "RBracket", L"] (Right Bracket)", "punctuation");
        add(VK_OEM_7, "Quote", L"' (Quote)", "punctuation");
        add(VK_OEM_8, "Oem8", L"OEM 8", "punctuation");
        add(VK_OEM_102, "IntlBackslash", L"< > (102nd key)", "punctuation");

        // Numeric keypad.
        for (int i = 0; i <= 9; ++i)
            addGenerated(static_cast<uint16_t>(VK_NUMPAD0 + i), "Numpad" + std::to_string(i),
                         L"Num " + std::to_wstring(i), "numpad");
        add(VK_MULTIPLY, "NumpadMultiply", L"Num *", "numpad");
        add(VK_ADD, "NumpadAdd", L"Num +", "numpad");
        add(VK_SEPARATOR, "NumpadSeparator", L"Num Separator", "numpad");
        add(VK_SUBTRACT, "NumpadSubtract", L"Num -", "numpad");
        add(VK_DECIMAL, "NumpadDecimal", L"Num .", "numpad");
        add(VK_DIVIDE, "NumpadDivide", L"Num /", "numpad");
        add(VK_CLEAR, "Clear", L"Clear (Num 5)", "numpad");

        // Media and browser keys.
        add(VK_VOLUME_MUTE, "VolumeMute", L"Volume Mute", "media");
        add(VK_VOLUME_DOWN, "VolumeDown", L"Volume Down", "media");
        add(VK_VOLUME_UP, "VolumeUp", L"Volume Up", "media");
        add(VK_MEDIA_NEXT_TRACK, "MediaNext", L"Next Track", "media");
        add(VK_MEDIA_PREV_TRACK, "MediaPrev", L"Previous Track", "media");
        add(VK_MEDIA_STOP, "MediaStop", L"Stop Media", "media");
        add(VK_MEDIA_PLAY_PAUSE, "MediaPlayPause", L"Play/Pause", "media");
        add(VK_BROWSER_BACK, "BrowserBack", L"Browser Back", "browser");
        add(VK_BROWSER_FORWARD, "BrowserForward", L"Browser Forward", "browser");
        add(VK_BROWSER_REFRESH, "BrowserRefresh", L"Browser Refresh", "browser");
        add(VK_BROWSER_STOP, "BrowserStop", L"Browser Stop", "browser");
        add(VK_BROWSER_SEARCH, "BrowserSearch", L"Browser Search", "browser");
        add(VK_BROWSER_FAVORITES, "BrowserFavorites", L"Browser Favorites", "browser");
        add(VK_BROWSER_HOME, "BrowserHome", L"Browser Home", "browser");
        add(VK_LAUNCH_MAIL, "LaunchMail", L"Mail", "launch");
        add(VK_LAUNCH_MEDIA_SELECT, "LaunchMedia", L"Media Select", "launch");
        add(VK_LAUNCH_APP1, "LaunchApp1", L"Launch App 1", "launch");
        add(VK_LAUNCH_APP2, "LaunchApp2", L"Launch App 2", "launch");
        add(VK_SLEEP, "Sleep", L"Sleep", "system");

        // Input method keys (Korean, Japanese).
        add(0x15, "Hangul", L"Hangul / Kana", "ime");
        add(0x19, "Hanja", L"Hanja / Kanji", "ime");
        add(0x16, "ImeOn", L"IME On", "ime");
        add(0x1A, "ImeOff", L"IME Off", "ime");
        add(0x1C, "Convert", L"Convert", "ime");
        add(0x1D, "NonConvert", L"Non-convert", "ime");

        for (size_t i = 0; i < keys.size(); ++i) index[keys[i].vk] = static_cast<int>(i);
    }
};

const Storage& S() {
    static const Storage s;
    return s;
}

}  // namespace

const std::vector<KeyInfo>& AllKeys() { return S().keys; }

const KeyInfo* FindKey(uint16_t vk) {
    if (vk > 0xFF) return nullptr;
    int i = S().index[vk];
    return i < 0 ? nullptr : &S().keys[static_cast<size_t>(i)];
}

std::wstring KeyDisplayName(uint16_t vk) {
    if (const KeyInfo* k = FindKey(vk)) return k->name;
    wchar_t buf[32];
    swprintf(buf, 32, L"Key 0x%02X", vk);
    return buf;
}

std::string KeyId(uint16_t vk) {
    if (const KeyInfo* k = FindKey(vk)) return k->id;
    char buf[16];
    snprintf(buf, sizeof buf, "VK_%02X", vk);
    return buf;
}

bool ParseKeyId(std::string_view id, uint16_t& vk) {
    for (const KeyInfo& k : AllKeys()) {
        if (id == k.id) {
            vk = k.vk;
            return true;
        }
    }
    if (id.size() > 3 && id.substr(0, 3) == "VK_" && id.size() <= 5) {
        unsigned v = 0;
        for (char c : id.substr(3)) {
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<unsigned>(c - '0');
            else if (c >= 'A' && c <= 'F') v |= static_cast<unsigned>(c - 'A' + 10);
            else if (c >= 'a' && c <= 'f') v |= static_cast<unsigned>(c - 'a' + 10);
            else return false;
        }
        // Mouse buttons and reserved codes are not keyboard keys.
        if (v < 0x08 || v > 0xFE || v == 0xE7 || v == 0xFF) return false;
        vk = static_cast<uint16_t>(v);
        return true;
    }
    return false;
}

bool IsGenericModifierVk(uint16_t vk) {
    return vk == VK_CONTROL || vk == VK_MENU || vk == VK_SHIFT || vk == VK_WIN_GENERIC;
}

bool IsModifierVk(uint16_t vk) {
    switch (vk) {
        case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
        case VK_MENU: case VK_LMENU: case VK_RMENU:
        case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
        case VK_WIN_GENERIC: case VK_LWIN: case VK_RWIN:
            return true;
        default:
            return false;
    }
}

}  // namespace km
