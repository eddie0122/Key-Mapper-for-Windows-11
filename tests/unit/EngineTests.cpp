// Behavioural tests for the mapping engine. A small simulator plays the role
// of Windows: it delivers passed-through events first, then injected ones,
// and records what applications would observe.

#include "Test.h"

#include "core/Engine.h"
#include "core/KeyCatalog.h"

#include <windows.h>

#include <array>
#include <utility>

using namespace km;

namespace {

uint16_t Vk(const char* id) {
    uint16_t vk = 0;
    if (!ParseKeyId(id, vk)) kmtest::Fail(__FILE__, __LINE__, std::string("unknown key id ") + id);
    return vk;
}

KeyCombo Combo(const char* text) {
    KeyCombo c;
    if (!ParseComboText(text, c)) kmtest::Fail(__FILE__, __LINE__, std::string("bad combo ") + text);
    return c;
}

using Rows = std::initializer_list<std::pair<const char*, const char*>>;

Profile MakeProfile(Rows keys, Rows shortcuts = {}) {
    Profile p;
    p.id = "test";
    p.name = L"Test";
    for (auto& [f, t] : keys) p.keys.push_back({Combo(f), Combo(t)});
    for (auto& [f, t] : shortcuts) p.shortcuts.push_back({Combo(f), Combo(t)});
    if (!ValidateProfile(p).empty()) kmtest::Fail(__FILE__, __LINE__, "test profile is invalid");
    return p;
}

struct Sim {
    Engine e;
    std::string log;
    std::array<bool, 256> os{};
    Decision last;

    explicit Sim(const Profile& p) { e.setProfile(CompiledProfile::Compile(p)); }

    void record(uint16_t vk, bool down) {
        if (!log.empty()) log += ' ';
        log += down ? '+' : '-';
        log += vk == kMaskVk ? std::string("Mask") : KeyId(vk);
        os[vk] = down;
    }
    void deliver(const std::vector<OutEvent>& out) {
        for (const OutEvent& o : out) record(o.vk, o.down);
    }
    void event(const char* id, bool down, bool extended = false) {
        InEvent ev;
        ev.vk = Vk(id);
        ev.down = down;
        ev.extended = extended;
        last = e.process(ev);
        if (!last.suppress) record(ev.vk, down);
        deliver(last.out);
    }
    void down(const char* id) { event(id, true); }
    void up(const char* id) { event(id, false); }
    void tap(const char* id) { down(id); up(id); }

    std::string take() { return std::exchange(log, std::string()); }
    bool clean() const {
        for (int vk = 0; vk < 256; ++vk)
            if (os[vk]) return false;
        return true;
    }
    void pause() { std::vector<OutEvent> out; e.setEnabled(false, out); deliver(out); }
    void resume() { std::vector<OutEvent> out; e.setEnabled(true, out); deliver(out); }
};

}  // namespace

TEST(KeyToKey) {
    Sim s(MakeProfile({{"A", "B"}}));
    s.down("A");
    CHECK_EQ(s.take(), "+B");
    s.up("A");
    CHECK_EQ(s.take(), "-B");
    s.tap("C");
    CHECK_EQ(s.take(), "+C -C");
    CHECK(s.clean());
}

TEST(SwapDoesNotRecurse) {
    Sim s(MakeProfile({{"A", "B"}, {"B", "A"}}));
    s.tap("A");
    CHECK_EQ(s.take(), "+B -B");
    s.tap("B");
    CHECK_EQ(s.take(), "+A -A");
    CHECK(s.clean());
}

TEST(RepeatBehaviour) {
    Sim s(MakeProfile({{"A", "B"}}, {{"Ctrl+J", "Enter"}}));
    s.down("A");
    s.down("A");
    s.down("A");
    s.up("A");
    CHECK_EQ(s.take(), "+B +B +B -B");
    s.down("C");
    s.down("C");
    s.up("C");
    CHECK_EQ(s.take(), "+C +C -C");
    s.down("LCtrl");
    s.down("J");
    s.down("J");
    s.up("J");
    s.up("LCtrl");
    CHECK_EQ(s.take(), "+LCtrl -LCtrl +Enter +Enter -Enter");
    CHECK(s.clean());
}

