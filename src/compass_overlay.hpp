#pragma once
#include "compass_notifications.hpp"
#include <vector>
#include <algorithm>
namespace compass {
struct NoticeVertex {float x,y;unsigned alpha;};
// Core plus transparent fringes provides analytic edge coverage at any HUD
// scale, independently of the game's MSAA setting. Six continuous strokes.
inline std::vector<NoticeVertex> envelopeMesh(float x,float y,float width,float height,unsigned alpha,float thickness){
    std::vector<NoticeVertex> vertices;vertices.reserve(108);
    const float half=std::max(.4f,thickness*.5f),fringe=.85f;
    for(const auto& segment:envelopeSegments()){
        const float ax=x+segment.x1*width,ay=y+segment.y1*height,bx=x+segment.x2*width,by=y+segment.y2*height;
        const float length=std::hypot(bx-ax,by-ay);if(length<.01f)continue;
        const float nx=-(by-ay)/length,ny=(bx-ax)/length;
        const float offset[4]={-half-fringe,-half,half,half+fringe};
        const unsigned opacity[4]={0,alpha,alpha,0};
        for(unsigned band=0;band<3;++band){
            const NoticeVertex corners[4]={{ax+nx*offset[band],ay+ny*offset[band],opacity[band]},
                {bx+nx*offset[band],by+ny*offset[band],opacity[band]},
                {bx+nx*offset[band+1],by+ny*offset[band+1],opacity[band+1]},
                {ax+nx*offset[band+1],ay+ny*offset[band+1],opacity[band+1]}};
            for(unsigned n:{0u,1u,2u,0u,2u,3u})vertices.push_back(corners[n]);
        }
    }
    return vertices;
}
// Pixel-space ring geometry keeps circles round at every screen aspect ratio.
// Clockwise from twelve o'clock; the last segment ends at the exact hold fraction.
inline std::vector<NoticeVertex> holdRingMesh(float x,float y,float radius,float progress,unsigned alpha,float thickness){
    std::vector<NoticeVertex> vertices;
    if(!std::isfinite(progress)||!std::isfinite(radius)||radius<=0||progress<=0||!alpha)return vertices;
    progress=std::min(progress,1.0f);
    constexpr unsigned segments=96;constexpr float tau=6.28318530718f;
    const unsigned count=static_cast<unsigned>(std::ceil(progress*segments));vertices.reserve(count*18);
    const float half=std::max(.5f,thickness*.5f),fringe=.85f;
    const float offsets[4]={-half-fringe,-half,half,half+fringe};
    const unsigned opacity[4]={0,alpha,alpha,0};
    for(unsigned segment=0;segment<count;++segment){
        const float start=tau*static_cast<float>(segment)/segments,end=tau*std::min(progress,static_cast<float>(segment+1)/segments);
        for(unsigned band=0;band<3;++band){
            const auto point=[&](float angle,unsigned edge){const float r=radius+offsets[edge];return NoticeVertex{x+std::sin(angle)*r,y-std::cos(angle)*r,opacity[edge]};};
            const NoticeVertex corners[4]={point(start,band),point(end,band),point(end,band+1),point(start,band+1)};
            for(unsigned index:{0u,1u,2u,0u,2u,3u})vertices.push_back(corners[index]);
        }
    }
    return vertices;
}

}
