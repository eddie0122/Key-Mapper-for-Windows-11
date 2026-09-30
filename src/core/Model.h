#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace km {

// How a modifier participates in a shortcut: absent, either side, or one side.
enum class Side : uint8_t { None = 0, Either = 1, Left = 2, Right = 3 };

enum ModType : int { MT_Ctrl = 0, MT_Alt = 1, MT_Shift = 2, MT_Win = 3, MT_Count = 4 };

// Order used when displaying shortcuts ("Win + Ctrl + Alt + Shift + K").
inline constexpr std::array<ModType, MT_Count> kDisplayModOrder = {MT_Win, MT_Ctrl, MT_Alt, MT_Shift};

// Maps a modifier virtual key (generic or side-specific) to its type and side.
bool ModFromVk(uint16_t vk, ModType& type, Side& side);
uint16_t VkFromMod(ModType type, Side side);

// A mapping endpoint: either a single key, or modifiers plus one
// non-modifier action key.
struct KeyCombo {
    std::array<Side, MT_Count> mods{};
    uint16_t key = 0;

    bool empty() const { return key == 0; }
    bool hasMods() const;
    bool isSingleKey() const;   // Exactly one key and no modifiers.
    bool isShortcut() const;    // One or more modifiers and a non-modifier key.
    bool isValidEndpoint() const { return isSingleKey() || isShortcut(); }
    bool operator==(const KeyCombo&) const = default;

    static KeyCombo Single(uint16_t vk);
};

struct Mapping {
    KeyCombo from;
    KeyCombo to;
    bool operator==(const Mapping&) const = default;
};

// A physical keyboard, identified by a stable id such as "VID_046D&PID_B35B"
// (see KeyboardIdFromInterfacePath). The name is remembered for display
// while the keyboard is unplugged.
struct KeyboardRef {
    std::string id;
    std::wstring name;
    bool operator==(const KeyboardRef&) const = default;
};

struct Profile {
    std::string id;
    std::wstring name;
    std::vector<Mapping> keys;       // "Remap a key": single-key sources.
    std::vector<Mapping> shortcuts;  // "Remap a shortcut": shortcut sources.
    std::vector<KeyboardRef> keyboards;  // Connecting one of these activates the profile.
    bool operator==(const Profile&) const = default;
};

// One automatic switch: `device` connecting activated `profileId`, replacing
// `previousProfileId`, which comes back when the device disconnects.
struct SwitchEntry {
    std::string deviceId;
    std::string profileId;
    std::string previousProfileId;
    bool operator==(const SwitchEntry&) const = default;
};

// What makes a keyboard's linked profile active.
enum class SwitchMode : uint8_t {
    Connect = 0,  // The keyboard connecting; disconnecting restores the previous profile.
    Typing = 1,   // Typing on the keyboard.
    Manual = 2,   // Nothing: links are kept but ignored.
};

struct Settings {
    bool enabled = true;
    std::string activeProfileId;
    std::vector<Profile> profiles;
    SwitchMode switchMode = SwitchMode::Connect;
    std::vector<SwitchEntry> switchStack;  // Most recent automatic switch last (Connect mode only).

    Profile* find(std::string_view id);
    const Profile* find(std::string_view id) const;
    int indexOf(std::string_view id) const;
    bool operator==(const Settings&) const = default;
};

// First-launch settings: one empty "Default" profile, remapping enabled.
Settings DefaultSettings();
std::string NewProfileId();

std::wstring ModDisplayName(ModType type, Side side);
std::wstring FormatCombo(const KeyCombo& c);  // "Ctrl + Shift + J", "Caps Lock"
std::string ComboToText(const KeyCombo& c);   // "Ctrl+Shift+J", "CapsLock"
bool ParseComboText(std::string_view text, KeyCombo& out);

// ---- Validation -----------------------------------------------------------

enum class Section { Keys = 0, Shortcuts = 1 };

struct Issue {
    Section section;
    size_t row;
    bool missing;          // A required input has not been chosen yet.
    std::wstring message;
};

// Returns at most one issue per row.
std::vector<Issue> ValidateProfile(const Profile& p);
// Combinations Windows reserves (Ctrl+Alt+Del, Win+L). Sets `why`.
bool IsReservedCombo(const KeyCombo& c, std::wstring* why = nullptr);

}  // namespace km
