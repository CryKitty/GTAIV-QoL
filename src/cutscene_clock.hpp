#pragma once
#include <cstdint>
namespace cutscenepause {
// Offset is measured from actual game-clock movement, not assumed wall time:
// clocks that truly stop must not receive a second pause correction.
struct Timeline {
    bool held=false;
    int anchor=-1,lastRaw=-1;
    std::int64_t offset=0;
    int Filter(int raw){
        if(raw<0){if(!held)Reset();return held?anchor:raw;}
        if(!held&&lastRaw>=0&&raw<lastRaw)offset=0; // new audio section
        lastRaw=raw;
        if(held)return anchor;
        const auto value=static_cast<std::int64_t>(raw)-offset;
        return value<0?0:static_cast<int>(value);
    }
    bool Begin(int raw){if(raw<0||held)return false;anchor=Filter(raw);held=true;return true;}
    void End(int raw){
        if(!held)return;
        // Preserve the same playback position if the game's bookkeeping clock
        // advanced while the captured XAudio2 engine was stopped.
        if(raw>=0&&lastRaw>=0){const auto target=static_cast<std::int64_t>(raw)-anchor;offset=target>0?target:0;lastRaw=raw;}
        held=false;
    }
    void Reset(){held=false;anchor=-1;lastRaw=-1;offset=0;}
};
}
