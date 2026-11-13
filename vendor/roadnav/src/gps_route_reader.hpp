#pragma once
#include "marker_geometry.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
namespace roadnav {
struct NativeGpsRoute {int slot=-1,state=0;std::vector<Vec3> points;};
// The two native buffers can simultaneously contain old and new requests.
// Copy each consistently, then choose the endpoint belonging to our request.
struct AcceptGpsRoute {bool operator()(const std::vector<Vec3>&)const{return true;}};
template<class Read,class Accept=AcceptGpsRoute> bool readGpsRoute(std::uintptr_t slots,Read read,NativeGpsRoute& result,const Vec3* target=nullptr,const Vec3* start=nullptr,Accept accept={}){
    constexpr std::size_t stride=0xdb0,countAt=0xd60,stateAt=0xd64,maxPoints=countAt/16;
    result={};float best=std::numeric_limits<float>::max(),bestOrigin=std::numeric_limits<float>::max();
    for(int slot=0;slot<2;++slot){
        const auto base=slots+static_cast<std::uintptr_t>(slot)*stride;std::int32_t count=0,state=0;
        if(!read(base+countAt,&count,4)||!read(base+stateAt,&state,4)||count<2||count>static_cast<int>(maxPoints)||(state!=2&&state!=3&&state!=5))continue;
        std::array<float,maxPoints*4> raw{};if(!read(base,raw.data(),static_cast<std::size_t>(count)*16))continue;
        std::vector<Vec3> points;bool valid=true;points.reserve(count);
        for(int i=0;i<count;++i){const auto at=static_cast<std::size_t>(i)*4;const Vec3 point{raw[at],raw[at+1],raw[at+2]};
            if(!std::isfinite(point.x)||!std::isfinite(point.y)||!std::isfinite(point.z)||std::fabs(point.x)>6000||std::fabs(point.y)>6000||std::fabs(point.z)>2000){valid=false;break;}
            points.push_back(point);
        }
        std::int32_t countAfter=0,stateAfter=0;
        if(!valid||!read(base+countAt,&countAfter,4)||!read(base+stateAt,&stateAfter,4)||countAfter!=count||stateAfter!=state)continue;
        // Validate each buffer before ranking. A backward old buffer can have
        // a more exact endpoint than a usable new forward calculation.
        if(!accept(points))continue;
        const float error=target?distance2D(points.back(),*target)+4*std::fabs(points.back().z-target->z):0;
        // Opt-in only after a primary replan. Use origin only for effectively
        // identical endpoints; never trade ownership for a nearer other goal.
        const float origin=start?distance2D(points.front(),*start)+4*std::fabs(points.front().z-start->z):0;
        const bool replace=start&&result.slot>=0&&std::fabs(error-best)<=1?origin<bestOrigin:error<best;
        if(replace){result={slot,state,std::move(points)};best=error;bestOrigin=origin;}
        if(!target&&!start)return true;
    }
    return result.points.size()>1;
}
}
