#pragma once

#include "core/Model.h"

#include <set>
#include <string>
#include <string_view>

namespace km {

// Stable identity of a keyboard from one of its device-interface paths:
//   USB        \\?\HID#VID_046D&PID_C52B&MI_00#7&...#{guid}              -> VID_046D&PID_C52B
//   Bluetooth  \\?\HID#{00001124-...}_VID&0002046D_PID&B35B&Col01#...   -> VID_046D&PID_B35B
//   BLE        \\?\HID#{00001812-...}_Dev_VID&02046D_PID&B35B_REV&...#.. -> VID_046D&PID_B35B
//   Built-in   \\?\ACPI#MSF0001#4&...#{guid}                             -> ACPI\MSF0001
// Every interface of one keyboard maps to the same id. Returns an empty
// string for virtual keyboards that should not be offered (remote desktop,
// root-enumerated software devices).
std::string KeyboardIdFromInterfacePath(std::wstring_view path);

// Fallback display name when Windows provides nothing better.
std::wstring DefaultKeyboardName(const std::string& id);

// The profile a keyboard is assigned to, or null.
const Profile* ProfileForKeyboard(const Settings& s, const std::string& deviceId);

// Automatic profile switching. Each function updates `s` (its switch stack
// and active profile) and returns true when the active profile changed.
namespace autoswitch {

// An assigned keyboard connected: activate its profile, remembering the
// profile it replaces.
bool OnConnected(Settings& s, const std::string& deviceId);
// A keyboard disconnected: if its switch is the latest and still in effect,
// return to the profile that was active before it connected.
bool OnDisconnected(Settings& s, const std::string& deviceId);
// Startup (or re-enabling): forget switches whose keyboards are gone, then
// treat connected, assigned keyboards not yet on the stack as new connections.
bool Reconcile(Settings& s, const std::set<std::string>& connected);
// The user picked a profile by hand; later disconnects leave it alone.
void OnManualActivation(Settings& s);

// Typing mode: a key arrived from `deviceId`. Switches only when the device
// differs from `lastTyped` (updated here), so a hand-picked profile stays
// while the user keeps typing on the same keyboard.
bool OnTyped(Settings& s, const std::string& deviceId, std::string& lastTyped);

}  // namespace autoswitch

}  // namespace km
