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
}
