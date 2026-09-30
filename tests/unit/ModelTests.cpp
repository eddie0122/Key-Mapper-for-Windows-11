#include "Test.h"

#include "core/Json.h"
#include "core/KeyCatalog.h"
#include "core/Model.h"
#include "core/SettingsIO.h"
#include "core/Utf.h"

#include <windows.h>

using namespace km;

namespace {

KeyCombo C(const char* text) {
    KeyCombo c;
    if (!ParseComboText(text, c)) kmtest::Fail(__FILE__, __LINE__, std::string("bad combo ") + text);
    return c;
}

std::wstring FirstIssue(const Profile& p) {
    auto issues = ValidateProfile(p);
    return issues.empty() ? L"" : issues.front().message;
}

Profile KeysProfile(std::initializer_list<Mapping> keys, std::initializer_list<Mapping> shortcuts = {}) {
    Profile p;
    p.id = "x";
    p.name = L"x";
    p.keys = keys;
    p.shortcuts = shortcuts;
    return p;
}

}  // namespace

TEST(ComboTextRoundTrip) {
    for (const char* t : {"CapsLock", "Ctrl", "RWin", "Ctrl+Shift+J", "LCtrl+RAlt+Delete", "Win+Tab",
                          "F13", "Hangul", "VK_E9"}) {
        KeyCombo c;
        CHECK(ParseComboText(t, c));
        KeyCombo again;
        CHECK(ParseComboText(ComboToText(c), again));
        CHECK(c == again);
    }
    KeyCombo c;
    CHECK(!ParseComboText("Ctrl+Shift", c));   // no action key
    CHECK(!ParseComboText("A+B", c));          // A is not a modifier
    CHECK(!ParseComboText("Ctrl+LCtrl+J", c)); // Ctrl listed twice
    CHECK(!ParseComboText("Nope", c));
}

TEST(FormatsForDisplay) {
    CHECK(FormatCombo(C("Shift+Ctrl+J")) == L"Ctrl + Shift + J");
    CHECK(FormatCombo(C("LWin+D")) == L"Win (Left) + D");
    CHECK(FormatCombo(C("CapsLock")) == L"Caps Lock");
    KeyCombo partial;
    partial.mods[MT_Ctrl] = Side::Either;
    CHECK(FormatCombo(partial) == L"Ctrl + \u2026");
}

TEST(EndpointShapes) {
    CHECK(C("A").isSingleKey());
    CHECK(C("Ctrl").isSingleKey());
    CHECK(C("Ctrl+A").isShortcut());
    KeyCombo modOnly;
    modOnly.mods[MT_Ctrl] = Side::Either;
    modOnly.key = VK_SHIFT;
    CHECK(!modOnly.isValidEndpoint());
}

TEST(ValidationMissingInputs) {
    Profile p = KeysProfile({{KeyCombo{}, C("A")}, {C("B"), KeyCombo{}}});
    auto issues = ValidateProfile(p);
    CHECK_EQ(issues.size(), size_t(2));
    CHECK(issues[0].missing && issues[1].missing);
    CHECK_EQ(issues[1].row, size_t(1));
}

TEST(ValidationDuplicatesAndIdentity) {
    CHECK(FirstIssue(KeysProfile({{C("A"), C("B")}, {C("A"), C("C")}})).find(L"Duplicate") == 0);
    // Generic and side-specific sources are different sources.
    CHECK(FirstIssue(KeysProfile({{C("Ctrl"), C("Alt")}, {C("LCtrl"), C("Win")}})).empty());
    CHECK(!FirstIssue(KeysProfile({{C("A"), C("A")}})).empty());
    CHECK(!FirstIssue(KeysProfile({{C("LCtrl"), C("Ctrl")}})).empty());  // emits Left Ctrl
    CHECK(FirstIssue(KeysProfile({{C("Ctrl"), C("LCtrl")}})).empty());   // Right Ctrl changes
    CHECK(!FirstIssue(KeysProfile({}, {{C("Ctrl+J"), C("Ctrl+J")}})).empty());
    CHECK(!FirstIssue(KeysProfile({}, {{C("LCtrl+J"), C("Ctrl+J")}})).empty());
    // Swaps are allowed.
    CHECK(ValidateProfile(KeysProfile({{C("A"), C("B")}, {C("B"), C("A")}})).empty());
}

