#pragma once
#include <algorithm>
#include <cstdint>
namespace wantedcompass {
struct Signals {bool known=false;unsigned stars=0;bool grey=false,flashing=false;};
struct Tint {unsigned r=0,g=0,b=0,a=0;};
struct Background {
    bool green=false,fading=false;std::uint32_t fadeAt=0;
    Tint update(Signals state,bool enabled,std::uint32_t now){
        if(!enabled||!state.known){*this={};return {};}
        if(state.stars){fading=false;
            if(!state.grey){green=false;return (now/400)%2?Tint{25,65,150,85}:Tint{150,30,35,85};}
            green=state.flashing;return green?Tint{30,115,55,80}:Tint{};
        }
        if(green&&!fading){fading=true;fadeAt=now;}
        if(fading){const auto elapsed=now-fadeAt;if(elapsed<1800)return {30,115,55,static_cast<unsigned>(80*(1-static_cast<float>(elapsed)/1800))};}
        *this={};return {};
    }
};
}
