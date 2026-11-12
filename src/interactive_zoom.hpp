#pragma once
#include <cmath>
#include <cstdint>
namespace interactivezoom {
inline unsigned SelectStop(float target) {
    const float stops[] = {6540,5263,3986,2709,1175,650};
    if (target == stops[0]) return 0;
    unsigned best = 1;
    for (unsigned i = 2; i < 5; ++i)
        if (std::fabs(stops[i]-target) < std::fabs(stops[best]-target)) best = i;
    return best;
}
struct Gate {
    bool active = false, handled = false;
    unsigned long entered = 0;
    bool Observe(bool pauseActive, std::uint32_t focus, unsigned long now) {
        if (!pauseActive || focus != 5) { active = false; handled = false; return false; }
        if (!active) { active = true; entered = now; handled = false; }
        if (handled || now-entered < 100) return false;
        handled = true; return true;
    }
};
}
