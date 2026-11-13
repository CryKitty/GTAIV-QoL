#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include "../vendor/roadnav/src/marker_geometry.hpp"
namespace compass {
inline std::string waypointMiles(float metres){
    if(!std::isfinite(metres)||metres<0)return {};
    const float miles=metres/1609.344f;char text[48]{};
    std::snprintf(text,sizeof(text),miles<10?"%.2f mi":"%.1f mi",static_cast<double>(miles));return text;
}
// Persistent waypoint rows reserve space below temporary hold/arrival captions.
inline float waypointLabelY(bool caption){return .087f+(caption?.048f:0);}
inline bool hoverReadout(unsigned handle,unsigned waypoint,bool focused,bool holding){return handle!=waypoint&&(focused||holding);}

// The same hold is available on foot and in vehicles; phone and combat retain
// their input priority. Steering, throttle and other D-pad directions are untouched.
inline bool waypointEligible(bool phone,bool combat){return !phone&&!combat;}
// A contact icon and the owned pin can occupy exactly the same place. Toggle
// by destination as well as handle; height prevents clearing an unrelated floor.
inline bool sameWaypointDestination(roadnav::Vec3 a,roadnav::Vec3 b,float radius=3){return roadnav::distance2D(a,b)<radius&&std::fabs(a.z-b.z)<8;}
inline bool waypointReached(roadnav::Vec3 player,roadnav::Vec3 destination){return sameWaypointDestination(player,destination,12);}
// One action per fresh hold. Losing focus/control or a long tick gap cancels
// until release; holding through a phone/menu transition cannot set a pin.
struct WaypointHold {
    static constexpr std::uint32_t duration=900;
    unsigned target=0;
    std::uint32_t started=0,lastTick=0;
    bool held=false,blocked=false,fired=false;
    unsigned update(bool down,bool eligible,unsigned focus,std::uint32_t now){
        if(!down){*this={};return 0;}
        if(held&&now-lastTick>250)blocked=true;
        lastTick=now;
        if(!eligible||!focus){held=true;blocked=true;target=0;return 0;}
        if(!held){held=true;target=focus;started=now;}
        if(blocked||fired)return 0;
        if(target!=focus){blocked=true;target=0;return 0;}
        if(now-started>=duration){fired=true;return target;}
        return 0;
    }
    float progress(std::uint32_t now)const{return target&&!blocked&&!fired?std::clamp(static_cast<float>(now-started)/duration,0.0f,1.0f):0;}
};
// Entering/exiting a vehicle must not complete a hold started in the other
// control context. This tracks context only; it owns no trigger/weapon state.
struct WaypointVehicleGate {
    bool known=false,inVehicle=false;
    bool update(bool vehicle){const bool changed=known&&vehicle!=inVehicle;known=true;inVehicle=vehicle;return changed;}
};
}
