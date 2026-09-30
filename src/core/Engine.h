#pragma once

#include "core/Model.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace km {

// Side-specific modifier bits.
enum : uint8_t {
    MB_LCTRL = 0x01, MB_RCTRL = 0x02,
    MB_LALT = 0x04, MB_RALT = 0x08,
    MB_LSHIFT = 0x10, MB_RSHIFT = 0x20,
    MB_LWIN = 0x40, MB_RWIN = 0x80,
};

// Unassigned virtual key sent before an artificial Alt or Win release so the
// release does not open the Start menu or activate a window's menu bar.
constexpr uint16_t kMaskVk = 0xE8;

uint8_t ModBitForVk(uint16_t vk);   // Side-specific modifier keys only; else 0.
uint16_t VkForModBit(uint8_t bit);  // Exactly one bit set.

// Destination of a mapping. A single modifier destination is stored as a
// modifier with vk == 0.
struct Dest {
    std::array<Side, MT_Count> mods{};
    uint16_t vk = 0;
};

// Lookup tables built from a profile's valid rows.
struct CompiledProfile {
    struct ShortcutEntry {
        std::array<Side, MT_Count> mods{};
        int specificity = 0;  // Number of side-specific modifiers.
        Dest dest;
    };

    std::array<int16_t, 256> keyIndex;
    std::vector<Dest> keyDest;
    std::array<std::vector<ShortcutEntry>, 256> shortcuts;  // By action key.

    CompiledProfile() { keyIndex.fill(-1); }

    // Rows with validation issues are skipped.
    static std::shared_ptr<const CompiledProfile> Compile(const Profile& p);

    // Side-specific source first, then the generic modifier source.
    const Dest* findKey(uint16_t physicalVk) const;
    // Exact modifier match; side-specific entries win over generic ones.
    const Dest* findShortcut(uint16_t actionVk, uint8_t mods) const;
};

struct InEvent {
    uint16_t vk = 0;
    uint16_t scan = 0;
    bool down = true;
    bool extended = false;
};

struct OutEvent {
    uint16_t vk = 0;
    bool down = true;
    bool original = false;  // Re-injection of the incoming event: keep scan/extended.
    uint16_t scan = 0;
    bool extended = false;
};

struct Decision {
    bool suppress = false;      // Block the incoming event.
    bool capture = false;       // Forward the incoming event to the capture UI.
    std::vector<OutEvent> out;  // Events to inject, in order, after the decision.
};

// Pure mapping state machine. It sees physical key events (never its own
// injected output), decides whether each passes through, and produces the
// events to inject. It tracks physical keys and what Windows believes about
// modifier state separately, so that source modifiers can be temporarily
// lifted for a destination and restored afterwards.
//
// Injected events are delivered after the incoming event if it passes, so
// whenever output must come before the incoming key, the key is suppressed
// and re-injected as part of `out`.
class Engine {
public:
    // Profile changes take effect once no physical keys are held.
    void setProfile(std::shared_ptr<const CompiledProfile> p);
    bool hasPendingProfile() const { return pendingSet_; }
    const CompiledProfile* profile() const { return profile_.get(); }

    // Pausing releases every key the engine is holding down.
    void setEnabled(bool on, std::vector<OutEvent>& out);
    bool enabled() const { return enabled_; }

    // While capturing, key presses are swallowed and forwarded to the capture
    // UI. Remapping resumes with the previous enabled state afterwards.
    void setCapture(bool on, std::vector<OutEvent>& out);
    bool capturing() const { return capture_; }

    Decision process(const InEvent& ev);

    // Release every output key and injected modifier the engine owns. Keys
    // that are still physically held stay tracked, and their releases are
    // swallowed.
    void releaseAll(std::vector<OutEvent>& out);
    // releaseAll, then forget all state (e.g. after the session was locked).
    void reset(std::vector<OutEvent>& out);
    // Forget a key whose release was missed; see keysExpectedDown().
    void forget(uint16_t vk);
    // Keys that passed through and that Windows should therefore report as
    // down; used to detect missed releases.
    std::vector<uint16_t> keysExpectedDown() const;

    uint8_t osMods() const { return os_; }
    int heldCount() const { return heldCount_; }
    bool ownsOutput(uint16_t vk) const { return owned_[vk & 0xFF]; }

private:
    enum class Kind : uint8_t {
        None,           // Not held.
        Pass,           // Non-modifier passed through.
        PassMod,        // Physical modifier passed through.
        RemapMod,       // Key remapped to a modifier.
        RemapKey,       // Key remapped to a single key.
        KeyToShortcut,  // Key remapped to a shortcut.
        Shortcut,       // Action key of a matched shortcut.
        Swallow,        // Suppress until released.
    };
    struct Held {
        Kind kind = Kind::None;
        uint8_t contrib = 0;  // Modifier bits this key contributes.
        Dest dest;
    };

    Decision onDown(const InEvent& ev, Held& h);
    Decision onRepeat(const InEvent& ev, Held& h);
    Decision onUp(const InEvent& ev, Held& h);
    Decision processCapture(const InEvent& ev, Held& h);
    void modifierDown(const InEvent& ev, Held& h, bool passable, Decision& d);
    void modifierUp(const InEvent& ev, Held& h, Decision& d);
    void startOverride(uint8_t target, Held& h, Decision& d);
    void endOverride(uint16_t vk, Decision& d);

    uint8_t effectiveMods(uint16_t excludeVk = 0) const;
    void syncTo(uint8_t target, bool mask, std::vector<OutEvent>& out);
    void emit(uint16_t vk, bool down, std::vector<OutEvent>& out);
    void finishRelease(Held& h);
    void maybeApplyPending();

    std::array<Held, 256> held_{};
    std::array<bool, 256> owned_{};  // Non-modifier output keys we hold down.
    int heldCount_ = 0;
    int overrides_ = 0;              // Active shortcut / key-to-shortcut outputs.
    uint8_t os_ = 0;                 // Modifiers Windows believes are down.
    bool enabled_ = true;
    bool capture_ = false;
    std::shared_ptr<const CompiledProfile> profile_;
    std::shared_ptr<const CompiledProfile> pending_;
    bool pendingSet_ = false;
};

}  // namespace km
