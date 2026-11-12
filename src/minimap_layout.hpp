#pragma once
#include <cstdint>
#include <cstddef>
#include <cmath>
namespace minimaplayout {
struct Point {float x,y;};
struct Info {std::uint32_t slot;Point pos,size;std::uint32_t rgb,alpha;};
static_assert(sizeof(Info)==28,"Native HUD layout record");
inline bool displaced(Point p){return p.x==-10000&&p.y==-10000;}
inline bool valid(Point p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::fabs(p.x)<=10000&&std::fabs(p.y)<=10000;}
inline bool radar(const Info& info){return info.slot<67&&valid(info.pos)&&valid(info.size)&&info.size.x>0&&info.size.y>0;}
inline std::uint32_t frontend(const unsigned char* p,std::size_t n){
    unsigned at=0;
    if(n>=15&&p[0]==0xa0&&p[5]==0xc6&&p[6]==5&&p[11]==1&&p[12]==0x8d&&p[13]==0x73&&p[14]==3)at=1;
    else if(n>=14&&p[0]==0x8a&&p[1]==0x1d&&p[6]==0xc6&&p[7]==5&&p[12]==1&&p[13]==0x7f)at=2;
    if(!at)return 0;
    return std::uint32_t(p[at])|(std::uint32_t(p[at+1])<<8)|(std::uint32_t(p[at+2])<<16)|(std::uint32_t(p[at+3])<<24);
}
}
