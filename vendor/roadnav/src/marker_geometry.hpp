#pragma once
#include "route_geometry.hpp"
#include <limits>

namespace roadnav {
struct Quaternion { float x{}, y{}, z{}, w{1}; };
// Forward in the stock arrow mesh is local +Z; its face normal is local -Y.
// Map those axes onto the route tangent and road normal respectively.
inline Quaternion roadArrowRotation(Vec3 direction) {
    const float horizontal = std::hypot(direction.x, direction.y);
    if (horizontal < 0.0001f) return {};
    const float length = std::sqrt(horizontal*horizontal + direction.z*direction.z);
    Vec3 f{direction.x/length,direction.y/length,direction.z/length};
    Vec3 r{direction.y/horizontal,-direction.x/horizontal,0};
    Vec3 u{r.y*f.z,-r.x*f.z,r.x*f.y-r.y*f.x};
    const float m[3][3]={{r.x,-u.x,f.x},{r.y,-u.y,f.y},{r.z,-u.z,f.z}};
    Quaternion q;
    const float trace=m[0][0]+m[1][1]+m[2][2];
    if(trace>0){
        const float s=2*std::sqrt(trace+1);
        q={ (m[2][1]-m[1][2])/s,(m[0][2]-m[2][0])/s,(m[1][0]-m[0][1])/s,s/4 };
    }else{
        int i=0;if(m[1][1]>m[i][i])i=1;if(m[2][2]>m[i][i])i=2;
        const int j=(i+1)%3,k=(i+2)%3;
        const float s=2*std::sqrt(1+m[i][i]-m[j][j]-m[k][k]);
        float a[3]{};a[i]=s/4;a[j]=(m[i][j]+m[j][i])/s;a[k]=(m[i][k]+m[k][i])/s;
        q={a[0],a[1],a[2],(m[k][j]-m[j][k])/s};
    }
    return q;
}
inline Vec3 rotate(Quaternion q,Vec3 v){
    const Vec3 t{2*(q.y*v.z-q.z*v.y),2*(q.z*v.x-q.x*v.z),2*(q.x*v.y-q.y*v.x)};
    return {v.x+q.w*t.x+q.y*t.z-q.z*t.y,
            v.y+q.w*t.y+q.z*t.x-q.x*t.z,
            v.z+q.w*t.z+q.x*t.y-q.y*t.x};
}
inline float nearestDistanceAlong(const std::vector<Vec3>& points,Vec3 player){
    float best=std::numeric_limits<float>::max(),along=0,result=0;
    for(std::size_t i=1;i<points.size();++i){
        const auto a=points[i-1],b=points[i];const float dx=b.x-a.x,dy=b.y-a.y;
        const float len=std::hypot(dx,dy);if(len<0.0001f)continue;
        const float t=std::clamp(((player.x-a.x)*dx+(player.y-a.y)*dy)/(len*len),0.0f,1.0f);
        const float ex=player.x-a.x-t*dx,ey=player.y-a.y-t*dy;
        // Height distinguishes stacked roads without shifting along a slope.
        const float ez=player.z-a.z-t*(b.z-a.z);
        const float distance=ex*ex+ey*ey+ez*ez;
        if(distance<best){best=distance;result=along+t*len;}
        along+=len;
    }
    return result;
}
}