TEST(KeyToShortcut) {
    Sim s(MakeProfile({{"F1", "Ctrl+C"}}));
    s.down("F1");
    CHECK_EQ(s.take(), "+LCtrl +C");
    s.down("F1");
    CHECK_EQ(s.take(), "+C");
    s.up("F1");
    CHECK_EQ(s.take(), "-C -LCtrl");
    CHECK(s.clean());
}

TEST(KeyToShortcutKeepsOtherHeldModifiers) {
    Sim s(MakeProfile({{"F1", "Ctrl+C"}}));
    s.down("LShift");
    s.down("F1");
    s.up("F1");
    s.up("LShift");
    CHECK_EQ(s.take(), "+LShift +LCtrl +C -C -LCtrl -LShift");
    CHECK(s.clean());
}

TEST(ShortcutToKeyLiftsSourceModifiers) {
    Sim s(MakeProfile({}, {{"Ctrl+J", "Enter"}}));
    s.down("LCtrl");
    CHECK_EQ(s.take(), "+LCtrl");
    s.down("J");
    CHECK_EQ(s.take(), "-LCtrl +Enter");
    s.up("J");
    CHECK_EQ(s.take(), "-Enter");
    // Ctrl is still physically held: the next ordinary key gets it back.
    s.down("K");
    CHECK_EQ(s.take(), "+LCtrl +K");
    s.up("K");
    s.up("LCtrl");
    CHECK_EQ(s.take(), "-K -LCtrl");
    CHECK(s.clean());
}

TEST(ShortcutToShortcut) {
    Sim s(MakeProfile({}, {{"Ctrl+Shift+J", "Alt+Tab"}}));
    s.down("LCtrl");
    s.down("LShift");
    CHECK_EQ(s.take(), "+LCtrl +LShift");
    s.down("J");
    CHECK_EQ(s.take(), "-LCtrl -LShift +LAlt +Tab");
    s.up("J");
    CHECK_EQ(s.take(), "-Tab -LAlt");
    s.up("LShift");
    s.up("LCtrl");
    CHECK_EQ(s.take(), "");
    CHECK(s.clean());
}

TEST(ShortcutDestinationSharingSourceModifier) {
    Sim s(MakeProfile({}, {{"Ctrl+J", "Ctrl+K"}}));
    s.down("LCtrl");
    s.down("J");
    s.up("J");
    s.up("LCtrl");
    CHECK_EQ(s.take(), "+LCtrl +K -K -LCtrl");
    // Generic destination modifiers emit the left key.
    s.down("RCtrl");
    s.down("J");
    s.up("J");
    s.up("RCtrl");
    CHECK_EQ(s.take(), "+RCtrl -RCtrl +LCtrl +K -K -LCtrl");
    CHECK(s.clean());
}

TEST(ExactModifierMatching) {
    Sim s(MakeProfile({}, {{"Ctrl+J", "Enter"}}));
    s.down("LCtrl");
    s.down("LShift");
    s.tap("J");  // Ctrl+Shift+J is not Ctrl+J.
    s.up("LShift");
    s.up("LCtrl");
    CHECK_EQ(s.take(), "+LCtrl +LShift +J -J -LShift -LCtrl");
    s.tap("J");  // No modifiers: not a match either.
    CHECK_EQ(s.take(), "+J -J");
    CHECK(s.clean());
}

