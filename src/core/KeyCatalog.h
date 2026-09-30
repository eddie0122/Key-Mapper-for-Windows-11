#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace km {

// Internal pseudo virtual-key for "either Windows key". Windows has generic
// codes for Ctrl/Alt/Shift (VK_CONTROL/VK_MENU/VK_SHIFT) but none for Win;
// 0x07 is unassigned in the Windows virtual-key table.
constexpr uint16_t VK_WIN_GENERIC = 0x07;

struct KeyInfo {
    uint16_t vk;
    const char* id;        // Stable identifier used in the settings file.
    const wchar_t* name;   // Display name.
    const char* group;     // Used for searching ("letter", "media", ...).
};

// Every key a user can choose. Keys that Windows never reports (Fn, hardware
// only keys) are deliberately absent.
const std::vector<KeyInfo>& AllKeys();
const KeyInfo* FindKey(uint16_t vk);

// Display name; unknown keys are shown as "Key 0xNN".
std::wstring KeyDisplayName(uint16_t vk);
// Settings identifier; unknown keys are written as "VK_NN" (hex).
std::string KeyId(uint16_t vk);
bool ParseKeyId(std::string_view id, uint16_t& vk);

bool IsModifierVk(uint16_t vk);
bool IsGenericModifierVk(uint16_t vk);

}  // namespace km
