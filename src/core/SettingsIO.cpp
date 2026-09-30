#include "core/SettingsIO.h"

#include "core/Json.h"
#include "core/KeyCatalog.h"
#include "core/Utf.h"

#include <set>

namespace km {
namespace {

JValue EndpointToJson(const KeyCombo& c) {
    JValue o = JValue::Object();
    if (c.hasMods()) {
        JValue mods = JValue::Array();
        for (ModType t : kDisplayModOrder)
            if (c.mods[t] != Side::None) mods.push(JValue::String(KeyId(VkFromMod(t, c.mods[t]))));
        o.set("modifiers", std::move(mods));
    }
    o.set("key", JValue::String(KeyId(c.key)));
    return o;
}

JValue MappingsToJson(const std::vector<Mapping>& rows) {
    JValue a = JValue::Array();
    for (const Mapping& m : rows) {
        JValue o = JValue::Object();
        o.set("from", EndpointToJson(m.from));
        o.set("to", EndpointToJson(m.to));
        a.push(std::move(o));
    }
    return a;
}

bool EndpointFromJson(const JValue* v, KeyCombo& c, std::string& err) {
    c = KeyCombo{};
    if (!v || !v->isObject()) { err = "endpoint must be an object"; return false; }
    const JValue* key = v->find("key");
    if (!key || !key->isString() || !ParseKeyId(key->s, c.key)) {
        err = "endpoint has a missing or unknown key";
        return false;
    }
    if (const JValue* mods = v->find("modifiers")) {
        if (!mods->isArray()) { err = "modifiers must be an array"; return false; }
        for (const JValue& m : mods->arr) {
            uint16_t vk = 0;
            ModType t;
            Side s;
            if (!m.isString() || !ParseKeyId(m.s, vk) || !ModFromVk(vk, t, s)) {
                err = "unknown modifier";
                return false;
            }
            if (c.mods[t] != Side::None) { err = "modifier listed twice"; return false; }
            c.mods[t] = s;
        }
    }
    if (!c.isValidEndpoint()) {
        err = "endpoint is neither a key nor a shortcut (" + ComboToText(c) + ")";
        return false;
    }
    return true;
}

bool MappingsFromJson(const JValue* v, bool shortcutSection, std::vector<Mapping>& rows, std::string& err) {
    rows.clear();
    if (!v) return true;
    if (!v->isArray()) { err = "mapping list must be an array"; return false; }
    for (const JValue& item : v->arr) {
        if (!item.isObject()) { err = "mapping must be an object"; return false; }
        Mapping m;
        if (!EndpointFromJson(item.find("from"), m.from, err)) return false;
        if (!EndpointFromJson(item.find("to"), m.to, err)) return false;
        if (shortcutSection ? !m.from.isShortcut() : !m.from.isSingleKey()) {
            err = shortcutSection ? "shortcut mapping source is not a shortcut"
                                  : "key mapping source is not a single key";
            return false;
        }
        rows.push_back(m);
    }
    return true;
}

bool KeyboardsFromJson(const JValue* v, std::vector<KeyboardRef>& out) {
    out.clear();
    if (!v) return true;
    if (!v->isArray()) return false;
    for (const JValue& item : v->arr) {
        const JValue* id = item.find("id");
        const JValue* name = item.find("name");
        KeyboardRef k;
        if (!id || !id->isString() || id->s.empty() || !name || !name->isString() ||
            !TryUtf8ToWide(name->s, k.name))
            return false;
        k.id = id->s;
        out.push_back(std::move(k));
    }
    return true;
}

bool SwitchSettingsFromJson(const JValue& v, Settings& s) {
    if (const JValue* en = v.find("enabled")) {
        if (!en->isBool()) return false;
        s.switchMode = en->b ? SwitchMode::Connect : SwitchMode::Manual;
    }
    // Files from before switching modes only have "enabled". An unknown mode
    // (from a newer version) keeps what "enabled" says.
    if (const JValue* mode = v.find("mode"); mode && mode->isString()) {
        if (mode->s == "connect") s.switchMode = SwitchMode::Connect;
        else if (mode->s == "typing") s.switchMode = SwitchMode::Typing;
        else if (mode->s == "manual") s.switchMode = SwitchMode::Manual;
    }
    if (const JValue* stack = v.find("stack")) {
        if (!stack->isArray()) return false;
        for (const JValue& item : stack->arr) {
            const JValue* d = item.find("device");
            const JValue* p = item.find("profile");
            const JValue* prev = item.find("previous");
            if (!d || !d->isString() || !p || !p->isString() || !prev || !prev->isString()) return false;
            s.switchStack.push_back({d->s, p->s, prev->s});
        }
    }
    return true;
}

}  // namespace

std::string SerializeSettings(const Settings& s) {
    JValue root = JValue::Object();
    root.set("app", JValue::String("Keymapper"));
    root.set("version", JValue::Number(kSettingsVersion));
    root.set("enabled", JValue::Bool(s.enabled));
    root.set("activeProfileId", JValue::String(s.activeProfileId));
    JValue profiles = JValue::Array();
    for (const Profile& p : s.profiles) {
        JValue o = JValue::Object();
        o.set("id", JValue::String(p.id));
        o.set("name", JValue::String(WideToUtf8(p.name)));
        o.set("keyMappings", MappingsToJson(p.keys));
        o.set("shortcutMappings", MappingsToJson(p.shortcuts));
        if (!p.keyboards.empty()) {
            JValue kbs = JValue::Array();
            for (const KeyboardRef& k : p.keyboards) {
                JValue kb = JValue::Object();
                kb.set("id", JValue::String(k.id));
                kb.set("name", JValue::String(WideToUtf8(k.name)));
                kbs.push(std::move(kb));
            }
            o.set("keyboards", std::move(kbs));
        }
        profiles.push(std::move(o));
    }
    root.set("profiles", std::move(profiles));
    JValue autoSwitch = JValue::Object();
    // "enabled" lets versions without modes treat any automatic mode as Connect.
    autoSwitch.set("enabled", JValue::Bool(s.switchMode != SwitchMode::Manual));
    autoSwitch.set("mode", JValue::String(s.switchMode == SwitchMode::Typing   ? "typing"
                                          : s.switchMode == SwitchMode::Manual ? "manual"
                                                                               : "connect"));
    JValue stack = JValue::Array();
    for (const SwitchEntry& e : s.switchStack) {
        JValue o = JValue::Object();
        o.set("device", JValue::String(e.deviceId));
        o.set("profile", JValue::String(e.profileId));
        o.set("previous", JValue::String(e.previousProfileId));
        stack.push(std::move(o));
    }
    autoSwitch.set("stack", std::move(stack));
    root.set("autoSwitch", std::move(autoSwitch));
    return WriteJson(root);
}

bool DeserializeSettings(std::string_view text, Settings& out, std::string& error) {
    JValue root;
    if (!ParseJson(text, root, error)) return false;
    if (!root.isObject()) { error = "settings root must be an object"; return false; }

    const JValue* version = root.find("version");
    if (!version || !version->isNumber()) { error = "missing settings version"; return false; }
    if (version->n != kSettingsVersion) {
        error = "unsupported settings version " + std::to_string(static_cast<long long>(version->n));
        return false;
    }

    Settings s;
    s.enabled = true;
    if (const JValue* en = root.find("enabled")) {
        if (!en->isBool()) { error = "'enabled' must be true or false"; return false; }
        s.enabled = en->b;
    }

    const JValue* profiles = root.find("profiles");
    if (!profiles || !profiles->isArray() || profiles->arr.empty()) {
        error = "settings must contain at least one profile";
        return false;
    }
    std::set<std::string> ids;
    s.profiles.reserve(profiles->arr.size());
    for (const JValue& pv : profiles->arr) {
        if (!pv.isObject()) { error = "profile must be an object"; return false; }
        Profile p;
        const JValue* id = pv.find("id");
        const JValue* name = pv.find("name");
        if (!id || !id->isString() || id->s.empty()) { error = "profile has no id"; return false; }
        if (!ids.insert(id->s).second) { error = "duplicate profile id " + id->s; return false; }
        if (!name || !name->isString() || !TryUtf8ToWide(name->s, p.name)) {
            error = "profile " + id->s + " has an invalid name";
            return false;
        }
        p.id = id->s;
        std::string mappingError;
        if (!MappingsFromJson(pv.find("keyMappings"), false, p.keys, mappingError) ||
            !MappingsFromJson(pv.find("shortcutMappings"), true, p.shortcuts, mappingError)) {
            error = "profile " + p.id + ": " + mappingError;
            return false;
        }
        if (!KeyboardsFromJson(pv.find("keyboards"), p.keyboards)) {
            error = "profile " + p.id + ": invalid keyboard list";
            return false;
        }
        s.profiles.push_back(std::move(p));
    }

    const JValue* active = root.find("activeProfileId");
    if (active && active->isString() && s.find(active->s)) s.activeProfileId = active->s;
    else s.activeProfileId = s.profiles.front().id;

    if (const JValue* as = root.find("autoSwitch")) {
        if (!as->isObject() || !SwitchSettingsFromJson(*as, s)) {
            error = "invalid automatic switching settings";
            return false;
        }
    }

    out = std::move(s);
    return true;
}

}  // namespace km
