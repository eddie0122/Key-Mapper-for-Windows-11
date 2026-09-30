#include "core/Engine.h"

#include "core/KeyCatalog.h"

#include <windows.h>

#include <set>

namespace km {
namespace {

constexpr uint8_t kLeftBit[MT_Count] = {MB_LCTRL, MB_LALT, MB_LSHIFT, MB_LWIN};
constexpr uint8_t kRightBit[MT_Count] = {MB_RCTRL, MB_RALT, MB_RSHIFT, MB_RWIN};
constexpr uint8_t kAltWinBits = MB_LALT | MB_RALT | MB_LWIN | MB_RWIN;

bool SideMatches(Side spec, uint8_t mods, int type) {
    const bool l = (mods & kLeftBit[type]) != 0;
    const bool r = (mods & kRightBit[type]) != 0;
    switch (spec) {
        case Side::None: return !l && !r;
        case Side::Either: return l || r;
        case Side::Left: return l && !r;
        case Side::Right: return r && !l;
    }
    return false;
}

bool HasMods(const std::array<Side, MT_Count>& mods) {
    for (Side s : mods)
        if (s != Side::None) return true;
    return false;
}

// Modifier bits a destination needs. A generic modifier is satisfied by
// whichever side `present` already has, and otherwise emits the left key.
uint8_t DestBits(const std::array<Side, MT_Count>& mods, uint8_t present) {
    uint8_t bits = 0;
    for (int t = 0; t < MT_Count; ++t) {
        const uint8_t both = kLeftBit[t] | kRightBit[t];
        switch (mods[t]) {
            case Side::None: break;
            case Side::Either: bits |= (present & both) ? (present & both) : kLeftBit[t]; break;
            case Side::Left: bits |= kLeftBit[t]; break;
            case Side::Right: bits |= kRightBit[t]; break;
        }
    }
    return bits;
}

Dest ToDest(const KeyCombo& to) {
    Dest d;
    ModType t;
    Side s;
    if (to.isSingleKey() && ModFromVk(to.key, t, s)) {
        d.mods[t] = s;
        d.vk = 0;
    } else {
        d.mods = to.mods;
        d.vk = to.key;
    }
    return d;
}

// Windows reports side-specific modifiers to low-level hooks, but other
// software can inject the generic codes.
uint16_t NormalizeVk(const InEvent& ev) {
    switch (ev.vk) {
        case VK_CONTROL: return ev.extended ? VK_RCONTROL : VK_LCONTROL;
        case VK_MENU: return ev.extended ? VK_RMENU : VK_LMENU;
        case VK_SHIFT: return ev.scan == 0x36 ? VK_RSHIFT : VK_LSHIFT;
        default: return ev.vk;
    }
}

OutEvent Original(const InEvent& ev) {
    OutEvent o;
    o.vk = ev.vk;
    o.down = ev.down;
    o.original = true;
    o.scan = ev.scan;
    o.extended = ev.extended;
    return o;
}

}  // namespace

uint8_t ModBitForVk(uint16_t vk) {
    switch (vk) {
        case VK_LCONTROL: return MB_LCTRL;
        case VK_RCONTROL: return MB_RCTRL;
        case VK_LMENU: return MB_LALT;
        case VK_RMENU: return MB_RALT;
        case VK_LSHIFT: return MB_LSHIFT;
        case VK_RSHIFT: return MB_RSHIFT;
        case VK_LWIN: return MB_LWIN;
        case VK_RWIN: return MB_RWIN;
        default: return 0;
    }
}

uint16_t VkForModBit(uint8_t bit) {
    switch (bit) {
        case MB_LCTRL: return VK_LCONTROL;
        case MB_RCTRL: return VK_RCONTROL;
        case MB_LALT: return VK_LMENU;
        case MB_RALT: return VK_RMENU;
        case MB_LSHIFT: return VK_LSHIFT;
        case MB_RSHIFT: return VK_RSHIFT;
        case MB_LWIN: return VK_LWIN;
        case MB_RWIN: return VK_RWIN;
        default: return 0;
    }
}

// ---- CompiledProfile --------------------------------------------------------

std::shared_ptr<const CompiledProfile> CompiledProfile::Compile(const Profile& p) {
    auto cp = std::make_shared<CompiledProfile>();
    std::set<std::pair<int, size_t>> invalid;
    for (const Issue& i : ValidateProfile(p)) invalid.insert({static_cast<int>(i.section), i.row});

    for (size_t i = 0; i < p.keys.size(); ++i) {
        if (invalid.count({static_cast<int>(Section::Keys), i})) continue;
        const Mapping& m = p.keys[i];
        cp->keyIndex[m.from.key & 0xFF] = static_cast<int16_t>(cp->keyDest.size());
        cp->keyDest.push_back(ToDest(m.to));
    }
    for (size_t i = 0; i < p.shortcuts.size(); ++i) {
        if (invalid.count({static_cast<int>(Section::Shortcuts), i})) continue;
        const Mapping& m = p.shortcuts[i];
        ShortcutEntry e;
        e.mods = m.from.mods;
        for (Side s : e.mods)
            if (s == Side::Left || s == Side::Right) ++e.specificity;
        e.dest = ToDest(m.to);
        cp->shortcuts[m.from.key & 0xFF].push_back(e);
    }
    return cp;
}

const Dest* CompiledProfile::findKey(uint16_t vk) const {
    if (vk > 0xFF) return nullptr;
    if (keyIndex[vk] >= 0) return &keyDest[static_cast<size_t>(keyIndex[vk])];
    ModType t;
    Side s;
    if (ModFromVk(vk, t, s) && s != Side::Either) {
        const uint16_t generic = VkFromMod(t, Side::Either);
        if (keyIndex[generic] >= 0) return &keyDest[static_cast<size_t>(keyIndex[generic])];
    }
    return nullptr;
}

const Dest* CompiledProfile::findShortcut(uint16_t vk, uint8_t mods) const {
    if (vk > 0xFF) return nullptr;
    const ShortcutEntry* best = nullptr;
    for (const ShortcutEntry& e : shortcuts[vk]) {
        bool match = true;
        for (int t = 0; t < MT_Count && match; ++t) match = SideMatches(e.mods[t], mods, t);
        if (match && (!best || e.specificity > best->specificity)) best = &e;
    }
    return best ? &best->dest : nullptr;
}

// ---- Engine -----------------------------------------------------------------

void Engine::setProfile(std::shared_ptr<const CompiledProfile> p) {
    pending_ = std::move(p);
    pendingSet_ = true;
    maybeApplyPending();
}

void Engine::maybeApplyPending() {
    if (pendingSet_ && heldCount_ == 0) {
        profile_ = std::move(pending_);
        pending_.reset();
        pendingSet_ = false;
    }
}

void Engine::setEnabled(bool on, std::vector<OutEvent>& out) {
    if (!on && enabled_) releaseAll(out);
    enabled_ = on;
}

void Engine::setCapture(bool on, std::vector<OutEvent>& out) {
    if (on && !capture_) releaseAll(out);
    capture_ = on;
}

uint8_t Engine::effectiveMods(uint16_t excludeVk) const {
    uint8_t m = 0;
    for (size_t vk = 0; vk < held_.size(); ++vk)
        if (vk != excludeVk) m |= held_[vk].contrib;
    return m;
}

void Engine::syncTo(uint8_t target, bool mask, std::vector<OutEvent>& out) {
    const uint8_t release = os_ & ~target;
    const uint8_t press = target & ~os_;
    if (mask && (release & kAltWinBits)) {
        out.push_back({kMaskVk, true});
        out.push_back({kMaskVk, false});
    }
    for (int i = 0; i < 8; ++i)
        if (release & (1u << i)) out.push_back({VkForModBit(static_cast<uint8_t>(1u << i)), false});
    for (int i = 0; i < 8; ++i)
        if (press & (1u << i)) out.push_back({VkForModBit(static_cast<uint8_t>(1u << i)), true});
    os_ = target;
}

void Engine::emit(uint16_t vk, bool down, std::vector<OutEvent>& out) {
    out.push_back({vk, down});
    owned_[vk & 0xFF] = down;
}

void Engine::finishRelease(Held& h) {
    h = Held{};
    --heldCount_;
    maybeApplyPending();
}

Decision Engine::process(const InEvent& in) {
    InEvent ev = in;
    ev.vk = NormalizeVk(in);
    if (ev.vk == 0 || ev.vk > 0xFE) return {};
    Held& h = held_[ev.vk];
    if (capture_) return processCapture(ev, h);
    if (ev.down) {
        if (h.kind != Kind::None) return onRepeat(ev, h);
        ++heldCount_;
        return onDown(ev, h);
    }
    if (h.kind == Kind::None) {
        // Release of a key pressed before we started tracking it.
        os_ &= static_cast<uint8_t>(~ModBitForVk(ev.vk));
        return {};
    }
    Decision d = onUp(ev, h);
    finishRelease(h);
    return d;
}

Decision Engine::onDown(const InEvent& ev, Held& h) {
    Decision d;
    const uint8_t eff = effectiveMods(ev.vk);
    const uint8_t selfBit = ModBitForVk(ev.vk);
    const CompiledProfile* p = enabled_ ? profile_.get() : nullptr;

    if (p) {
        // Shortcuts are evaluated before ordinary remapping of the action key.
        if (!selfBit) {
            if (const Dest* sd = p->findShortcut(ev.vk, eff)) {
                h.kind = Kind::Shortcut;
                h.dest = *sd;
                startOverride(DestBits(sd->mods, 0), h, d);
                return d;
            }
        }
        if (const Dest* kd = p->findKey(ev.vk)) {
            h.dest = *kd;
            if (kd->vk == 0) {
                h.kind = Kind::RemapMod;
                h.contrib = DestBits(kd->mods, 0);
                modifierDown(ev, h, false, d);
            } else if (!HasMods(kd->mods)) {
                h.kind = Kind::RemapKey;
                d.suppress = true;
                if (overrides_ == 0) syncTo(eff, true, d.out);
                emit(kd->vk, true, d.out);
            } else {
                h.kind = Kind::KeyToShortcut;
                startOverride(eff | DestBits(kd->mods, eff), h, d);
            }
            return d;
        }
    }

    if (selfBit) {
        h.kind = Kind::PassMod;
        h.contrib = selfBit;
        modifierDown(ev, h, true, d);
        return d;
    }
    h.kind = Kind::Pass;
    if (p && overrides_ == 0 && os_ != eff) {
        // Modifiers were lifted for an earlier mapping; restore them first.
        d.suppress = true;
        syncTo(eff, true, d.out);
        d.out.push_back(Original(ev));
    }
    return d;
}

Decision Engine::onRepeat(const InEvent&, Held& h) {
    Decision d;
    switch (h.kind) {
        case Kind::Pass:
            break;
        case Kind::PassMod:
            if (overrides_ > 0 || !(os_ & h.contrib)) d.suppress = true;
            break;
        case Kind::RemapKey:
            d.suppress = true;
            emit(h.dest.vk, true, d.out);
            break;
        case Kind::KeyToShortcut:
        case Kind::Shortcut:
            d.suppress = true;
            if (h.dest.vk) emit(h.dest.vk, true, d.out);
            break;
        default:
            d.suppress = true;
            break;
    }
    return d;
}

Decision Engine::onUp(const InEvent& ev, Held& h) {
    Decision d;
    switch (h.kind) {
        case Kind::Pass:
            break;
        case Kind::PassMod:
        case Kind::RemapMod:
            modifierUp(ev, h, d);
            break;
        case Kind::RemapKey:
            d.suppress = true;
            emit(h.dest.vk, false, d.out);
            break;
        case Kind::KeyToShortcut:
        case Kind::Shortcut:
            d.suppress = true;
            if (h.dest.vk) emit(h.dest.vk, false, d.out);
            endOverride(ev.vk, d);
            break;
        default:
            d.suppress = true;
            break;
    }
    return d;
}

void Engine::modifierDown(const InEvent&, Held& h, bool passable, Decision& d) {
    if (overrides_ > 0) {
        // A destination is being held; keep its modifier state intact.
        d.suppress = true;
        return;
    }
    const uint8_t target = os_ | h.contrib;
    if (target == os_) {
        d.suppress = true;
        return;
    }
    if (passable) {
        os_ = target;
        return;
    }
    d.suppress = true;
    syncTo(target, false, d.out);
}

void Engine::modifierUp(const InEvent& ev, Held& h, Decision& d) {
    if (overrides_ > 0) {
        d.suppress = true;
        return;
    }
    const uint8_t target = os_ & effectiveMods(ev.vk);
    if (target == os_) {
        d.suppress = true;
        return;
    }
    if (h.kind == Kind::PassMod && static_cast<uint8_t>(os_ ^ target) == h.contrib) {
        os_ = target;
        return;
    }
    d.suppress = true;
    syncTo(target, false, d.out);
}

void Engine::startOverride(uint8_t target, Held& h, Decision& d) {
    d.suppress = true;
    ++overrides_;
    syncTo(target, true, d.out);
    if (h.dest.vk) emit(h.dest.vk, true, d.out);
}

void Engine::endOverride(uint16_t vk, Decision& d) {
    if (overrides_ > 0) --overrides_;
    // Release destination modifiers that are not physically held. Source
    // modifiers that were lifted stay up until another key needs them.
    if (overrides_ == 0) syncTo(os_ & effectiveMods(vk), false, d.out);
}

Decision Engine::processCapture(const InEvent& ev, Held& h) {
    Decision d;
    d.capture = true;
    if (ev.down) {
        if (h.kind == Kind::None) {
            ++heldCount_;
            h.kind = Kind::Swallow;
        }
        d.suppress = true;
        return d;
    }
    if (h.kind == Kind::None) {
        os_ &= static_cast<uint8_t>(~ModBitForVk(ev.vk));
        return d;
    }
    if (h.kind == Kind::PassMod) {
        if (os_ & h.contrib) os_ &= static_cast<uint8_t>(~h.contrib);
        else d.suppress = true;
    } else if (h.kind != Kind::Pass) {
        d.suppress = true;
    }
    finishRelease(h);
    return d;
}

void Engine::releaseAll(std::vector<OutEvent>& out) {
    for (Held& h : held_) {
        switch (h.kind) {
            case Kind::RemapKey:
            case Kind::KeyToShortcut:
            case Kind::Shortcut:
                if (h.dest.vk && owned_[h.dest.vk]) emit(h.dest.vk, false, out);
                h.kind = Kind::Swallow;
                h.contrib = 0;
                break;
            case Kind::RemapMod:
                h.kind = Kind::Swallow;
                h.contrib = 0;
                break;
            default:
                break;
        }
    }
    for (size_t vk = 0; vk < owned_.size(); ++vk)
        if (owned_[vk]) emit(static_cast<uint16_t>(vk), false, out);
    overrides_ = 0;
    // Only physically held, passed-through modifiers remain.
    syncTo(os_ & effectiveMods(), true, out);
}

void Engine::reset(std::vector<OutEvent>& out) {
    releaseAll(out);
    syncTo(0, true, out);
    held_.fill(Held{});
    heldCount_ = 0;
    maybeApplyPending();
}

void Engine::forget(uint16_t vk) {
    Held& h = held_[vk & 0xFF];
    if (h.kind != Kind::Pass && h.kind != Kind::PassMod) return;
    if (h.kind == Kind::PassMod) os_ &= static_cast<uint8_t>(~h.contrib);
    finishRelease(h);
}

std::vector<uint16_t> Engine::keysExpectedDown() const {
    std::vector<uint16_t> keys;
    for (size_t vk = 0; vk < held_.size(); ++vk) {
        const Held& h = held_[vk];
        if (h.kind == Kind::Pass || (h.kind == Kind::PassMod && (os_ & h.contrib)))
            keys.push_back(static_cast<uint16_t>(vk));
    }
    return keys;
}

}  // namespace km
