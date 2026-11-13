#pragma once
#include "../vendor/roadnav/src/marker_geometry.hpp"
#include <cmath>
#include <limits>
#include <cstdint>
namespace policeescape {
// Experimental fallback only; prefer the validated live radar radius table.
inline float estimatedRadius(unsigned stars){constexpr float values[]={0,150,250,400,550,700,850};return stars<=6?values[stars]:0;}
inline bool outside(roadnav::Vec3 point,roadnav::Vec3 center,float radius){return roadnav::distance2D(point,center)>=radius+60;}
struct Candidate {roadnav::Vec3 point;float score;};
// Failed requests must not disarm the chase. Use elapsed unsigned times for wrap.
struct ReplanState {
    bool observed=false,searching=false,pending=false,attempted=false,inSight=false;
    roadnav::Vec3 heading{};
    unsigned stars=0,failures=0;std::uint32_t attemptedAt=0;
    roadnav::Vec3 center{};float radius=0;
    const char* observe(unsigned nextStars,bool nextSearching,roadnav::Vec3 nextCenter,float nextRadius,bool nextInSight=false,roadnav::Vec3 travel={}){
        const char* reason=nullptr;
        if(!observed)reason="wanted started";
        else if(stars!=nextStars)reason="wanted level changed";
        else if(inSight!=nextInSight)reason=nextInSight?"police sighting reset":"police lost sight";
        else if(searching!=nextSearching)reason=nextSearching?"police lost player":"police re-found player";
        else if(roadnav::distance2D(center,nextCenter)>=25||std::fabs(radius-nextRadius)>=15)reason="search boundary changed";
        // New pursuit information re-arms the normal debounce; repeated failures
        // in the same search area retain exponential backoff.
        if(reason){observed=true;inSight=nextInSight;heading=travel;stars=nextStars;searching=nextSearching;center=nextCenter;radius=nextRadius;pending=true;failures=0;}
        return reason;
    }
    unsigned delay()const{return std::min(10000u,2500u*(1u<<std::min(failures,2u)));}
    bool ready(std::uint32_t now)const{return !attempted||now-attemptedAt>=delay();}
    void begin(std::uint32_t now){attempted=true;attemptedAt=now;pending=false;}
    void failed(std::uint32_t now){attempted=true;attemptedAt=now;failures=std::min(failures+1,2u);pending=true;}
    void committed(){failures=0;pending=false;}
};
inline float segmentRisk(roadnav::Vec3 a,roadnav::Vec3 b,const std::vector<roadnav::Vec3>& police){
    const float dx=b.x-a.x,dy=b.y-a.y,length2=dx*dx+dy*dy;float risk=0;
    for(const auto cop:police){
        const float t=length2>0?std::clamp(((cop.x-a.x)*dx+(cop.y-a.y)*dy)/length2,0.0f,1.0f):0;
        const roadnav::Vec3 closest{a.x+dx*t,a.y+dy*t,a.z+(b.z-a.z)*t};
        if(std::fabs(closest.z-cop.z)>35)continue;
        const float proximity=std::max(0.0f,1-roadnav::distance2D(closest,cop)/120);
        risk+=proximity*proximity*8;
    }
    return std::sqrt(length2)*std::min(risk,24.0f);
}
inline float pathScore(const std::vector<roadnav::Vec3>& path,roadnav::Vec3 player,const std::vector<roadnav::Vec3>& police){
    if(path.size()<2)return std::numeric_limits<float>::max();
    const float start=roadnav::nearestDistanceAlong(path,player),end=roadnav::routeLength2D(path);
    float result=std::max(0.0f,end-start);
    roadnav::ForwardRouteSampler samples(path);
    // Fixed spacing keeps the penalty independent of native node density;
    // only evaluate the road still ahead, never police behind the vehicle.
    for(float d=start;d<end&&d<start+700;d+=12){
        const auto a=samples.at(d).position,b=samples.at(std::min(d+12,end)).position;
        result+=segmentRisk(a,b,police);
    }
    return result;
}
inline bool onPath(const std::vector<roadnav::Vec3>& path,roadnav::Vec3 player){
    if(path.size()<2)return false;
    const auto closest=roadnav::sampleAt(path,roadnav::nearestDistanceAlong(path,player)).position;
    return roadnav::distance2D(player,closest)<=12&&std::fabs(player.z-closest.z)<=5;
}
inline float pathRisk(const std::vector<roadnav::Vec3>& path,roadnav::Vec3 player,const std::vector<roadnav::Vec3>& police){
    if(path.size()<2)return 0;
    return std::max(0.0f,pathScore(path,player,police)-(roadnav::routeLength2D(path)-roadnav::nearestDistanceAlong(path,player)));
}
inline float remaining(const std::vector<roadnav::Vec3>& path,roadnav::Vec3 player){
    return path.size()>1?std::max(0.0f,roadnav::routeLength2D(path)-roadnav::nearestDistanceAlong(path,player)):0;
}
inline float refreshLead(float speed){return std::max(100.0f,std::max(0.0f,speed)*6);}
inline bool needsForwardRoute(const std::vector<roadnav::Vec3>& path,roadnav::Vec3 player,float speed){
    return !onPath(path,player)||remaining(path,player)<std::max(30.0f,std::max(0.0f,speed)*2.5f);
}
inline bool replanDue(bool valid,bool onRoute,unsigned age,float committedRisk,float risk){
    if(!valid||!onRoute)return true;
    return age>=12000||(age>=5000&&risk>committedRisk+250);
}
inline bool exits(const std::vector<roadnav::Vec3>& path,roadnav::Vec3 center,float radius){
    return path.size()>1&&outside(path.back(),center,radius);
}
// Compare both native GPS buffers against the outstanding request; the first
// populated buffer can still belong to a previous route.
inline float endpointError(const std::vector<roadnav::Vec3>& path,roadnav::Vec3 target){
    if(path.size()<2)return std::numeric_limits<float>::max();
    return roadnav::distance2D(path.back(),target)+4*std::fabs(path.back().z-target.z);
}
inline unsigned routeWait(unsigned shownPoints){return shownPoints>1?2500u:6000u;}
inline bool matches(const std::vector<roadnav::Vec3>& path,roadnav::Vec3 target){return path.size()>1&&roadnav::distance2D(path.back(),target)<=45&&std::fabs(path.back().z-target.z)<=15;}
// Snap boundary samples to drivable nodes, then compare actual road travel
// costs instead of drawing a straight line over buildings or water.
inline bool followsHeading(const std::vector<roadnav::Vec3>& path,roadnav::Vec3 player,roadnav::Vec3 heading){
    const float h=std::hypot(heading.x,heading.y);if(h<.5f)return true;
    if(path.size()<2)return false;
    const float at=roadnav::nearestDistanceAlong(path,player);
    const auto a=roadnav::sampleAt(path,at).position,b=roadnav::sampleAt(path,std::min(roadnav::routeLength2D(path),at+15)).position;
    const float dx=b.x-a.x,dy=b.y-a.y,d=std::hypot(dx,dy);
    return d>1&&(dx*heading.x+dy*heading.y)/(d*h)>.2f;
}
template<class Snap,class Cost> std::vector<Candidate> candidates(roadnav::Vec3 player,roadnav::Vec3 center,float radius,float heading,Snap snap,Cost cost,const std::vector<roadnav::Vec3>& police,bool forward=false){
    std::vector<Candidate> result;
    if(!std::isfinite(radius)||radius<50||radius>3000)return result;
    const float distance=roadnav::distance2D(player,center);
    const float sampleRadius=std::max(radius+150,distance+150);
    const float angle=distance>10?std::atan2(player.y-center.y,player.x-center.x):heading;

    for(unsigned i=0;i<16;++i){const float a=angle+static_cast<float>(i)*.3926990817f;
        const roadnav::Vec3 sample{center.x+std::cos(a)*sampleRadius,center.y+std::sin(a)*sampleRadius,player.z};
        roadnav::Vec3 node{};if(!snap(sample,node)||!std::isfinite(node.x)||!std::isfinite(node.y)||!std::isfinite(node.z)||roadnav::distance2D(node,center)<radius+120||std::fabs(node.z-player.z)>90)continue;
        const float dx=node.x-player.x,dy=node.y-player.y;
        if(forward&&dx*std::cos(heading)+dy*std::sin(heading)<=.2f*std::hypot(dx,dy))continue;
        const float travel=cost(player,node);
        if(!std::isfinite(travel)||travel<1||travel>15000||travel+30<roadnav::distance2D(player,node))continue;
        bool duplicate=false;for(const auto& old:result)if(roadnav::distance2D(old.point,node)<20)duplicate=true;
        if(!duplicate)result.push_back({node,travel+segmentRisk(player,node,police)});
    }
    std::stable_sort(result.begin(),result.end(),[](const Candidate& a,const Candidate& b){return a.score<b.score;});
    return result;
}
template<class Snap,class Cost> bool destination(roadnav::Vec3 player,roadnav::Vec3 center,float radius,float heading,Snap snap,Cost cost,roadnav::Vec3& selected){
    const auto options=candidates(player,center,radius,heading,snap,cost,{});
    if(options.empty())return false;
    selected=options.front().point;return true;
}
}
