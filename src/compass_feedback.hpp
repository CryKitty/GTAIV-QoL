#pragma once
#include "compass.hpp"
namespace compass {
// A single objective-style caption slot for holding, results and arrival.
// Keep the last icon anchor if clearing a waypoint removes that icon mid-frame.
struct Caption {
    const char* text="";
    float x=StripLayout::center,y=StripLayout::focusLabelY;
    float size=.22f,width=.85f,height=1.10f;
    unsigned alpha=235;
    bool visible()const{return text&&text[0];}
};
inline Caption caption(const char* text,float x){Caption out;out.text=text;out.x=std::clamp(x,StripLayout::left,StripLayout::right);return out;}
struct WaypointFeedback {
    const char* text="";unsigned target=0;std::uint32_t at=0,duration=0;
    float x=StripLayout::center;
    void show(const char* message,std::uint32_t now,unsigned handle,float anchor,std::uint32_t lifetime=1800){text=message;at=now;target=handle;x=anchor;duration=lifetime;}
    void track(unsigned handle,float anchor){if(target&&target==handle)x=anchor;}
    Caption draw(std::uint32_t now)const{return caption(duration&&now-at<duration?text:"",x);}
};
}