TEST(SideSpecificShortcutWins) {
    Sim s(MakeProfile({}, {{"Ctrl+J", "X"}, {"LCtrl+J", "Y"}}));
    s.down("LCtrl");
    s.tap("J");
    s.up("LCtrl");
    CHECK_EQ(s.take(), "+LCtrl -LCtrl +Y -Y");
    s.down("RCtrl");
    s.tap("J");
    s.up("RCtrl");
    CHECK_EQ(s.take(), "+RCtrl -RCtrl +X -X");
    // Both Ctrl keys: the left-only entry requires Right Ctrl to be up.
    s.down("LCtrl");
    s.down("RCtrl");
    s.tap("J");
    s.up("RCtrl");
    s.up("LCtrl");
    CHECK_EQ(s.take(), "+LCtrl +RCtrl -LCtrl -RCtrl +X -X");
    CHECK(s.clean());
}

TEST(SideSpecificKeyWins) {
    Sim s(MakeProfile({{"Ctrl", "Alt"}, {"RCtrl", "LWin"}}));
    s.tap("LCtrl");
    CHECK_EQ(s.take(), "+LAlt -LAlt");
    s.tap("RCtrl");
    CHECK_EQ(s.take(), "+LWin -LWin");
    CHECK(s.clean());
}

TEST(CapsLockToCtrlFeedsShortcuts) {
    Sim s(MakeProfile({{"CapsLock", "Ctrl"}}, {{"Ctrl+J", "Enter"}}));
    s.down("CapsLock");
    CHECK_EQ(s.take(), "+LCtrl");
    s.down("J");
    CHECK_EQ(s.take(), "-LCtrl +Enter");
    s.up("J");
    s.up("CapsLock");
    CHECK_EQ(s.take(), "-Enter");
    CHECK(s.clean());
    // Ordinary combination through the remapped modifier.
    s.down("CapsLock");
    s.tap("C");
    s.up("CapsLock");
    CHECK_EQ(s.take(), "+LCtrl +C -C -LCtrl");
    CHECK(s.clean());
}

TEST(RemappedAndPhysicalModifierTogether) {
    Sim s(MakeProfile({{"CapsLock", "Ctrl"}}));
    s.down("CapsLock");
    s.down("LCtrl");
    s.up("CapsLock");
    CHECK_EQ(s.take(), "+LCtrl");
    s.up("LCtrl");
    CHECK_EQ(s.take(), "-LCtrl");
    CHECK(s.clean());
}

TEST(TabIsOnlyAModifierWhenRemapped) {
    Sim plain(MakeProfile({}, {{"Shift+Tab", "X"}}));
    plain.down("LShift");
    plain.tap("Tab");
    plain.up("LShift");
    CHECK_EQ(plain.take(), "+LShift -LShift +X -X");
    plain.tap("Tab");
    CHECK_EQ(plain.take(), "+Tab -Tab");

    Sim mod(MakeProfile({{"Tab", "Ctrl"}}, {{"Ctrl+J", "Enter"}}));
    mod.down("Tab");
    mod.tap("J");
    mod.up("Tab");
    CHECK_EQ(mod.take(), "+LCtrl -LCtrl +Enter -Enter");
    CHECK(mod.clean());
}

TEST(ModifierToOrdinaryKey) {
    Sim s(MakeProfile({{"RAlt", "Menu"}}, {{"Alt+J", "X"}}));
    s.down("RAlt");
    CHECK_EQ(s.take(), "+Menu");
    s.tap("J");  // Right Alt no longer acts as Alt.
    s.up("RAlt");
    CHECK_EQ(s.take(), "+J -J -Menu");
    CHECK(s.clean());
}

TEST(AltReleaseIsMasked) {
    Sim s(MakeProfile({}, {{"Alt+J", "X"}}));
    s.down("LAlt");
    s.down("J");
    CHECK_EQ(s.take(), "+LAlt +Mask -Mask -LAlt +X");
    s.up("J");
    s.up("LAlt");
    CHECK_EQ(s.take(), "-X");
    CHECK(s.clean());
}

