#include "Test.h"

#include "core/DeviceMatch.h"
#include "core/SettingsIO.h"

using namespace km;

TEST(KeyboardIdsFromRealWorldPaths) {
    // USB keyboard / receiver, several interfaces of one device.
    CHECK_EQ(KeyboardIdFromInterfacePath(
                 L"\\\\?\\HID#VID_046D&PID_C52B&MI_00#7&2a3b4c5d&0&0000#{884b96c3-56ef-11d1-bc8c-00a0c91405dd}"),
             std::string("VID_046D&PID_C52B"));
    CHECK_EQ(KeyboardIdFromInterfacePath(
                 L"\\\\?\\HID#VID_046D&PID_C52B&MI_01&Col01#8&1c2d&0&0000#{884b96c3-56ef-11d1-bc8c-00a0c91405dd}"),
             std::string("VID_046D&PID_C52B"));
    // Lower case paths are normalized.
    CHECK_EQ(KeyboardIdFromInterfacePath(L"\\\\?\\hid#vid_3434&pid_0281&mi_00#8&abc&0&0000#{884b96c3}"),
             std::string("VID_3434&PID_0281"));
    // Bluetooth Classic: vendor id carries a 0002 source prefix.
    CHECK_EQ(KeyboardIdFromInterfacePath(
                 L"\\\\?\\HID#{00001124-0000-1000-8000-00805f9b34fb}_VID&0002046d_PID&b35b&Col01#9&1a&0&0000#{884b96c3}"),
             std::string("VID_046D&PID_B35B"));
    // Bluetooth LE (HID over GATT).
    CHECK_EQ(KeyboardIdFromInterfacePath(
                 L"\\\\?\\HID#{00001812-0000-1000-8000-00805f9b34fb}_Dev_VID&02046d_PID&b35b_REV&0012_d1f2#9&2b&0&0000#{884b96c3}"),
             std::string("VID_046D&PID_B35B"));
    // Built-in laptop keyboard and I2C HID keyboards without VID/PID.
    CHECK_EQ(KeyboardIdFromInterfacePath(L"\\\\?\\ACPI#MSF0001#4&1d401fb5&0#{884b96c3-56ef-11d1-bc8c-00a0c91405dd}"),
             std::string("ACPI\\MSF0001"));
    CHECK_EQ(KeyboardIdFromInterfacePath(L"\\\\?\\HID#VEN_ELAN&DEV_0001&Col02#5&abc&0&0001#{884b96c3}"),
             std::string("HID\\VEN_ELAN&DEV_0001"));
    // Virtual keyboards are not offered.
    CHECK_EQ(KeyboardIdFromInterfacePath(L"\\\\?\\Root#RDP_KBD#0000#{884b96c3}"), std::string());
    CHECK_EQ(KeyboardIdFromInterfacePath(L"\\\\?\\TERMINPUT_BUS#UMB#2&1&0&0&0&S0&RDP_KBD#{884b96c3}"), std::string());
    CHECK_EQ(KeyboardIdFromInterfacePath(L"garbage"), std::string());
}

TEST(KeyboardDefaultNames) {
    CHECK(DefaultKeyboardName("VID_046D&PID_C52B") == L"Keyboard (VID 046D, PID C52B)");
    CHECK(DefaultKeyboardName("ACPI\\MSF0001") == L"Built-in keyboard");
}

namespace {

// Profiles: laptop (built-in keyboard), mech (K2), travel (K380, also a USB receiver).
Settings SwitchSettings() {
    Settings s;
    s.profiles = {{"laptop", L"Laptop", {}, {}, {{"ACPI\\MSF0001", L"Built-in"}}},
                  {"mech", L"Mechanical", {}, {}, {{"VID_3434&PID_0281", L"Keychron K2"}}},
                  {"travel", L"Travel", {}, {}, {{"VID_046D&PID_B35B", L"K380"}, {"VID_046D&PID_C52B", L"Receiver"}}},
                  {"plain", L"Plain", {}, {}, {}}};
    s.activeProfileId = "plain";
    return s;
}

}  // namespace

TEST(AutoSwitchConnectAndReturn) {
    Settings s = SwitchSettings();
    CHECK(autoswitch::OnConnected(s, "VID_3434&PID_0281"));
    CHECK_EQ(s.activeProfileId, std::string("mech"));
    CHECK(!autoswitch::OnConnected(s, "VID_FFFF&PID_0001"));  // Unassigned keyboard: no change.
    CHECK(autoswitch::OnDisconnected(s, "VID_3434&PID_0281"));
    CHECK_EQ(s.activeProfileId, std::string("plain"));
    CHECK(s.switchStack.empty());
    CHECK(!autoswitch::OnDisconnected(s, "VID_3434&PID_0281"));  // Already gone.
}

