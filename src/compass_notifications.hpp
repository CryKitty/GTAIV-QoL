#pragma once
#include <array>
#include <cmath>
#include <cstdint>

namespace compass {
// Mirror the game's SET_MESSAGES_WAITING state. It remains set across opening
// the phone and clears only when the phone script reports no unread texts.
struct MessageNotice {
    bool waiting=false;
    std::uint32_t arrivedAt=0;
    void update(bool unread,std::uint32_t now){waiting=unread;if(unread)arrivedAt=now;}
    unsigned alpha(std::uint32_t now)const{
        if(!waiting)return 0;
        const auto age=now-arrivedAt;
        return age<2400&&(age/300)%2?120u:240u;
    }
};
struct NoticeRect {float x,y,width,height;};
inline NoticeRect messageEnvelope(float aspect){
    if(!std::isfinite(aspect)||aspect<.5f||aspect>5)aspect=16.0f/9;
    const float width=.028f/aspect;
    return {.30f-.024f-width/2,.040f,width,.0175f};
}
// Continuous line segments; render with a feathered edge rather than a
// staircase of little rectangles. Coordinates are relative to the icon bounds.
struct NoticeSegment {float x1,y1,x2,y2;};
inline constexpr std::array<NoticeSegment,6> envelopeSegments() {
    return {{{-.5f,-.5f,.5f,-.5f},{.5f,-.5f,.5f,.5f},
             {.5f,.5f,-.5f,.5f},{-.5f,.5f,-.5f,-.5f},
             {-.5f,-.5f,0,.08f},{0,.08f,.5f,-.5f}}};
}
}