TEST(ValidationShapeAndReserved) {
    CHECK(!FirstIssue(KeysProfile({{C("Ctrl+A"), C("B")}})).empty());           // shortcut in key section
    CHECK(!FirstIssue(KeysProfile({}, {{C("A"), C("B")}})).empty());            // key in shortcut section
    CHECK(!FirstIssue(KeysProfile({}, {{C("Ctrl+Alt+Delete"), C("B")}})).empty());
    CHECK(!FirstIssue(KeysProfile({{C("F1"), C("Win+L")}})).empty());
    CHECK(!FirstIssue(KeysProfile({}, {{C("LWin+L"), C("B")}})).empty());
    CHECK(FirstIssue(KeysProfile({}, {{C("Win+Shift+L"), C("B")}})).empty());
    // All four mapping forms are valid.
    CHECK(ValidateProfile(KeysProfile({{C("CapsLock"), C("Ctrl")}, {C("F1"), C("Ctrl+C")}},
                                      {{C("Ctrl+J"), C("Enter")}, {C("Ctrl+Shift+J"), C("Alt+Tab")}}))
              .empty());
}

TEST(JsonParsesAndEscapes) {
    JValue v;
    std::string err;
    CHECK(ParseJson(R"({"a":[1,2.5,-3e2,true,false,null],"s":"q\"\\\/\n\u00e9\ud83d\ude00"})", v, err));
    CHECK(v.find("a")->arr.size() == 6);
    CHECK(v.find("a")->arr[2].n == -300);
    CHECK(v.find("s")->s == "q\"\\/\n\xC3\xA9\xF0\x9F\x98\x80");
    JValue again;
    CHECK(ParseJson(WriteJson(v), again, err));
    CHECK(again.find("s")->s == v.find("s")->s);
    for (const char* bad : {"", "{", "[1,]", "{\"a\" 1}", "\"\\ud800\"", "tru", "{} x", "\"a\nb\""})
        CHECK(!ParseJson(bad, v, err));
}

TEST(SettingsRoundTripAllForms) {
    Settings s;
    Profile p;
    p.id = "p1";
    p.name = L"\uD55C\uAE00 \"quoted\" profile";
    p.keys = {{C("CapsLock"), C("Ctrl")}, {C("F1"), C("Ctrl+C")}, {C("RAlt"), C("Hangul")}};
    p.shortcuts = {{C("Ctrl+J"), C("Enter")}, {C("LCtrl+Shift+J"), C("Alt+Tab")}};
    Profile q;
    q.id = "p2";
    q.name = L"Second";
    s.profiles = {p, q};
    s.activeProfileId = "p2";
    s.enabled = false;
    std::string text = SerializeSettings(s);
    Settings back;
    std::string err;
    CHECK(DeserializeSettings(text, back, err));
    CHECK(back == s);
}

TEST(SettingsRejectsBadDocuments) {
    Settings out;
    std::string err;
    CHECK(!DeserializeSettings("not json", out, err));
    CHECK(!DeserializeSettings(R"({"version":2,"profiles":[{"id":"a","name":"A"}]})", out, err));
    CHECK(!DeserializeSettings(R"({"version":1,"profiles":[]})", out, err));
    CHECK(!DeserializeSettings(R"({"version":1,"profiles":[{"id":"a","name":"A"},{"id":"a","name":"B"}]})", out, err));
    CHECK(!DeserializeSettings(
        R"({"version":1,"profiles":[{"id":"a","name":"A","keyMappings":[{"from":{"key":"Bogus"},"to":{"key":"A"}}]}]})",
        out, err));
    // An unknown active profile falls back to the first profile.
    CHECK(DeserializeSettings(R"({"version":1,"activeProfileId":"zz","profiles":[{"id":"a","name":"A"}]})", out, err));
    CHECK(out.activeProfileId == "a");
    CHECK(out.enabled);
}

TEST(CatalogIsConsistent) {
    for (const KeyInfo& k : AllKeys()) {
        uint16_t vk = 0;
        CHECK(ParseKeyId(k.id, vk));
        CHECK_EQ(vk, k.vk);
        CHECK(FindKey(k.vk) == &k);
    }
    CHECK(IsModifierVk(VK_WIN_GENERIC));
    CHECK(!IsModifierVk(VK_CAPITAL));
    CHECK(!IsModifierVk(VK_TAB));
}

TEST(ProfileIdsAreUnique) {
    std::string a = NewProfileId(), b = NewProfileId();
    CHECK(a != b);
    CHECK(a.size() == 17);
}