TEST(WinReleaseIsMaskedAndStartMenuNotTriggered) {
    Sim s(MakeProfile({}, {{"Win+J", "X"}}));
    s.down("LWin");
    s.down("J");
    s.up("J");
    s.up("LWin");  // Win up is swallowed: Windows already saw it released.
    CHECK_EQ(s.take(), "+LWin +Mask -Mask -LWin +X -X");
    CHECK(s.clean());
    s.tap("LWin");  // An unmapped tap still reaches Windows.
    CHECK_EQ(s.take(), "+LWin -LWin");
}

TEST(ReleaseModifierBeforeActionKey) {
    Sim s(MakeProfile({}, {{"Ctrl+J", "Enter"}}));
    s.down("LCtrl");
    s.down("J");
    s.up("LCtrl");
    s.up("J");
    CHECK_EQ(s.take(), "+LCtrl -LCtrl +Enter -Enter");
    CHECK(s.clean());
}

TEST(ProfileSwitchWaitsForRelease) {
    Sim s(MakeProfile({{"A", "B"}}));
    s.down("A");
    s.e.setProfile(CompiledProfile::Compile(MakeProfile({{"A", "C"}})));
    CHECK(s.e.hasPendingProfile());
    s.down("A");
    s.up("A");
    CHECK_EQ(s.take(), "+B +B -B");
    CHECK(!s.e.hasPendingProfile());
    s.tap("A");
    CHECK_EQ(s.take(), "+C -C");
    CHECK(s.clean());
}

TEST(PauseReleasesHeldOutput) {
    Sim s(MakeProfile({{"A", "B"}, {"CapsLock", "Ctrl"}}));
    s.down("A");
    s.down("CapsLock");
    CHECK_EQ(s.take(), "+B +LCtrl");
    s.pause();
    CHECK_EQ(s.take(), "-B -LCtrl");
    CHECK(s.clean());
    s.up("A");
    s.up("CapsLock");
    CHECK_EQ(s.take(), "");
    s.tap("A");
    CHECK_EQ(s.take(), "+A -A");
    s.resume();
    s.tap("A");
    CHECK_EQ(s.take(), "+B -B");
    CHECK(s.clean());
}

TEST(PauseKeepsPhysicalModifier) {
    Sim s(MakeProfile({}, {{"Ctrl+J", "Enter"}}));
    s.down("LCtrl");
    s.pause();
    s.tap("J");
    s.up("LCtrl");
    CHECK_EQ(s.take(), "+LCtrl +J -J -LCtrl");
    CHECK(s.clean());
}

TEST(PauseDuringShortcut) {
    Sim s(MakeProfile({}, {{"Win+J", "Alt+Tab"}}));
    s.down("LWin");
    s.down("J");
    CHECK_EQ(s.take(), "+LWin +Mask -Mask -LWin +LAlt +Tab");
    s.pause();
    CHECK_EQ(s.take(), "-Tab +Mask -Mask -LAlt");
    s.up("J");
    s.up("LWin");
    CHECK_EQ(s.take(), "");
    CHECK(s.clean());
}

TEST(ResetReleasesEverything) {
    Sim s(MakeProfile({{"A", "B"}, {"F1", "Ctrl+Shift+Esc"}, {"CapsLock", "Win"}}));
    s.down("A");
    s.down("F1");
    s.down("CapsLock");
    s.down("LAlt");
    std::vector<OutEvent> out;
    s.e.reset(out);
    s.deliver(out);
    CHECK(s.clean());
    CHECK_EQ(s.e.heldCount(), 0);
    // Releases arriving after the reset are harmless.
    s.up("A");
    s.up("F1");
    s.up("CapsLock");
    s.up("LAlt");
    CHECK(s.clean());
}

