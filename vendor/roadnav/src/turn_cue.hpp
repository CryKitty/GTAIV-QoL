#pragma once
#include "marker_geometry.hpp"
#include <array>

namespace roadnav {
enum class CueKind {Turn,UTurn};
struct TurnCue {
    bool visible=false;
    Vec3 position{}, outgoing{};
    float angleDegrees=0, distanceAhead=0;
    CueKind kind=CueKind::Turn;
    std::array<Sample,4> markers{};
    bool followsRoute=false;
    bool fork=false;
    Vec3 approach{};
};
struct DestinationCue {bool visible=false;Vec3 position{};float distanceAhead=0;};
struct NavigationCues {
    std::array<TurnCue,2> turns{};
    unsigned count=0;
    float lookAhead=70;
    DestinationCue destination{};
    bool visible()const{return count>0 || destination.visible;}
};
// Ten feet of clear space between different cue clusters, plus glyph extents.
inline bool separatedTurnClusters(const TurnCue& a,const TurnCue& b){
    constexpr float clearance=3.048f+1.32f;
    for(const auto& x:a.markers)for(const auto& y:b.markers)if(distance2D(x.position,y.position)<clearance)return false;
    return true;
}
// Prefer the later maneuver when adjacent clusters compete for the same space.
inline void preferLaterTurn(NavigationCues& cues){
    if(cues.count==2&&!separatedTurnClusters(cues.turns[0],cues.turns[1])){
        cues.turns[0]=cues.turns[1];cues.turns[1]={};cues.count=1;
    }
}
// Reaction time plus comfortable braking distance; bounded to keep distant
// city routes readable. Speed is metres/second, distances are metres.
inline float turnLookAhead(float speed){
    const float v=std::isfinite(speed)?std::clamp(speed,0.0f,80.0f):0.0f;
    return std::clamp(20.0f+2.0f*v+v*v/8.0f,70.0f,320.0f);
}
// A shallow fork needs a distinct straight-ahead road node, aligned with the
// incoming road. Mere bends/elevation changes are insufficient evidence.
inline bool forkContinuation(Vec3 before,Vec3 junction,Vec3 branch,Vec3 node,float heading){
    const float length=distance2D(before,junction);if(length<2)return false;
    const Vec3 in{(junction.x-before.x)/length,(junction.y-before.y)/length,0};
    const Vec3 delta{node.x-junction.x,node.y-junction.y,0};
    const float forward=delta.x*in.x+delta.y*in.y;
    const float across=std::fabs(delta.x*in.y-delta.y*in.x);
    constexpr float radians=.01745329252f;
    const float alignment=std::fabs(in.x*(-std::sin(heading*radians))+in.y*std::cos(heading*radians));
    return forward>=12&&forward<=140&&across<=8&&std::fabs(node.z-junction.z)<=3&&distance2D(node,branch)>=7&&alignment>.985f;
}
// A nearby perpendicular road arm is an ordinary intersection, not a Y fork.
inline bool crossStreetArm(Vec3 before,Vec3 junction,Vec3 node,float heading){
    const float length=distance2D(before,junction);if(length<2)return false;
    const Vec3 in{(junction.x-before.x)/length,(junction.y-before.y)/length,0};
    const Vec3 delta{node.x-junction.x,node.y-junction.y,0};
    const float forward=std::fabs(delta.x*in.x+delta.y*in.y),across=std::fabs(delta.x*in.y-delta.y*in.x);
    const float radians=heading*.01745329252f;
    const float alignment=std::fabs(in.x*(-std::sin(radians))+in.y*std::cos(radians));
    return forward<=14&&across>=8&&across<=38&&std::fabs(node.z-junction.z)<=3&&alignment<.5f;
}
inline void pruneForkCues(std::vector<TurnCue>& cues){
    std::vector<TurnCue> accepted;
    for(const auto& cue:cues){
        if(cue.fork){
            bool shadowed=false;
            for(const auto& other:cues)if(!other.fork&&std::fabs(other.distanceAhead-cue.distanceAhead)<45)shadowed=true;
            for(const auto& other:accepted)if(other.fork&&cue.distanceAhead-other.distanceAhead<80)shadowed=true;
            if(shadowed)continue;
        }
        accepted.push_back(cue);
    }
    cues=std::move(accepted);
}
// A ramp separates into another road corridor; a toll plaza fans out and
// rejoins. Follow the selected path well beyond the first shallow bend before
// accepting a nearby straight-road node as evidence of a real fork.
inline bool persistentFork(const std::vector<Vec3>& points,float along,Vec3 before){
    const float end=routeLength2D(points),available=end-along;
    const auto junction=sampleAt(points,along).position;
    const float length=distance2D(before,junction);if(length<2||available<55)return false;
    const Vec3 in{(junction.x-before.x)/length,(junction.y-before.y)/length,0};
    const auto early=sampleAt(points,along+std::min(40.0f,available)).position;
    const float earlyAcross=(early.x-junction.x)*in.y-(early.y-junction.y)*in.x;
    if(std::fabs(earlyAcross)<2)return false; // A later intersection is not a fork here.
    float side=0;
    for(const float horizon:{75.0f,140.0f,200.0f}){
        const float distance=std::min(available,horizon);
        const auto point=sampleAt(points,along+distance).position;
        const Vec3 delta{point.x-junction.x,point.y-junction.y,point.z-junction.z};
        const float across=delta.x*in.y-delta.y*in.x;
        const bool elevated=std::fabs(delta.z)>=4;
        if(horizon==75){
            if(std::fabs(across)<10||earlyAcross*across<=0)return false;
            side=across;
        }else if(!elevated&&(std::fabs(across)<10||side*across<=0))return false;
        if(available<=horizon)break;
    }
    return true;
}
// Compare tangents over a 24m window. Nearby same-direction detections belong
// to one rounded corner; opposite-direction maneuvers remain separate.
inline NavigationCues routeCues(const std::vector<Vec3>& points,Vec3 player,float speed=0,const std::vector<Vec3>& forks={}){
    NavigationCues cues;cues.lookAhead=turnLookAhead(speed);
    if(points.size()<2)return cues;
    const float anchor=nearestDistanceAlong(points,player);
    const float end=routeLength2D(points);
    const float remaining=std::max(0.0f,end-anchor);
    if(remaining<=70 && distance2D(player,points.back())<=85)
        cues.destination={true,points.back(),remaining};
    std::vector<TurnCue> candidates;
    float along=0;
    for(std::size_t i=1;i+1<points.size();++i){
        along+=distance2D(points[i-1],points[i]);
        const float ahead=along-anchor;
        if(ahead < -3.0f)continue;
        if(ahead > cues.lookAhead+55.0f)break;
        const auto before=sampleAt(points,std::max(0.0f,along-12.0f)).position;
        const auto after=sampleAt(points,std::min(end,along+12.0f)).position;
        Vec3 in{points[i].x-before.x,points[i].y-before.y,0};
        Vec3 out{after.x-points[i].x,after.y-points[i].y,0};
        const float a=std::hypot(in.x,in.y),b=std::hypot(out.x,out.y);
        if(a<2.0f || b<2.0f)continue;
        const float angle=std::atan2(in.x*out.y-in.y*out.x,in.x*out.x+in.y*out.y)*57.2957795f;
        bool fork=false;for(const auto& hint:forks)if(distance2D(hint,points[i])<1&&std::fabs(hint.z-points[i].z)<1){fork=true;break;}
        if((fork?std::fabs(angle)<.75f:std::fabs(angle)<40.0f) || distance2D(player,points[i])>cues.lookAhead+70.0f)continue;
        TurnCue cue{true,points[i],{out.x/b,out.y/b,0},angle,ahead};cue.fork=fork;cue.approach={in.x/a,in.y/a,0};
        if(!candidates.empty() && ahead-candidates.back().distanceAhead<24.0f &&
           angle*candidates.back().angleDegrees>0 &&
           cue.outgoing.x*candidates.back().outgoing.x+cue.outgoing.y*candidates.back().outgoing.y>.5f){
            if(!candidates.back().fork&&(fork||std::fabs(angle)>std::fabs(candidates.back().angleDegrees)))candidates.back()=cue;
        }else candidates.push_back(cue);
    }
    pruneForkCues(candidates);
    for(auto& cue:candidates){
        cue.followsRoute=true;
        for(unsigned marker=0;marker<4;++marker)cue.markers[marker]=sampleAt(points,std::min(end,anchor+cue.distanceAhead+(cue.fork?8.0f:1.5f)+2.3f*static_cast<float>(marker)));
        if(cue.fork){
            // A single level bank across the ramp mouth, not a rising trail
            // receding along the ramp which projects as vertically stacked glyphs.
            const auto center=sampleAt(points,std::min(end,anchor+cue.distanceAhead+16.0f)).position;
            const float side=cue.angleDegrees<0?1.0f:-1.0f;
            const Vec3 across{side*cue.approach.y,-side*cue.approach.x,0};
            for(unsigned marker=0;marker<4;++marker){const float offset=1.8f*(static_cast<float>(marker)-1.5f);
                cue.markers[marker]={{center.x+across.x*offset,center.y+across.y*offset,center.z},across};}
        }
    }
    // Resolve before the linked-turn distance gate: a very close second turn
    // used to be rejected while the first cluster's last glyph crossed into it.
    while(candidates.size()>1&&!separatedTurnClusters(candidates[0],candidates[1]))candidates.erase(candidates.begin());
    if(!candidates.empty() && candidates.front().distanceAhead<=cues.lookAhead+20.0f &&
       distance2D(player,candidates.front().position)<=cues.lookAhead+35.0f){
        cues.turns[cues.count++]=candidates.front();
        if(candidates.size()>1){
            const auto& first=candidates[0];const auto& second=candidates[1];
            const float gap=second.distanceAhead-first.distanceAhead;
            const float headingDot=first.outgoing.x*second.outgoing.x+first.outgoing.y*second.outgoing.y;
            // A second cue needs a distinct maneuver, not another sample of the same road bend.
            if(!first.fork&&!second.fork&&gap>=12.0f && gap<=35.0f && (headingDot<.5f ||
               (first.angleDegrees*second.angleDegrees<0 && std::fabs(first.angleDegrees)>=35 && std::fabs(second.angleDegrees)>=35)))
                cues.turns[cues.count++]=second;
        }
    }
    preferLaterTurn(cues);
    return cues;
}
// Wrong-way is based on route tangent AND actual forward movement, not camera yaw.
// Latching keeps the instruction visible when the driver stops to turn around.
struct WrongWayState {
    unsigned accumulated=0;bool active=false;
    bool update(const std::vector<Vec3>& points,Vec3 player,Vec3 facing,Vec3 velocity,unsigned elapsed){
        if(points.size()<2){*this={};return false;}
        const auto nearest=sampleAt(points,nearestDistanceAlong(points,player));
        const float horizontal=std::hypot(facing.x,facing.y),speed=std::hypot(velocity.x,velocity.y);
        if(horizontal<0.01f||distance2D(player,nearest.position)>9.0f||std::fabs(player.z-nearest.position.z)>3.0f){*this={};return false;}
        facing.x/=horizontal;facing.y/=horizontal;
        const float alignment=facing.x*nearest.direction.x+facing.y*nearest.direction.y;
        if(alignment>-.35f){*this={};return false;}
        const float forwardSpeed=velocity.x*facing.x+velocity.y*facing.y;
        if(!active){
            if(alignment<-.75f&&speed>=2.0f&&forwardSpeed>=1.5f){accumulated+=std::min(elapsed,300u);active=accumulated>=1200u;}
            else accumulated=0;
        }
        return active;
    }
};
inline NavigationCues uTurnCues(Vec3 player,Vec3 facing){
    NavigationCues result;const float length=std::hypot(facing.x,facing.y);if(length<0.01f)return result;
    auto& cue=result.turns[0];cue.visible=true;cue.kind=CueKind::UTurn;
    cue.position={player.x+facing.x/length*12.0f,player.y+facing.y/length*12.0f,player.z};
    cue.outgoing={-facing.x/length,-facing.y/length,0};cue.angleDegrees=180;cue.distanceAhead=12;result.count=1;return result;
}
inline TurnCue nextTurn(const std::vector<Vec3>& points,Vec3 player){
    const auto cues=routeCues(points,player);
    return cues.count?cues.turns[0]:TurnCue{};
}
struct CameraFrame {Vec3 position{},rotation{};float fov=45;};
struct ScreenPoint {float x=0,y=0;};
inline bool project(Vec3 point,const CameraFrame& cam,float width,float height,ScreenPoint& out){
    if(!std::isfinite(cam.fov)||cam.fov<10||cam.fov>140||width<=0||height<=0)return false;
    constexpr float radians=0.01745329252f;
    const float p=cam.rotation.x*radians,y=cam.rotation.z*radians,r=cam.rotation.y*radians;
    const Vec3 forward{-std::sin(y)*std::cos(p),std::cos(y)*std::cos(p),std::sin(p)};
    const Vec3 right0{std::cos(y),std::sin(y),0};
    const Vec3 up0{std::sin(y)*std::sin(p),-std::cos(y)*std::sin(p),std::cos(p)};
    const Vec3 right{right0.x*std::cos(r)+up0.x*std::sin(r),right0.y*std::cos(r)+up0.y*std::sin(r),up0.z*std::sin(r)};
    const Vec3 up{up0.x*std::cos(r)-right0.x*std::sin(r),up0.y*std::cos(r)-right0.y*std::sin(r),up0.z*std::cos(r)};
    const Vec3 d{point.x-cam.position.x,point.y-cam.position.y,point.z-cam.position.z};
    const float depth=d.x*forward.x+d.y*forward.y+d.z*forward.z;
    if(!std::isfinite(depth)||depth<=0.5f)return false;
    const float scale=height/(2*std::tan(cam.fov*radians/2)*depth);
    out={width/2+(d.x*right.x+d.y*right.y+d.z*right.z)*scale,
         height/2-(d.x*up.x+d.y*up.y+d.z*up.z)*scale};
    return std::isfinite(out.x)&&std::isfinite(out.y);
}
}
