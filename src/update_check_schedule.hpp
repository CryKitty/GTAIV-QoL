#pragma once
#include <cstdint>
namespace ingameupdates {
inline constexpr std::uint64_t checkIntervalMs=10*60*1000;
struct Schedule {
    bool enabled=false,started=false;std::uint64_t attempted=0;
    bool due(bool on,std::uint64_t now,bool pending,bool busy,bool staged){
        if(!on){*this={};return false;}
        const bool first=!enabled;
        if(first){enabled=true;attempted=now;}
        if(pending||busy||staged)return false;
        if(started&&now-attempted<checkIntervalMs)return false;
        attempted=now;started=true;return true;
    }
};
}