TEST(CaptureSwallowsKeysAndRestores) {
    Sim s(MakeProfile({{"A", "B"}}));
    s.down("LCtrl");
    std::vector<OutEvent> out;
    s.e.setCapture(true, out);
    s.deliver(out);
    s.down("J");
    CHECK(s.last.suppress && s.last.capture);
    s.up("J");
    CHECK(s.last.suppress);
    s.tap("Enter");
    s.tap("Esc");
    s.up("LCtrl");  // Was down before capture: Windows must see the release.
    CHECK_EQ(s.take(), "+LCtrl -LCtrl");
    s.e.setCapture(false, out);
    s.tap("A");
    CHECK_EQ(s.take(), "+B -B");
    CHECK(s.clean());
}

TEST(CaptureKeepsPausedState) {
    Sim s(MakeProfile({{"A", "B"}}));
    s.pause();
    std::vector<OutEvent> out;
    s.e.setCapture(true, out);
    s.e.setCapture(false, out);
    s.tap("A");
    CHECK_EQ(s.take(), "+A -A");
}

TEST(ForeignGenericModifierIsNormalized) {
    Sim s(MakeProfile({{"RCtrl", "Esc"}}));
    s.event("Ctrl", true, /*extended=*/true);
    s.event("Ctrl", false, true);
    CHECK_EQ(s.take(), "+Esc -Esc");
}

TEST(MissedReleaseCanBeForgotten) {
    Sim s(MakeProfile({{"A", "B"}}));
    s.down("LCtrl");
    s.down("K");
    auto keys = s.e.keysExpectedDown();
    CHECK_EQ(keys.size(), size_t(2));
    s.e.forget(VK_LCONTROL);
    s.e.forget('K');
    CHECK_EQ(s.e.heldCount(), 0);
    CHECK_EQ(int(s.e.osMods()), 0);
}

TEST(InvalidRowsAreNotCompiled) {
    Profile p;
    p.keys.push_back({Combo("A"), Combo("A")});  // identity
    p.keys.push_back({Combo("B"), KeyCombo{}});  // missing destination
    p.keys.push_back({Combo("C"), Combo("D")});
    auto cp = CompiledProfile::Compile(p);
    CHECK(cp->findKey('A') == nullptr);
    CHECK(cp->findKey('B') == nullptr);
    CHECK(cp->findKey('C') != nullptr);
}

TEST(RandomizedSequencesLeaveNoStuckKeys) {
    // Fuzz: random presses and releases over a rich profile must always end
    // with nothing held once every physical key is released.
    Sim s(MakeProfile({{"CapsLock", "Ctrl"}, {"A", "B"}, {"B", "A"}, {"F1", "Ctrl+C"}, {"RAlt", "Win"},
                       {"Tab", "Shift"}},
                      {{"Ctrl+J", "Enter"}, {"Ctrl+Shift+J", "Alt+Tab"}, {"Win+K", "Ctrl+Alt+T"},
                       {"LAlt+L", "Right"}, {"Shift+Tab", "Esc"}}));
    const char* keys[] = {"CapsLock", "A", "B", "F1", "RAlt", "Tab", "J", "K", "L", "LCtrl", "RCtrl",
                          "LShift", "RShift", "LAlt", "LWin", "C"};
    uint32_t rng = 12345;
    auto next = [&] { return rng = rng * 1664525u + 1013904223u; };
    bool held[16] = {};
    for (int round = 0; round < 20000; ++round) {
        int k = static_cast<int>((next() >> 8) % 16);
        bool press = ((next() >> 9) & 3) != 0 ? !held[k] : true;  // occasional repeats
        s.event(keys[k], press);
        held[k] = press;
        if (round % 97 == 0) (round % 2 ? s.pause() : s.resume());
        if (round % 500 == 499) {
            for (int i = 0; i < 16; ++i)
                if (held[i]) { s.up(keys[i]); held[i] = false; }
            s.resume();
            CHECK(s.clean());
            CHECK_EQ(s.e.heldCount(), 0);
            s.take();
        }
    }
}
