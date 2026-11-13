#pragma once
#include "../vendor/roadnav/src/road_line.hpp"
#include <cstdint>
namespace roadnav {
struct RoadLaneSegment {Vec3 a{},b{};int forward=-1,backward=-1;float median=-1;};
// GET_CLOSEST_ROAD supplies counts for both directions, not a measured width.
// Reject nearby parallel/intersecting roads and other decks before using them.
inline int roadLaneCount(const RoadLaneSegment& road,Vec3 point,Vec3 tangent){
    if(road.forward<0||road.backward<0||road.forward>8||road.backward>8||
       road.forward+road.backward<1||!std::isfinite(road.median)||road.median<0||road.median>40)return 0;
    for(auto p:{road.a,road.b})if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))return 0;
    const float dx=road.b.x-road.a.x,dy=road.b.y-road.a.y,length=std::hypot(dx,dy),h=std::hypot(tangent.x,tangent.y);
    if(length<2||length>500||h<.5f||std::fabs((dx*tangent.x+dy*tangent.y)/(length*h))<.85f)return 0;
    const float t=((point.x-road.a.x)*dx+(point.y-road.a.y)*dy)/(length*length);
    if(t<-.05f||t>1.05f)return 0;
    const Vec3 projected{road.a.x+dx*t,road.a.y+dy*t,road.a.z+(road.b.z-road.a.z)*t};
    if(distance2D(point,projected)>6||std::fabs(point.z-projected.z)>5)return 0;
    return road.forward+road.backward;
}
struct LaneOffsetSample {
    Vec3 point{},direction{};std::uint32_t checked=0,updated=0;
    bool wide=false,candidate=false;unsigned observations=0;float amount=0;
    void observe(bool next){
        if(next==wide){observations=0;candidate=next;return;}
        if(next!=candidate){candidate=next;observations=1;}
        else ++observations;
        if(observations>=2){wide=next;observations=0;}
    }
    float advance(std::uint32_t now){
        const float step=static_cast<float>(std::min(now-updated,200u))/1500.0f;updated=now;
        amount=wide?std::min(1.0f,amount+step):std::max(0.0f,amount-step);return amount;
    }
};
struct RoadLaneOffset {
    std::vector<LaneOffsetSample> samples;
    template<class Query> float at(Vec3 point,Vec3 tangent,std::uint32_t now,Query query){
        for(auto& sample:samples){
            if(distance2D(sample.point,point)>4||std::fabs(sample.point.z-point.z)>3||
               std::fabs(sample.direction.x*tangent.x+sample.direction.y*tangent.y)<.85f)continue;
            if(now-sample.checked>=1000){sample.observe(query(sample.point,sample.direction)>=2);sample.checked=now;}
            return sample.advance(now);
        }
        const bool wide=query(point,tangent)>=2;
        if(samples.size()>=256)samples.erase(samples.begin());
        samples.push_back({point,tangent,now,now,wide,wide,0,wide?1.0f:0.0f});return wide?1.0f:0.0f;
    }
    template<class Query> std::vector<float> amounts(const RoadLine& line,const std::vector<Vec3>& route,std::uint32_t now,Query query){
        std::vector<float> result;result.reserve(line.points.size());
        if(route.size()<2)return std::vector<float>(line.points.size(),0);
        // Fixed route-distance stations and interpolation avoid segment-index
        // flips; a 12 m blend avoids a lateral step at a lane-count boundary.
        const float total=routeLength2D(route);float cached=-1,left=0,right=0;
        for(auto point:line.points){
            const float along=nearestDistanceAlong(route,point),station=std::floor(along/12)*12;
            const auto value=[&](float d){const auto p=sampleAt(route,d),a=sampleAt(route,std::max(0.0f,d-4)),b=sampleAt(route,std::min(total,d+4));
                const float length=distance2D(a.position,b.position);
                const Vec3 tangent=length>.01f?Vec3{(b.position.x-a.position.x)/length,(b.position.y-a.position.y)/length,0}:p.direction;
                return at(p.position,tangent,now,query);};
            if(station!=cached){left=value(station);right=value(std::min(total,station+12));cached=station;}
            const float t=std::clamp((along-station)/12,0.0f,1.0f),smooth=t*t*(3-2*t);
            result.push_back(2*(left+(right-left)*smooth));
        }
        return result;
    }
};
}
