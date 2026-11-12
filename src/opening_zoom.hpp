#pragma once
namespace openingzoom {
struct Ranges { float current, companion; };
inline bool IsDefault(const Ranges& value) { return value.current == 6540.0f && value.companion == 6540.0f; }
struct EntryGate {
    bool active = false, consumed = false, sawDefault = false;
    unsigned stable = 0;
    unsigned long entered = 0;
    bool Observe(unsigned char flag, const Ranges& ranges, unsigned long now) {
        if (flag != 1) { active = false; consumed = false; sawDefault = false; stable = 0; return false; }
        if (!active) { active = true; entered = now; consumed = false; sawDefault = false; stable = 0; }
        if (consumed) return false;
        if (now-entered > 1500) { consumed = true; return false; }
        if (!IsDefault(ranges)) {
            if (sawDefault) consumed = true; // respect manual/engine changes after baseline was seen
            stable = 0; return false;
        }
        sawDefault = true;
        return ++stable >= 2;
    }
    void Applied() { consumed = true; }
};
}
