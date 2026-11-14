#pragma once
#include "turn_cue.hpp"
namespace roadnav {
// Normal maneuvers always use the original directional chevrons. Camera yaw
// must not turn a road bend into an apparent go-straight instruction.
inline unsigned cueMarkerCount(const TurnCue& cue,const CameraFrame&){
    return cue.kind==CueKind::UTurn?6u:4u;
}
inline float approachOpacity(float distance,float speed){
    // Gone at fifteen feet; preserve a short visible fade at highway speeds.
    const float finish=4.572f,start=finish+std::max(3.5f,std::max(0.0f,speed)*.25f);
    const float t=std::clamp((distance-finish)/(start-finish),0.0f,1.0f);
    return t*t*(3.0f-2.0f*t);
}
// GPS/mission scripts commonly clear a destination around the 12 m arrival
// gate. Be fully transparent before that removal, with a visible approach ramp.
inline float destinationOpacity(float distance,float speed){
    const float finish=12.0f,start=finish+std::max(16.0f,std::max(0.0f,speed)*.65f);
    const float t=std::clamp((distance-finish)/(start-finish),0.0f,1.0f);
    return t*t*(3-2*t);
}
struct DestinationMarker {Vec3 position{};float opacity=0;};
// A native route can disappear before its coordinate marker is removed. Retire
// only the glyph at its last world position, never retain/fabricate a road tail.
struct DestinationMarkerFade {
    DestinationMarker marker{};unsigned lostAt=0;bool held=false,retiring=false;
    void clear(){*this={};}
    DestinationMarker update(bool visible,Vec3 position,Vec3 player,float speed,unsigned now){
        if(visible){marker={position,destinationOpacity(distance2D(player,position),speed)};held=true;retiring=false;return marker;}
        if(!held)return {};
        if(!retiring){lostAt=now;retiring=true;}
        const float t=std::clamp(static_cast<float>(now-lostAt)/300.0f,0.0f,1.0f);
        auto result=marker;
        result.opacity*=1-t*t*(3-2*t);
        // Approaching a retired glyph must never make it brighter again.
        result.opacity=std::min(result.opacity,destinationOpacity(distance2D(player,marker.position),speed));
        if(t>=1)clear();
        return result;
    }
};
// Retire only cues removed by GPS. Remaining/new maneuvers keep their slots.
inline NavigationCues departingCues(const NavigationCues& before,const NavigationCues& after){
    NavigationCues result{};result.lookAhead=before.lookAhead;
    for(unsigned i=0;i<before.count;++i){
        bool retained=false;
        for(unsigned j=0;j<after.count;++j)
            if(before.turns[i].kind==after.turns[j].kind&&distance2D(before.turns[i].position,after.turns[j].position)<3.0f)retained=true;
        if(!retained&&result.count<result.turns.size())result.turns[result.count++]=before.turns[i];
    }
    if(before.destination.visible&&(!after.destination.visible||distance2D(before.destination.position,after.destination.position)>=3.0f))result.destination=before.destination;
    return result;
}
inline float retirementOpacity(unsigned elapsed){return 1.0f-std::min(elapsed,200u)/200.0f;}
inline std::array<Vec3,6> cueVertices(const TurnCue& cue,unsigned marker,float scale,const CameraFrame& camera){
    constexpr float shape[6][2]={{-.60f,.65f},{-.05f,.65f},{.60f,0},{-.05f,-.65f},{-.60f,-.65f},{.05f,0}};
    const float yaw=camera.rotation.z*.01745329252f;
    const Vec3 right{std::cos(yaw),std::sin(yaw),0};
    std::array<Vec3,6> result{};
    if(cue.kind==CueKind::UTurn){
        // Six linked chevrons form an upright, camera-facing return bend.
        const float angle=3.14159265f*(1.0f-static_cast<float>(std::min(marker,4u))/4.0f);
        const float x=.9f*std::cos(angle),z=.9f*std::sin(angle)+(marker==5?-.6f:.15f);
        const float tx=std::sin(angle),tz=-std::cos(angle);
        for(unsigned v=0;v<6;++v){const float along=shape[v][0]*scale*.48f,across=shape[v][1]*scale*.48f;
            const float horizontal=x+along*tx-across*tz;
            result[v]={cue.position.x+right.x*horizontal,cue.position.y+right.y*horizontal,cue.position.z+2.0f+z+along*tz+across*tx};}
        return result;
    }
    const unsigned index=std::min(marker,3u);
    const Vec3 position=cue.followsRoute?cue.markers[index].position:Vec3{cue.position.x+cue.outgoing.x*(.5f+1.6f*static_cast<float>(index)),cue.position.y+cue.outgoing.y*(.5f+1.6f*static_cast<float>(index)),cue.position.z};
    Vec3 direction=cue.followsRoute?cue.markers[index].direction:cue.outgoing;
    if(cue.fork){
        const float length=std::hypot(cue.approach.x,cue.approach.y);
        if(length>.001f){
            // Face the driver on the approach road, not edge-on along the ramp.
            // Positive cross-product is a left fork, negative is a right fork.
            const float side=cue.angleDegrees<0?1.0f:-1.0f;
            direction={side*cue.approach.y/length,-side*cue.approach.x/length,0};
        }
    }
    for(unsigned v=0;v<6;++v){
        result[v]={position.x+direction.x*shape[v][0]*scale,position.y+direction.y*shape[v][0]*scale,position.z+1.45f+shape[v][1]*scale};
    }
    return result;
}
}
