#pragma once
#include "road_line.hpp"
namespace roadnav {
inline bool finiteDestination(Vec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
// Pin the symbol independently of the road-fitted ribbon. Mission coordinates
// must not be replaced by the native GPS's nearest road node or ground sample.
inline void pinDestination(RoadLine& line,Vec3 target){if(finiteDestination(target)){line.destination=target;line.destinationPinned=true;}}
inline Vec3 roadLineDestination(const RoadLine& line){return line.destinationPinned?line.destination:line.points.empty()?Vec3{}:line.points.back();}
inline void pinDestination(NavigationCues& cues,Vec3 target){if(cues.destination.visible&&finiteDestination(target))cues.destination.position=target;}
// Only extend a complete, validated road tail within its small road corridor.
// Never join an unstreamed/truncated ribbon or cut across a distant block.
inline bool trackLineEndpoint(RoadLine& line,Vec3 target){
    if(!line.endsAtDestination||line.points.size()<2||!finiteDestination(target))return false;
    float best=8,tail=0;std::size_t segment=0;
    for(std::size_t i=line.points.size()-1;i>0;--i){
        const auto a=line.points[i-1],b=line.points[i];const float dx=b.x-a.x,dy=b.y-a.y,length=distance2D(a,b);
        if(length<.001f)continue;
        const float raw=((target.x-a.x)*dx+(target.y-a.y)*dy)/(length*length);
        const float t=std::clamp(raw,0.0f,i+1==line.points.size()?1+30/length:1.0f);
        const Vec3 projection{a.x+dx*t,a.y+dy*t,a.z+(b.z-a.z)*std::min(t,1.0f)};
        const float distance=distance2D(target,projection);
        if(distance<best&&std::fabs(target.z-projection.z)<4){best=distance;segment=i;}
        tail+=length;if(tail>150)break;
    }
    if(!segment)return false;
    // Preserve all previously fitted cross-sections before the short live tail.
    line.points.resize(segment);line.points.push_back(target);
    if(!line.edges.empty()){line.edges.resize(segment);line.edges.push_back({target.z,target.z});}
    line.bank=roadLineBank(line.points);line.ahead=routeLength2D(line.points);pinDestination(line,target);return true;
}
}
