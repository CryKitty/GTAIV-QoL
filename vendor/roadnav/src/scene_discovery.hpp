#pragma once
#include <cstdint>
// A failed lookup is transient: mods/scene resources may still be initializing.
// Limit PE rescans on the render thread, and allow immediate retry after reset.
struct SceneDiscoveryRetry {
    bool attempted=false;std::uint32_t last=0;
    bool due(std::uint32_t now){if(attempted&&now-last<2000)return false;attempted=true;last=now;return true;}
    void reset(){*this={};}
};
