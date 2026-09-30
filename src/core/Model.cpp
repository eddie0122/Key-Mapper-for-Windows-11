#include "core/Model.h"

#include "core/KeyCatalog.h"

#include <windows.h>

#include <chrono>
#include <cstdio>
#include <random>

namespace km {

bool ModFromVk(uint16_t vk, ModType& type, Side& side) {
    switch (vk) {
        case VK_CONTROL: type = MT_Ctrl; side = Side::Either; return true;
        case VK_LCONTROL: type = MT_Ctrl; side = Side::Left; return true;
        case VK_RCONTROL: type = MT_Ctrl; side = Side::Right; return true;
        case VK_MENU: type = MT_Alt; side = Side::Either; return true;
        case VK_LMENU: type = MT_Alt; side = Side::Left; return true;
        case VK_RMENU: type = MT_Alt; side = Side::Right; return true;
        case VK_SHIFT: type = MT_Shift; side = Side::Either; return true;
        case VK_LSHIFT: type = MT_Shift; side = Side::Left; return true;
        case VK_RSHIFT: type = MT_Shift; side = Side::Right; return true;
        case VK_WIN_GENERIC: type = MT_Win; side = Side::Either; return true;
        case VK_LWIN: type = MT_Win; side = Side::Left; return true;
        case VK_RWIN: type = MT_Win; side = Side::Right; return true;
        default: return false;
    }
}

uint16_t VkFromMod(ModType type, Side side) {
    static const uint16_t table[MT_Count][3] = {
        {VK_CONTROL, VK_LCONTROL, VK_RCONTROL},
        {VK_MENU, VK_LMENU, VK_RMENU},
        {VK_SHIFT, VK_LSHIFT, VK_RSHIFT},
        {VK_WIN_GENERIC, VK_LWIN, VK_RWIN},
    };
    if (side == Side::None) return 0;
    return table[type][static_cast<int>(side) - 1];
}

bool KeyCombo::hasMods() const {
    for (Side s : mods)
        if (s != Side::None) return true;
    return false;
}

bool KeyCombo::isSingleKey() const { return key != 0 && !hasMods(); }

bool KeyCombo::isShortcut() const { return key != 0 && hasMods() && !IsModifierVk(key); }

KeyCombo KeyCombo::Single(uint16_t vk) {
    KeyCombo c;
    c.key = vk;
    return c;
}

Profile* Settings::find(std::string_view id) {
    for (Profile& p : profiles)
        if (p.id == id) return &p;
    return nullptr;
}

const Profile* Settings::find(std::string_view id) const {
    for (const Profile& p : profiles)
        if (p.id == id) return &p;
    return nullptr;
}

int Settings::indexOf(std::string_view id) const {
    for (size_t i = 0; i < profiles.size(); ++i)
        if (profiles[i].id == id) return static_cast<int>(i);
    return -1;
}

std::string NewProfileId() {
    static std::mt19937_64 rng([] {
        std::random_device rd;
        uint64_t seed = (static_cast<uint64_t>(rd()) << 32) ^ rd();
        seed ^= static_cast<uint64_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
        return seed;
    }());
    char buf[24];
    snprintf(buf, sizeof buf, "p%016llx", static_cast<unsigned long long>(rng()));
    return buf;
}

Settings DefaultSettings() {
    Settings s;
    Profile p;
    p.id = NewProfileId();
    p.name = L"Default";
    s.activeProfileId = p.id;
    s.profiles.push_back(std::move(p));
    s.enabled = true;
    return s;
}

std::wstring ModDisplayName(ModType type, Side side) {
    return KeyDisplayName(VkFromMod(type, side));
}

std::wstring FormatCombo(const KeyCombo& c) {
    std::wstring out;
    for (ModType t : kDisplayModOrder) {
        if (c.mods[t] == Side::None) continue;
        out += ModDisplayName(t, c.mods[t]);
        out += L" + ";
    }
    if (c.key) out += KeyDisplayName(c.key);
    else if (!out.empty()) out += L"…";  // Incomplete shortcut: "Ctrl + …"
    return out;
}

std::string ComboToText(const KeyCombo& c) {
    std::string out;
    for (ModType t : kDisplayModOrder) {
        if (c.mods[t] == Side::None) continue;
        out += KeyId(VkFromMod(t, c.mods[t]));
        out += '+';
    }
    if (c.key) out += KeyId(c.key);
    return out;
}

bool ParseComboText(std::string_view text, KeyCombo& out) {
    out = KeyCombo{};
    std::vector<std::string_view> parts;
    size_t start = 0;
    while (true) {
        size_t p = text.find('+', start);
        parts.push_back(text.substr(start, p == std::string_view::npos ? std::string_view::npos : p - start));
        if (p == std::string_view::npos) break;
        start = p + 1;
    }
    for (size_t i = 0; i < parts.size(); ++i) {
        uint16_t vk = 0;
        if (!ParseKeyId(parts[i], vk)) return false;
        if (i + 1 < parts.size()) {
            ModType t;
            Side s;
            if (!ModFromVk(vk, t, s) || out.mods[t] != Side::None) return false;
            out.mods[t] = s;
        } else {
            out.key = vk;
        }
    }
    return out.isValidEndpoint();
}

// ---- Validation -----------------------------------------------------------

bool IsReservedCombo(const KeyCombo& c, std::wstring* why) {
    if (!c.isShortcut()) return false;
    auto has = [&](ModType t) { return c.mods[t] != Side::None; };
    if (has(MT_Ctrl) && has(MT_Alt) && c.key == VK_DELETE) {
        if (why) *why = L"Ctrl+Alt+Del is reserved by Windows.";
        return true;
    }
    if (has(MT_Win) && !has(MT_Ctrl) && !has(MT_Alt) && !has(MT_Shift) && c.key == 'L') {
        if (why) *why = L"Win+L is reserved by Windows.";
        return true;
    }
    return false;
}

namespace {

// A source side is reproduced exactly by a destination side when they are the
// same, or when a left-only source maps to a generic destination (which emits
// the left key).
bool SideReproduced(Side src, Side dst) {
    return src == dst || (src == Side::Left && dst == Side::Either);
}

bool KeyReproduced(uint16_t src, uint16_t dst) {
    if (src == dst) return true;
    ModType ts, td;
    Side ss, sd;
    if (ModFromVk(src, ts, ss) && ModFromVk(dst, td, sd)) return ts == td && SideReproduced(ss, sd);
    return false;
}

bool IsIdentity(const Mapping& m) {
    if (!KeyReproduced(m.from.key, m.to.key)) return false;
    for (int t = 0; t < MT_Count; ++t)
        if (!SideReproduced(m.from.mods[t], m.to.mods[t])) return false;
    return true;
}

void ValidateSection(const std::vector<Mapping>& rows, Section section, std::vector<Issue>& issues) {
    const bool shortcutSection = section == Section::Shortcuts;
    for (size_t i = 0; i < rows.size(); ++i) {
        const Mapping& m = rows[i];
        auto add = [&](bool missing, std::wstring msg) {
            issues.push_back({section, i, missing, std::move(msg)});
        };
        std::wstring why;
        if (m.from.empty()) {
            add(true, shortcutSection ? L"Select a shortcut to remap." : L"Select a key to remap.");
        } else if (m.to.empty()) {
            add(true, L"Select what it should send.");
        } else if (!shortcutSection && !m.from.isSingleKey()) {
            add(false, L"Choose a single key here; use “Remap a shortcut” for shortcuts.");
        } else if (shortcutSection && !m.from.isShortcut()) {
            add(false, L"A shortcut needs at least one modifier (Ctrl, Alt, Shift, Win) and one other key.");
        } else if (!m.to.isValidEndpoint()) {
            add(false, L"The destination must be a key, or modifiers plus one non-modifier key.");
        } else if (IsReservedCombo(m.from, &why) || IsReservedCombo(m.to, &why)) {
            add(false, why);
        } else if (IsIdentity(m)) {
            add(false, L"The source and destination are the same.");
        } else {
            for (size_t j = 0; j < i; ++j) {
                if (!rows[j].from.empty() && rows[j].from == m.from) {
                    add(false, L"Duplicate source; already remapped in row " + std::to_wstring(j + 1) + L".");
                    break;
                }
            }
        }
    }
}

}  // namespace

std::vector<Issue> ValidateProfile(const Profile& p) {
    std::vector<Issue> issues;
    ValidateSection(p.keys, Section::Keys, issues);
    ValidateSection(p.shortcuts, Section::Shortcuts, issues);
    return issues;
}

}  // namespace km
