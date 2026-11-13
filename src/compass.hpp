#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>
#include <unordered_map>
namespace compass {
// Explicit script declarations take precedence over a marker's display colour.
// Keys are complete game handles, including the pool generation.
struct Hostility {
    std::unordered_map<unsigned,bool> peds,blips;
    bool friendly(unsigned blip,unsigned ped) const {
        auto b=blips.find(blip);if(b!=blips.end())return b->second;
        auto p=peds.find(ped);return ped&&p!=peds.end()&&!p->second;
    }
    bool enemy(unsigned blip,unsigned ped,bool red) const {
        auto b=blips.find(blip);if(b!=blips.end())return !b->second;
        auto p=peds.find(ped);if(ped&&p!=peds.end())return p->second;
        return red;
    }
};
inline bool missionYellow(unsigned colour){
    if(colour<=255)return colour==5||colour==14;
    const unsigned r=(colour>>16)&255,g=(colour>>8)&255,b=colour&255;
    return r>160&&g>140&&b<120&&std::abs(static_cast<int>(r)-static_cast<int>(g))<100;
}
struct ChaseCandidate {std::uint32_t handle;float distance,speed;bool vehicle=false;};
inline bool chaseEnabled(bool legacy,bool roadLine,bool enemyLine){return legacy||(roadLine&&enemyLine);}
// Only the visible enemy-line request can preempt automatic police escape.
inline bool enemyOwnsAutomaticGps(bool fixedPrimary,bool roadLine,bool enemyLine,bool liveEnemy){
    return !fixedPrimary&&roadLine&&enemyLine&&liveEnemy;
}
inline bool allowChase(bool enabled,bool waypoint,bool ownsRoute,bool authoritativeRoute){
    return enabled&&!waypoint&&(ownsRoute||!authoritativeRoute);
}
inline std::uint32_t chooseChaseTarget(const std::vector<ChaseCandidate>& candidates,std::uint32_t previous){
    std::uint32_t best=0;float closest=3000;
    for(const auto& candidate:candidates){
        if(!candidate.handle||!std::isfinite(candidate.distance)||!std::isfinite(candidate.speed)||candidate.distance<0||candidate.distance>3000)continue;
        if(candidate.handle==previous)return previous;
        if((candidate.vehicle||candidate.speed>=2)&&candidate.distance<closest){best=candidate.handle;closest=candidate.distance;}
    }
    return best;
}
constexpr float pi=3.14159265359f;
inline float wrap(float degrees){
    if(!std::isfinite(degrees))return 0;
    float result=std::fmod(degrees+180.0f,360.0f);if(result<0)result+=360;return result-180;
}
// GTA camera yaw turns west from north. Screen bearings increase to the right.
inline float relativeBearing(float dx,float dy,float cameraYaw){return wrap(std::atan2(dx,dy)*180/pi+cameraYaw);}
inline float healthFraction(int health,int maximum){return std::clamp(static_cast<float>(health-100)/static_cast<float>(std::max(1,maximum-100)),0.0f,1.0f);}
inline float armourFraction(int armour,int maximum){return std::clamp(static_cast<float>(armour)/static_cast<float>(std::max(1,maximum)),0.0f,1.0f);}
inline bool radarDisplay(unsigned display){return display==2 || display==4;}
inline bool inRange(float distance,float range){return std::isfinite(distance)&&std::isfinite(range)&&range>=10&&distance>=0&&distance<=range;}
// Long-range mission/contact/waypoint markers remain visible at the edges,
// matching radar behaviour. Only short-range POIs use the local radar radius.
// BLIP_DATE (heart) is the shared romance/date sprite in the supported SDK.
inline bool dateMarker(unsigned sprite){return sprite==54;}
inline bool characterSprite(unsigned sprite){
    return dateMarker(sprite)||sprite==23||(sprite>=25&&sprite<=28)||
        (sprite>=31&&sprite<=45)||(sprite>=53&&sprite<=55)||sprite==63||sprite==64||
        sprite==69||sprite==72||sprite==74||sprite==76||sprite==80||sprite==92||sprite==93;
}
inline const char* fallbackSymbol(unsigned sprite){return dateMarker(sprite)?"<3":sprite==8?"X":sprite<=5?"*":".";}
inline bool markerInRange(float distance,float range,bool shortRange,unsigned sprite=0){
    return std::isfinite(distance)&&distance>=0&&(!shortRange||dateMarker(sprite)||inRange(distance,range));
}
// Use native metadata uniformly. Overlap priority never grants visibility.
// The deliberate heart exception alone ignores native short-range distance.
inline bool nativeRadarMarkerVisible(unsigned display,unsigned alpha,bool shortRange,float distance,float range,unsigned sprite){
    return radarDisplay(display)&&alpha>0&&markerInRange(distance,range,shortRange,sprite);
}
// Scripted custom colours are ARGB, unlike D3D's component argument order.
inline std::array<unsigned,3> markerColour(unsigned value){
    if(value>255)return {(value>>16)&255,(value>>8)&255,value&255};
    switch(value){
    case 1:return {170,45,40};case 2:return {65,125,60};case 3:return {95,180,225};
    case 4:return {145,145,145};case 5:return {230,198,95};case 6:return {225,135,55};
    case 7:return {185,110,220};case 8:return {110,190,105};case 9:return {225,65,55};
    case 10:return {235,135,135};case 11:return {240,185,120};case 12:return {45,125,130};
    case 13:return {95,190,185};case 14:return {240,225,150};default:return {230,232,224};
    }
}
// Native filters are sprites: GTA IV taxi::sub_33217 scans 0..109, then
// separately reads each returned blip's entity type. Include all supported
// compass sprites so late-created contact/date markers need no memory-table fallback.
constexpr auto blipCategories=[](){std::array<unsigned,128> values{};for(unsigned i=0;i<values.size();++i)values[i]=i;return values;}();
template<class Backend> std::vector<std::uint32_t> collectBlips(Backend& backend){
    std::vector<std::uint32_t> result;
    for(unsigned category:blipCategories){
        std::vector<std::uint32_t> seen;
        auto handle=backend.first(category);
        for(unsigned guard=0;guard<1500 && handle && backend.exists(handle);++guard){
            if(std::find(seen.begin(),seen.end(),handle)!=seen.end())break;
            seen.push_back(handle);
            if(std::find(result.begin(),result.end(),handle)==result.end()){
                result.push_back(handle);if(result.size()>=1500)return result;
            }
            handle=backend.next(category);
        }
    }
    return result;
}
// Ignore stale/recycled records and merge without duplicating native results.
template<class Backend> void appendRadarTable(Backend& backend,std::vector<std::uint32_t>& handles){
    for(unsigned slot=0;slot<1500;++slot){
        const auto handle=backend.handleAt(slot);
        if(!handle||(handle&0xffffu)!=slot||!backend.exists(handle))continue;
        if(std::find(handles.begin(),handles.end(),handle)==handles.end())handles.push_back(handle);
    }
}
// ScriptHook may tick repeatedly while the game's frame counter is unchanged.
// Native HUD commands must be enqueued at most once per rendered game frame.
struct FrameGate {
    std::uint32_t previous=0;bool haveFrame=false;
    bool accept(std::uint32_t frame){if(haveFrame&&frame==previous)return false;previous=frame;haveFrame=true;return true;}
};
struct StripLayout {
    static constexpr float left=.30f,right=.70f,center=.50f,half=.20f;
    static constexpr float lineY=.027f,tickY=.032f,labelY=.034f,iconY=.058f,focusLabelY=.087f,distanceY=.108f;
    static constexpr float barWidth=.198f,barHeight=.010f;
};
inline float raisedMeterY(unsigned screenHeight){return StripLayout::lineY-10.0f/static_cast<float>(screenHeight>=480&&screenHeight<=8640?screenHeight:800);}
inline float iconScale(bool enemy,bool target){return enemy||target?.85f:.90f;}
struct MeterSpan {float left=0,right=0;};
inline MeterSpan meterSpan(float fraction,bool health){
    const float width=StripLayout::barWidth*std::clamp(fraction,0.0f,1.0f);
    const float anchor=StripLayout::center+(health?-.002f:.002f);
    return health?MeterSpan{anchor-width,anchor}:MeterSpan{anchor,anchor+width};
}
// GET_BLIP_ALPHA writes through argument two. It does not return opacity.
// Keep the ABI contract in a tested adapter, rather than issuing a raw query.
constexpr std::uint32_t blipAlphaHash=0x297af6c8u;
template<class Invoker> unsigned readBlipAlpha(std::uint32_t blip){
    int alpha=255;
    Invoker::template invoke<blipAlphaHash,std::uint32_t>(blip,&alpha);
    return static_cast<unsigned>(std::clamp(alpha,0,255));
}
// Flash the full affected meter after damage. No delayed, shrinking loss trail.
struct DamageFlash {
    float value=0;std::uint32_t at=0;bool ready=false,damaged=false;
    void update(float fraction,std::uint32_t now){
        fraction=std::clamp(fraction,0.0f,1.0f);
        if(!ready){value=fraction;ready=true;return;}
        if(fraction<value){damaged=true;at=now;}
        else if(fraction>value)damaged=false; // Healing/replenishment is quiet.
        value=fraction;
    }
    unsigned alpha(std::uint32_t now)const{
        const auto elapsed=now-at;
        if(!damaged||elapsed>=1000||(elapsed/110)%2)return 0;
        return elapsed<800?240u:static_cast<unsigned>(240.0f*(1000-elapsed)/200.0f);
    }
};
// Restaurants and activities relevant to a date or friend outing, using the
// game's radar sprite IDs. Shops and unrelated mission contacts stay filtered.
inline bool socialVenue(unsigned sprite){switch(sprite){
    case 21:case 22:case 46:case 47:case 48:case 49:case 51:case 52:case 56:case 57:case 66:case 67:case 70:case 71:return true;
    default:return false;
}}
inline bool ambientMissionSprite(unsigned sprite){return (sprite>=21&&sprite<=52)||sprite==54||(sprite>=56&&sprite<=64)||sprite==66||sprite==67||sprite==69||sprite==70||sprite==71||sprite==74||sprite==75||sprite==79||(sprite>=83&&sprite<=92);}
inline bool hostileMarkerType(unsigned type){return type==1||type==2;} // Vehicle or person; location colour is not hostility.
inline bool enemyColour(unsigned colour){
    if(colour<=255)return colour==1||colour==9||colour==10;
    const unsigned r=(colour>>16)&255,g=(colour>>8)&255,b=colour&255;
    return r>=140&&r>g*1.5f&&r>b*1.5f;
}
struct Marker {std::uint32_t handle=0;unsigned sprite=0;float bearing=0,distance=0;unsigned colour=0,alpha=255;int priority=0;bool enemy=false;bool missionDestination=false;bool character=false;};
struct Placement {Marker marker;float x=0;bool rear=false;};
inline std::vector<Placement> layout(std::vector<Marker> markers){
    std::stable_sort(markers.begin(),markers.end(),[](const Marker& a,const Marker& b){
        // A person/date/contact must not be occluded by a nearer shop at
        // the same bearing (or by every rearward POI clamped to the strip edge).
        const bool aPerson=a.character||characterSprite(a.sprite),bPerson=b.character||characterSprite(b.sprite);
        if(aPerson!=bPerson)return aPerson;
        if(a.distance!=b.distance)return a.distance<b.distance;
        if(a.priority!=b.priority)return a.priority>b.priority;
        return std::fabs(a.bearing)<std::fabs(b.bearing);
    });
    std::vector<Placement> result;
    for(const auto& marker:markers){
        const float x=std::clamp(marker.bearing/90.0f,-1.0f,1.0f);
        bool occupied=false;for(const auto& placed:result)if(std::fabs(placed.x-x)<.075f){occupied=true;break;}
        if(!occupied)result.push_back({marker,x,std::fabs(marker.bearing)>90});
        if(result.size()>=24)break;
    }
    return result;
}
inline std::uint32_t focus(const std::vector<Placement>& markers,std::uint32_t previous){
    for(const auto& marker:markers)if(marker.marker.handle==previous&&std::fabs(marker.marker.bearing)<=5.0f)return previous;
    std::uint32_t best=0;float angle=3.5f;
    for(const auto& marker:markers)if(std::fabs(marker.marker.bearing)<=angle){angle=std::fabs(marker.marker.bearing);best=marker.marker.handle;}
    return best;
}
}