TEST(AutoSwitchStackedKeyboardsUnplugInEitherOrder) {
    // Newest unplugged first: step back one at a time.
    Settings s = SwitchSettings();
    autoswitch::OnConnected(s, "VID_3434&PID_0281");  // plain -> mech
    autoswitch::OnConnected(s, "VID_046D&PID_B35B");  // mech -> travel
    CHECK_EQ(s.activeProfileId, std::string("travel"));
    CHECK(autoswitch::OnDisconnected(s, "VID_046D&PID_B35B"));
    CHECK_EQ(s.activeProfileId, std::string("mech"));
    CHECK(autoswitch::OnDisconnected(s, "VID_3434&PID_0281"));
    CHECK_EQ(s.activeProfileId, std::string("plain"));

    // Oldest unplugged first: the active profile stays, and the chain is kept.
    Settings t = SwitchSettings();
    autoswitch::OnConnected(t, "VID_3434&PID_0281");
    autoswitch::OnConnected(t, "VID_046D&PID_B35B");
    CHECK(!autoswitch::OnDisconnected(t, "VID_3434&PID_0281"));
    CHECK_EQ(t.activeProfileId, std::string("travel"));
    CHECK(autoswitch::OnDisconnected(t, "VID_046D&PID_B35B"));
    CHECK_EQ(t.activeProfileId, std::string("plain"));
}

TEST(AutoSwitchTwoKeyboardsOfOneProfile) {
    Settings s = SwitchSettings();
    autoswitch::OnConnected(s, "VID_046D&PID_B35B");  // plain -> travel
    CHECK(!autoswitch::OnConnected(s, "VID_046D&PID_C52B"));  // already travel
    CHECK(!autoswitch::OnDisconnected(s, "VID_046D&PID_B35B"));  // receiver still there
    CHECK_EQ(s.activeProfileId, std::string("travel"));
    CHECK(autoswitch::OnDisconnected(s, "VID_046D&PID_C52B"));
    CHECK_EQ(s.activeProfileId, std::string("plain"));
}

TEST(AutoSwitchManualChoiceWins) {
    Settings s = SwitchSettings();
    autoswitch::OnConnected(s, "VID_3434&PID_0281");
    s.activeProfileId = "laptop";
    autoswitch::OnManualActivation(s);
    CHECK(!autoswitch::OnDisconnected(s, "VID_3434&PID_0281"));
    CHECK_EQ(s.activeProfileId, std::string("laptop"));
}

TEST(AutoSwitchSkipsDeletedPreviousProfile) {
    Settings s = SwitchSettings();
    autoswitch::OnConnected(s, "VID_3434&PID_0281");  // previous = plain
    s.profiles.pop_back();                            // delete "plain"
    CHECK(!autoswitch::OnDisconnected(s, "VID_3434&PID_0281"));
    CHECK_EQ(s.activeProfileId, std::string("mech"));
}

TEST(AutoSwitchConnectEventsIgnoredOutsideConnectMode) {
    for (SwitchMode mode : {SwitchMode::Manual, SwitchMode::Typing}) {
        Settings s = SwitchSettings();
        s.switchMode = mode;
        CHECK(!autoswitch::OnConnected(s, "VID_3434&PID_0281"));
        CHECK_EQ(s.activeProfileId, std::string("plain"));
        CHECK(!autoswitch::Reconcile(s, {"VID_3434&PID_0281"}));
        CHECK(!autoswitch::OnDisconnected(s, "VID_3434&PID_0281"));
        CHECK(s.switchStack.empty());
    }
}

TEST(AutoSwitchOnTyping) {
    Settings s = SwitchSettings();
    s.switchMode = SwitchMode::Typing;
    std::string last;
    CHECK(autoswitch::OnTyped(s, "VID_3434&PID_0281", last));  // plain -> mech
    CHECK_EQ(s.activeProfileId, std::string("mech"));
    CHECK_EQ(last, std::string("VID_3434&PID_0281"));
    CHECK(!autoswitch::OnTyped(s, "VID_3434&PID_0281", last));  // same keyboard: nothing
    CHECK(autoswitch::OnTyped(s, "ACPI\\MSF0001", last));       // mech -> laptop
    CHECK_EQ(s.activeProfileId, std::string("laptop"));
    CHECK(autoswitch::OnTyped(s, "VID_3434&PID_0281", last));   // and back
    CHECK_EQ(s.activeProfileId, std::string("mech"));
    CHECK(s.switchStack.empty());

    // An unlinked keyboard keeps the profile but counts as the last one used.
    CHECK(!autoswitch::OnTyped(s, "VID_FFFF&PID_0001", last));
    CHECK_EQ(s.activeProfileId, std::string("mech"));
    CHECK_EQ(last, std::string("VID_FFFF&PID_0001"));
    CHECK(!autoswitch::OnTyped(s, "VID_3434&PID_0281", last));  // mech already active
    CHECK_EQ(last, std::string("VID_3434&PID_0281"));

    // Injected input has no device.
    CHECK(!autoswitch::OnTyped(s, "", last));
    CHECK_EQ(last, std::string("VID_3434&PID_0281"));
}

