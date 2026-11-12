#pragma once
#include "../vendor/roadnav/src/marker_geometry.hpp"
#include <cstdint>

namespace roadnav {
// Extend only the first road tangent behind its old calculation origin. A
// driver can cover >30m during debounce; clamping at that origin would cancel
// the trigger before three seconds simply because the old GPS plan is stale.
inline Sample primaryRouteSample(const std::vector<Vec3>& path,Vec3 player){
    Sample result{};float best=std::numeric_limits<float>::max(),along=0;bool first=true;
    for(std::size_t i=1;i<path.size();++i){
        const auto a=path[i-1],b=path[i];const float dx=b.x-a.x,dy=b.y-a.y,length=std::hypot(dx,dy);
        if(length<.01f)continue;
        const float t=std::clamp(((player.x-a.x)*dx+(player.y-a.y)*dy)/(length*length),first?-300/length:0.0f,1.0f);
        const Vec3 point{a.x+dx*t,a.y+dy*t,a.z+(b.z-a.z)*t};
        const float error=distance2D(point,player)+4*std::fabs(point.z-player.z);
        if(error<best||(error<=best+.01f&&result.distanceAlong<0&&t>=0)){best=error;result={point,{dx/length,dy/length,0},along+t*length};}
        along+=length;first=false;
    }
    return result;
}
// Separate from Turn Guidance's existing wrong-way cue state. Body heading
// AND actual motion must oppose the road: backing along it is not wrong-way.
inline bool movingAgainstRoute(const std::vector<Vec3>& path,Vec3 player,Vec3 heading,Vec3 velocity){
    const float speed=std::hypot(velocity.x,velocity.y),h=std::hypot(heading.x,heading.y);
    if(path.size()<2||!std::isfinite(speed)||!std::isfinite(h)||speed<2.5f||h<.5f)return false;
    const auto nearest=primaryRouteSample(path,player);
    if(routeLength2D(path)-nearest.distanceAlong<15||distance2D(player,nearest.position)>18||std::fabs(player.z-nearest.position.z)>5)return false;
    return (heading.x*nearest.direction.x+heading.y*nearest.direction.y)/h<-.5f&&
        (velocity.x*nearest.direction.x+velocity.y*nearest.direction.y)/speed<-.5f;
}
struct PrimaryWrongWay {
    bool observing=false,attempted=false;std::uint32_t since=0,last=0,attemptAt=0;unsigned target=0;
    void cancel(){observing=false;target=0;}
    bool update(std::uint32_t now,unsigned goal,bool opposite){
        if(!goal||!opposite){cancel();return false;}
        if(!observing||target!=goal||now-last>600){observing=true;since=now;target=goal;}
        last=now;
        if(now-since<3000||(attempted&&now-attemptAt<10000))return false;
        attempted=true;attemptAt=now;cancel();return true;
    }
};
// Only a native road node well ahead on this level may become a lead-in.
// The native GPS still supplies all geometry; never draw a guessed straight link.
template<class Snap> bool forwardRoadTarget(Vec3 player,Vec3 heading,float speed,Snap snap,Vec3& target){
    const float length=std::hypot(heading.x,heading.y);
    if(!std::isfinite(length)||length<.5f||!std::isfinite(speed))return false;
    heading.x/=length;heading.y/=length;
    const float lead=std::clamp(speed*5,55.0f,180.0f);
    for(float distance:{lead,std::min(lead*.75f,100.0f),55.0f}){
        const Vec3 probe{player.x+heading.x*distance,player.y+heading.y*distance,player.z};
        for(unsigned n=1;n<=3;++n){
            Vec3 node{};if(!snap(probe,n,node)||!std::isfinite(node.x)||!std::isfinite(node.y)||!std::isfinite(node.z))continue;
            const float x=node.x-player.x,y=node.y-player.y,forward=x*heading.x+y*heading.y;
            const float lateral=std::fabs(x*heading.y-y*heading.x);
            if(forward<30||forward>250||lateral>18||std::fabs(node.z-player.z)>5)continue;
            target=node;return true;
        }
    }
    return false;
}
inline bool forwardGpsRoute(const std::vector<Vec3>& path,Vec3 player,Vec3 heading,Vec3 target){
    if(path.size()<2||distance2D(path.back(),target)>18||std::fabs(path.back().z-target.z)>5)return false;
    const float along=nearestDistanceAlong(path,player);const auto nearest=sampleAt(path,along),ahead=sampleAt(path,along+12);
    const float length=distance2D(nearest.position,ahead.position),h=std::hypot(heading.x,heading.y);
    return distance2D(nearest.position,player)<=15&&std::fabs(nearest.position.z-player.z)<=5&&length>6&&h>.5f&&
        ((ahead.position.x-nearest.position.x)*heading.x+(ahead.position.y-nearest.position.y)*heading.y)/(length*h)>.35f;
}
inline bool primaryOriginMatches(const std::vector<Vec3>& path,Vec3 player){
    return path.size()>1&&distance2D(path.front(),player)<=35&&std::fabs(path.front().z-player.z)<=5;
}
inline bool reachedForwardNode(Vec3 player,Vec3 target,Vec3 heading){
    const float x=player.x-target.x,y=player.y-target.y;
    return std::fabs(player.z-target.z)<6&&(distance2D(player,target)<14||
        (x*heading.x+y*heading.y>0&&std::fabs(x*heading.y-y*heading.x)<15));
}
}
