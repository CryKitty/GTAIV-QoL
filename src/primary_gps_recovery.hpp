#pragma once
#include "../vendor/roadnav/src/marker_geometry.hpp"
#include <cstdint>
namespace roadnav {
// Recover a live fixed destination independently of the chase/escape branch.
// An authored SET_ROUTE call is not proof that its asynchronous road arrived.
struct PrimaryGpsRecovery {
    unsigned target=0,failures=0;
    Vec3 goal{};
    std::uint32_t requestedAt=0;
    bool attempted=false;
    bool due(std::uint32_t now,unsigned nextTarget,Vec3 nextGoal,bool usable,bool borrowed){
        if(nextTarget!=target||distance2D(nextGoal,goal)>5||std::fabs(nextGoal.z-goal.z)>5){
            *this={};target=nextTarget;goal=nextGoal;
        }
        if(!target)return false;
        if(usable){failures=0;return false;}
        if(borrowed)return false;
        const unsigned wait=std::min(10000u,6000u*(1u<<std::min(failures,1u)));
        if(attempted&&now-requestedAt<wait)return false;
        if(attempted)failures=std::min(failures+1,2u);
        attempted=true;requestedAt=now;return true;
    }
};
inline bool primaryDestinationMatches(const std::vector<Vec3>& path,Vec3 goal,const Vec3* roadNode=nullptr){
    if(path.size()<2)return false;
    const auto near=[&](Vec3 point){return distance2D(path.back(),point)<=65&&std::fabs(path.back().z-point.z)<=15;};
    return near(goal)||(roadNode&&near(*roadNode));
}
}