TEST(AutoSwitchOnTypingKeepsManualChoice) {
    Settings s = SwitchSettings();
    s.switchMode = SwitchMode::Typing;
    std::string last;
    autoswitch::OnTyped(s, "VID_3434&PID_0281", last);  // plain -> mech
    s.activeProfileId = "plain";                        // picked by hand
    autoswitch::OnManualActivation(s);
    CHECK(!autoswitch::OnTyped(s, "VID_3434&PID_0281", last));  // still typing on the K2
    CHECK_EQ(s.activeProfileId, std::string("plain"));
    CHECK(autoswitch::OnTyped(s, "VID_046D&PID_C52B", last));   // moved to the receiver
    CHECK_EQ(s.activeProfileId, std::string("travel"));
}

TEST(AutoSwitchOnTypingOnlyInTypingMode) {
    for (SwitchMode mode : {SwitchMode::Connect, SwitchMode::Manual}) {
        Settings s = SwitchSettings();
        s.switchMode = mode;
        std::string last;
        CHECK(!autoswitch::OnTyped(s, "VID_3434&PID_0281", last));
        CHECK_EQ(s.activeProfileId, std::string("plain"));
        CHECK(last.empty());
    }
}

TEST(AutoSwitchReconcileAtStartup) {
    // Built-in keyboard is always there; K2 connected while Keymapper was off.
    Settings s = SwitchSettings();
    CHECK(autoswitch::Reconcile(s, {"ACPI\\MSF0001", "VID_3434&PID_0281"}));
    CHECK_EQ(s.activeProfileId, std::string("mech"));  // later profile in the list wins
    CHECK(autoswitch::OnDisconnected(s, "VID_3434&PID_0281"));
    CHECK_EQ(s.activeProfileId, std::string("laptop"));

    // A remembered switch whose keyboard vanished while Keymapper was closed.
    Settings t = SwitchSettings();
    autoswitch::OnConnected(t, "VID_046D&PID_B35B");  // plain -> travel
    CHECK(autoswitch::Reconcile(t, {}));
    CHECK_EQ(t.activeProfileId, std::string("plain"));
    CHECK(t.switchStack.empty());
}

TEST(AutoSwitchSettingsRoundTrip) {
    Settings s = SwitchSettings();
    autoswitch::OnConnected(s, "VID_3434&PID_0281");
    Settings back;
    std::string err;
    for (SwitchMode mode : {SwitchMode::Connect, SwitchMode::Typing, SwitchMode::Manual}) {
        s.switchMode = mode;
        CHECK(DeserializeSettings(SerializeSettings(s), back, err));
        CHECK(back == s);
    }
    // Files written before this feature still load, with switching on connect.
    CHECK(DeserializeSettings(R"({"version":1,"profiles":[{"id":"a","name":"A"}]})", back, err));
    CHECK(back.switchMode == SwitchMode::Connect);
    CHECK(back.profiles[0].keyboards.empty());
    // Files from before switching modes: on/off only.
    CHECK(DeserializeSettings(R"({"version":1,"profiles":[{"id":"a","name":"A"}],"autoSwitch":{"enabled":true}})",
                              back, err));
    CHECK(back.switchMode == SwitchMode::Connect);
    CHECK(DeserializeSettings(R"({"version":1,"profiles":[{"id":"a","name":"A"}],"autoSwitch":{"enabled":false}})",
                              back, err));
    CHECK(back.switchMode == SwitchMode::Manual);
    // A mode from a newer version falls back to "enabled" instead of failing.
    CHECK(DeserializeSettings(
        R"({"version":1,"profiles":[{"id":"a","name":"A"}],"autoSwitch":{"enabled":false,"mode":"future"}})", back, err));
    CHECK(back.switchMode == SwitchMode::Manual);
    CHECK(!DeserializeSettings(R"({"version":1,"profiles":[{"id":"a","name":"A","keyboards":[{"id":""}]}]})", back, err));
}
