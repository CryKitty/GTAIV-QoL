#pragma once
#include "road_line.hpp"
#include <utility>
namespace roadnav {
inline constexpr float roadSurfaceClearance=.24f;
struct RoadSurfaceStats {unsigned sampled=0,reused=0,missing=0;};
// GPS supplies only the horizontal path. Probe a shallow corridor above the
// last connected surface; never return to GPS height after a failed query.
template<class Probe> RoadSurfaceStats fitRoadSurface(RoadLine& line,Vec3 player,const RoadLine& previous,Probe probe,const RoadLine* sharedSurface=nullptr){
    RoadSurfaceStats stats;
    auto cached=[&](Vec3 point,float expected,float radius,float& height){
        float best=radius;bool found=false;
        const auto search=[&](const RoadLine& source){for(const auto& old:source.points){const float d=distance2D(point,old);
            if(d<best&&std::fabs(old.z-roadSurfaceClearance-expected)<2){best=d;height=old.z-roadSurfaceClearance;found=true;}}};
        search(previous);
        // Both independently drawn routes can share already validated road
        // samples. Never use native GPS Z or join a different elevation.
        if(sharedSurface)search(*sharedSurface);
        return found;
    };
    float surface=probe(player.x,player.y,player.z+.2f);
    if(!std::isfinite(surface)||surface<player.z-3.5f||surface>player.z+.3f){
        surface=player.z-1;cached(player,surface,8,surface);
    }
    Vec3 anchor=player;float unsupported=0;
    line.edges.clear();
    for(std::size_t i=0;i<line.points.size();++i){
        auto& point=line.points[i];const float distance=distance2D(anchor,point);
        const unsigned steps=std::max(1u,static_cast<unsigned>(std::ceil(std::min(distance,96.0f)/1.5f)));
        const auto from=anchor;
        for(unsigned n=1;n<=steps;++n){
            const float t=static_cast<float>(n)/steps,step=distance/steps;
            Vec3 sample{from.x+(point.x-from.x)*t,from.y+(point.y-from.y)*t,surface};
            const float allowance=.3f+.5f*step;
            const float ground=probe(sample.x,sample.y,surface+allowance);
            if(std::isfinite(ground)&&std::fabs(ground-surface)<=allowance){surface=ground;unsupported=0;++stats.sampled;}
            else {float old=surface;
                if(cached(sample,surface,1.6f,old)){surface=old;unsupported=0;++stats.reused;}
                else {unsupported+=step;++stats.missing;}
            }
            if(unsupported>6)break;
        }
        // Unstreamed collision ends only the distant tail, not the whole path.
        if(unsupported>6){line.points.resize(i);line.endsAtDestination=false;break;}
        point.z=surface+roadSurfaceClearance;
        const auto section=roadLineSection(line.points,i,.8568f);
        std::array<float,2> edges{{surface+roadSurfaceClearance,surface+roadSurfaceClearance}};
        for(unsigned edge=0;edge<2;++edge){
            const float ground=probe(section[edge].x,section[edge].y,surface+.45f);
            if(std::isfinite(ground)&&std::fabs(ground-surface)<.5f)edges[edge]=ground+roadSurfaceClearance;
        }
        line.edges.push_back(edges);anchor=point;
    }
    line.ahead=routeLength2D(line.points);
    // Banking depends only on XY, which fitting leaves unchanged. Recompute
    // only for a truncated tail or a caller without precomputed bank values.
    if(line.bank.size()!=line.points.size())line.bank=roadLineBank(line.points);
    return stats;
}
// Retain the same road height at overlapping world positions. Small collision
// query noise should not bounce a stationary ribbon; distinct levels stay apart.
inline void stabilizeRoadSurface(RoadLine& line,const RoadLine& previous){
    for(std::size_t n=0;n<line.points.size();++n){auto& point=line.points[n];float best=.6f;
        float oldHeight=point.z;std::array<float,2> oldEdges{};bool found=false;
        for(std::size_t i=1;i<previous.points.size();++i){const auto a=previous.points[i-1],b=previous.points[i];
            const float dx=b.x-a.x,dy=b.y-a.y,length2=dx*dx+dy*dy;
            const float t=length2>.001f?std::clamp(((point.x-a.x)*dx+(point.y-a.y)*dy)/length2,0.0f,1.0f):0;
            const Vec3 closest{a.x+dx*t,a.y+dy*t,a.z+(b.z-a.z)*t};const float distance=distance2D(point,closest);
            if(distance>=best||std::fabs(point.z-closest.z)>.35f)continue;
            best=distance;oldHeight=closest.z;found=true;oldEdges={closest.z,closest.z};
            if(i<previous.edges.size())for(unsigned side=0;side<2;++side)oldEdges[side]=previous.edges[i-1][side]+(previous.edges[i][side]-previous.edges[i-1][side])*t;
        }
        if(found){point.z=oldHeight+std::clamp((point.z-oldHeight)*.2f,-.04f,.04f);
            if(n<line.edges.size())for(unsigned side=0;side<2;++side)if(std::fabs(line.edges[n][side]-oldEdges[side])<.35f)line.edges[n][side]=oldEdges[side]+std::clamp((line.edges[n][side]-oldEdges[side])*.2f,-.04f,.04f);
        }
    }
}

// Publish a fitted escape candidate without dropping the last valid ribbon
// during planner retries or collision-streaming misses. Return true if retained.
inline bool updateEscapeSurface(RoadLine& current,RoadLine next,bool committed,bool planning){
    if(planning)next.endsAtDestination=false;
    if(!next.visible()&&current.visible()&&committed)return true;
    current=std::move(next);return false;
}

}
