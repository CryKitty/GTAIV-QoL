#pragma once
#include "turn_cue.hpp"
#include <cstdint>

namespace roadnav {
inline constexpr float roadSurfaceClearance=.24f;
struct RoadLine {std::vector<Vec3> points;std::vector<float> bank;std::vector<std::array<float,2>> edges;float ahead=0;bool endsAtDestination=false;Vec3 destination{};bool destinationPinned=false;bool visible()const{return points.size()>1;}};
inline Vec3 roadLineGroundAnchor(const RoadLine& line,Vec3 player){
    if(!line.points.empty())player.z=sampleAt(line.points,nearestDistanceAlong(line.points,player)).position.z-roadSurfaceClearance;
    return player;
}
// Dampen only tiny viewing-angle noise in decorative cross-section tilt.
// Projection and route geometry remain live; substantial turns snap immediately.
struct RoadLineViewHeading {
    Vec3 value{};std::uint32_t at=0;bool initialized=false;
    Vec3 update(Vec3 next,std::uint32_t now){
        const float length=std::hypot(next.x,next.y);if(length<.01f)return next;
        next={next.x/length,next.y/length,0};
        const float dot=value.x*next.x+value.y*next.y;
        if(!initialized||dot<.9961947f){value=next;initialized=true;at=now;return value;} // >5 degrees
        const float cross=value.x*next.y-value.y*next.x;
        const float elapsed=static_cast<float>(std::min(now-at,200u));at=now;
        if(std::fabs(cross)<.002618f)return value; // 0.15 degree deadband
        const float blend=elapsed/(150+elapsed);
        value={value.x+(next.x-value.x)*blend,value.y+(next.y-value.y)*blend,0};
        const float norm=std::hypot(value.x,value.y);value.x/=norm;value.y/=norm;return value;
    }
};
// Radar radius, not speed or route arc length. Native export has the same bounds.
inline float roadLineRange(float radius){return std::isfinite(radius)&&radius>=10&&radius<=5000?radius:300.0f;}
inline float roadRangeExit(Vec3 a,Vec3 b,Vec3 center,float radius){
    const float x=a.x-center.x,y=a.y-center.y,dx=b.x-a.x,dy=b.y-a.y;
    const float aa=dx*dx+dy*dy,bb=x*dx+y*dy,cc=x*x+y*y-radius*radius;
    return aa>.000001f?std::clamp((-bb+std::sqrt(std::max(0.0f,bb*bb-aa*cc)))/aa,0.0f,1.0f):0;
}
inline float roadRangeEnd(const std::vector<Vec3>& route,Vec3 player,float start,float radius){
    float along=start;auto a=sampleAt(route,start).position;
    if(distance2D(a,player)>radius)return start;
    float station=0;
    for(std::size_t i=1;i<route.size();++i){const float length=distance2D(route[i-1],route[i]);station+=length;
        if(station<=start)continue;
        const auto b=route[i];
        if(distance2D(b,player)>radius)return along+distance2D(a,b)*roadRangeExit(a,b,player,radius);
        along=station;a=b;
    }
    return along;
}
inline void clipRoadLineRange(RoadLine& line,Vec3 player,float radarRadius){
    const float radius=roadLineRange(radarRadius);
    if(line.points.empty())return;
    if(line.destinationPinned&&distance2D(line.destination,player)>radius)line.endsAtDestination=false;
    if(distance2D(line.points.front(),player)>radius){
        bool entry=false;
        for(std::size_t i=1;i<line.points.size();++i){
            const auto a=line.points[i-1],b=line.points[i];
            const float x=a.x-player.x,y=a.y-player.y,dx=b.x-a.x,dy=b.y-a.y;
            const float aa=dx*dx+dy*dy,bb=x*dx+y*dy,cc=x*x+y*y-radius*radius,disc=bb*bb-aa*cc;
            if(aa<.000001f||disc<0)continue;
            const float t=(-bb-std::sqrt(disc))/aa;if(t<0||t>1)continue;
            line.points[i-1]={a.x+dx*t,a.y+dy*t,a.z+(b.z-a.z)*t};
            if(line.bank.size()>i)line.bank[i-1]+=(line.bank[i]-line.bank[i-1])*t;
            if(line.edges.size()>i)for(unsigned side=0;side<2;++side)line.edges[i-1][side]+=(line.edges[i][side]-line.edges[i-1][side])*t;
            const auto skip=static_cast<std::ptrdiff_t>(i-1);
            line.points.erase(line.points.begin(),line.points.begin()+skip);
            if(line.bank.size()>=i)line.bank.erase(line.bank.begin(),line.bank.begin()+skip);
            if(line.edges.size()>=i)line.edges.erase(line.edges.begin(),line.edges.begin()+skip);
            line.ahead=routeLength2D(line.points);entry=true;break;
        }
        if(!entry){line={};return;}
    }
    for(std::size_t i=1;i<line.points.size();++i)if(distance2D(line.points[i],player)>radius){
        const auto a=line.points[i-1],b=line.points[i];const float t=roadRangeExit(a,b,player,radius);
        line.points[i]={a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t};
        if(line.bank.size()>i)line.bank[i]=line.bank[i-1]+(line.bank[i]-line.bank[i-1])*t;
        if(line.edges.size()>i)for(unsigned side=0;side<2;++side)line.edges[i][side]=line.edges[i-1][side]+(line.edges[i][side]-line.edges[i-1][side])*t;
        line.points.resize(i+1);if(line.bank.size()>i+1)line.bank.resize(i+1);if(line.edges.size()>i+1)line.edges.resize(i+1);
        line.ahead=routeLength2D(line.points);line.endsAtDestination=false;return;
    }
}
// Smooth direction continuously through GPS nodes, with bounded lateral and
// vertical deviation so a spline cannot cut a ramp or overshoot across a curb.
inline Vec3 smoothRoadPoint(const std::vector<Vec3>& route,std::size_t i,float t){
    const auto a=route[i-1],b=route[i],prev=route[i>=2?i-2:i-1],next=route[std::min(i+1,route.size()-1)];
    const float length=distance2D(a,b);if(length<.001f)return b;
    const auto direction=[](Vec3 a,Vec3 b){const float d=distance2D(a,b);return d>.001f?Vec3{(b.x-a.x)/d,(b.y-a.y)/d,(b.z-a.z)/d}:Vec3{};};
    const auto in=direction(prev,a),edge=direction(a,b),out=direction(b,next);
    const Vec3 m1{(in.x+edge.x)*length*.5f,(in.y+edge.y)*length*.5f,(in.z+edge.z)*length*.5f};
    const Vec3 m2{(edge.x+out.x)*length*.5f,(edge.y+out.y)*length*.5f,(edge.z+out.z)*length*.5f};
    const float u=t*t,v=u*t,h0=2*v-3*u+1,h1=v-2*u+t,h2=-2*v+3*u,h3=v-u;
    const Vec3 straight{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t};
    Vec3 delta{a.x*h0+m1.x*h1+b.x*h2+m2.x*h3-straight.x,a.y*h0+m1.y*h1+b.y*h2+m2.y*h3-straight.y,a.z*h0+m1.z*h1+b.z*h2+m2.z*h3-straight.z};
    const float deviation=std::hypot(delta.x,delta.y);if(deviation>1.25f){delta.x*=1.25f/deviation;delta.y*=1.25f/deviation;}
    return {straight.x+delta.x,straight.y+delta.y,straight.z};
}
// Signed bend profile raises the outside edge like a banked track. Sample a
// fixed road distance on each side so density and vehicle speed cannot change it.
inline std::vector<float> roadLineBank(const std::vector<Vec3>& points){
    std::vector<float> bank(points.size(),0),along(points.size(),0);
    for(std::size_t i=1;i<points.size();++i)along[i]=along[i-1]+distance2D(points[i-1],points[i]);
    // Both sample distances increase monotonically. Preserve sampleAt's
    // interpolation/degenerate-segment rules without rescanning the whole road.
    std::size_t beforeIndex=1,afterIndex=1;float beforeTotal=0,afterTotal=0;
    Vec3 beforePoint=points.empty()?Vec3{}:points.front(),afterPoint=beforePoint;
    const auto sample=[&](float meters,std::size_t& index,float& total,Vec3& result){
        for(;index<points.size();++index){
            const auto a=points[index-1],b=points[index];const float length=distance2D(a,b);
            if(length<=.0001f)continue;
            if(total+length>=meters){const float t=std::clamp((meters-total)/length,0.0f,1.0f);
                return Vec3{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t};}
            total+=length;result=b;
        }
        return result;
    };
    for(std::size_t i=1;i+1<points.size();++i){
        const float before=std::max(0.0f,along[i]-10),after=std::min(along.back(),along[i]+10);
        if(along[i]-before<2||after-along[i]<2)continue;
        const auto p=sample(before,beforeIndex,beforeTotal,beforePoint),q=points[i],r=sample(after,afterIndex,afterTotal,afterPoint);
        const float x=q.x-p.x,y=q.y-p.y,u=r.x-q.x,v=r.y-q.y;
        const float angle=std::atan2(x*v-y*u,x*u+y*v);
        const float amount=std::clamp((std::fabs(angle)-.08f)/1.2f,0.0f,1.0f);
        bank[i]=std::copysign(amount*amount*(3-2*amount),angle);
    }
    return bank;
}
inline bool keepRoadLineRoute(const std::vector<Vec3>& points,Vec3 player,unsigned age){
    if(points.size()<2||age>2000)return false;
    const auto nearest=sampleAt(points,nearestDistanceAlong(points,player)).position;
    return distance2D(player,nearest)<=12&&std::fabs(player.z-nearest.z)<=4;
}
// Separate native-GPS consumer; the original RoadNav maneuver logic is untouched.
inline RoadLine roadLine(const std::vector<Vec3>& route,Vec3 player,float /*speed*/,bool /*tracked*/=false,float radarRadius=300){
    RoadLine line;if(route.size()<2)return line;
    const float start=nearestDistanceAlong(route,player)+4;
    // Read a small native-road guard beyond the circle: smoothing may move a
    // point inward by up to 1.25 m. Clip the actual smoothed path below.
    const float end=roadRangeEnd(route,player,start,roadLineRange(radarRadius)+1.5f);
    // Quantized road stations avoid continuously rescaling every vertex as
    // the range/anchor moves. At most 600 samples plus native corners.
    const float spacing=1.5f*std::max(1.0f,std::ceil((end-start)/600));
    if(end<=start+1)return line;
    float along=0;
    for(std::size_t i=1;i<route.size();++i){
        const float length=distance2D(route[i-1],route[i]);
        if(!std::isfinite(length)||length>1000){line.points.clear();return line;}
        const float segmentEnd=along+length;
        if(length>.001f&&segmentEnd>start&&along<end){
            const float a=std::max(start,along),b=std::min(end,segmentEnd);
            if(line.points.empty())line.points.push_back(smoothRoadPoint(route,i,(a-along)/length));
            for(float d=(std::floor(a/spacing)+1)*spacing;d<b&&line.points.size()<1000;d+=spacing)line.points.push_back(smoothRoadPoint(route,i,(d-along)/length));
            if(line.points.size()<1000)line.points.push_back(smoothRoadPoint(route,i,(b-along)/length));
        }
        along=segmentEnd;if(along>=end)break;
    }
    clipRoadLineRange(line,player,radarRadius);line.ahead=routeLength2D(line.points);line.bank=roadLineBank(line.points);line.endsAtDestination=end>=routeLength2D(route)-.01f&&!line.points.empty()&&distance2D(line.points.back(),route.back())<.01f;return line;
}
// Shared mitered cross sections keep neighboring ribbon quads joined smoothly.
inline float roadLineBankHeight(Vec3 point,Vec3 player){
    const float distance=distance2D(point,player);
    const float below=std::max(0.0f,point.z-player.z);
    // A nearby crest hides more of the bend. Keep raising its outer edge as
    // we approach from below; blend back naturally as we climb to its level.
    const float proximity=std::clamp(1.0f-distance/60.0f,0.0f,1.0f);
    const float approach=proximity*proximity*(3.0f-2.0f*proximity);
    return .65f+std::min(below*.65f,5.0f)+std::min(below*1.35f,11.0f)*approach;
}
// One constant peak height per bend, with a mirrored cosine profile in road
// distance. Multiplying each point by its own uphill/proximity height skewed
// tall bends into a one-sided exponential-looking wall.
inline std::vector<float> roadLineLift(const std::vector<Vec3>& points,const std::vector<float>& bank,Vec3 player){
    std::vector<float> lift(points.size(),0),along(points.size(),0);
    if(points.size()<3||bank.size()!=points.size())return lift;
    for(std::size_t i=1;i<points.size();++i)along[i]=along[i-1]+distance2D(points[i-1],points[i]);
    for(std::size_t first=0;first<bank.size();){
        if(std::fabs(bank[first])<.001f){++first;continue;}
        const bool left=bank[first]>0;std::size_t end=first;float weight=0,center=0,strength=0;
        while(end<bank.size()&&std::fabs(bank[end])>=.001f&&(bank[end]>0)==left){
            const auto amount=std::fabs(bank[end]);weight+=amount;center+=amount*along[end];strength=std::max(strength,amount);++end;
        }
        center/=std::max(weight,.001f);
        const float start=along[first?first-1:first],finish=along[std::min(end,along.size()-1)];
        const float radius=std::clamp(std::max(center-start,finish-center),8.0f,60.0f);
        const float height=roadLineBankHeight(sampleAt(points,center).position,player)*strength;
        for(std::size_t i=0;i<points.size();++i){
            const float t=(along[i]-center)/radius;
            if(std::fabs(t)<1)lift[i]+=(left?1.0f:-1.0f)*height*.5f*(1+std::cos(3.14159265359f*t));
        }
        first=end;
    }
    return lift;
}
inline std::array<Vec3,2> roadLineSection(const std::vector<Vec3>& points,std::size_t i,float width,float bank=0,float bankHeight=.65f){
    const auto p=points[i],a=points[i?i-1:i],b=points[std::min(i+1,points.size()-1)];
    const float da=distance2D(a,p),db=distance2D(p,b);
    const Vec3 in=da>.001f?Vec3{(p.x-a.x)/da,(p.y-a.y)/da,0}:Vec3{};
    const Vec3 out=db>.001f?Vec3{(b.x-p.x)/db,(b.y-p.y)/db,0}:Vec3{};
    Vec3 tangent{in.x+out.x,in.y+out.y,0};float len=std::hypot(tangent.x,tangent.y);
    if(len<.001f){tangent=db>.001f?out:in;len=std::max(.001f,std::hypot(tangent.x,tangent.y));}
    const float x=-tangent.y/len,y=tangent.x/len;
    const Vec3 edge=db>.001f?out:in;
    const float half=width*.5f/std::max(.6f,std::fabs(x*(-edge.y)+y*edge.x));
    // Positive (left) bends raise the right/outside edge, and vice versa.
    const float rise=bankHeight*std::clamp(std::fabs(bank),0.0f,1.0f);
    return {{{p.x+x*half,p.y+y*half,p.z+(bank<0?rise:0)},
             {p.x-x*half,p.y-y*half,p.z+(bank>0?rise:0)}}};
}
struct RoadLineViewBend {
    float strength=0; // Zero preserves the original banked ribbon exactly.
    float maxDegrees=75;
    float response=1;
};
// Roll the complete cross-section around its road tangent, toward the viewer.
// Shared world-space sections join adjacent quads. A continuous signed response
// avoids choosing/flipping an edge abruptly as the camera crosses the road axis.
inline std::array<Vec3,2> roadLineViewSection(std::array<Vec3,2> section,Vec3 tangent,Vec3 viewForward,Vec3 /*camera*/,Vec3 /*anchor*/,RoadLineViewBend config){
    if(!std::isfinite(config.strength)||config.strength<=0)return section;
    const float length=std::hypot(tangent.x,tangent.y),viewLength=std::hypot(viewForward.x,viewForward.y);
    if(length<.001f||viewLength<.001f)return section;
    tangent={tangent.x/length,tangent.y/length,0};
    const Vec3 side{-tangent.y,tangent.x,0};
    const float alignment=std::clamp((tangent.x*viewForward.x+tangent.y*viewForward.y)/viewLength,-1.0f,1.0f);
    const float response=std::isfinite(config.response)?std::clamp(config.response,.25f,4.0f):1;
    const float amount=std::pow(std::clamp(std::acos(alignment)/1.57079632679f,0.0f,1.0f),response);
    // Use the common viewing direction, not a separate camera-to-point ray.
    // The latter approaches the leg's tangent at distant sections and flattens
    // a long perpendicular leg even though its road heading never changes.
    const float lateral=-(viewForward.x*side.x+viewForward.y*side.y)/viewLength;
    const float sign=std::clamp(lateral/.2f,-1.0f,1.0f);
    const float maxDegrees=std::isfinite(config.maxDegrees)?std::clamp(config.maxDegrees,0.0f,85.0f):75;
    const float angle=-sign*amount*std::clamp(config.strength,0.0f,1.0f)*maxDegrees*.01745329252f;
    const float c=std::cos(angle),s=std::sin(angle);
    const Vec3 center{(section[0].x+section[1].x)*.5f,(section[0].y+section[1].y)*.5f,(section[0].z+section[1].z)*.5f};
    const float floor=std::min(section[0].z,section[1].z);
    for(auto& point:section){const float across=(point.x-center.x)*side.x+(point.y-center.y)*side.y,z=point.z-center.z;
        const float rotated=across*c-z*s;
        point={point.x+side.x*(rotated-across),point.y+side.y*(rotated-across),center.z+across*s+z*c};}
    const float lift=std::max(0.0f,floor-std::min(section[0].z,section[1].z));
    for(auto& point:section)point.z+=lift;
    return section;
}
inline std::array<Vec3,4> roadLineQuad(Vec3 a,Vec3 b,float width){
    const float length=distance2D(a,b);if(length<.001f)return {{a,a,b,b}};
    const float x=-(b.y-a.y)/length*width*.5f,y=(b.x-a.x)/length*width*.5f;
    return {{{a.x+x,a.y+y,a.z},{a.x-x,a.y-y,a.z},{b.x-x,b.y-y,b.z},{b.x+x,b.y+y,b.z}}};
}
inline std::array<Vec3,2> roadLineRenderSection(const RoadLine& line,const std::vector<float>& lift,std::size_t n,float width,Vec3 forward,Vec3 camera,RoadLineViewBend config,bool fitted){
    auto section=roadLineSection(line.points,n,width,std::copysign(1.0f,lift[n]),std::fabs(lift[n]));
    if(fitted)for(unsigned edge=0;edge<2;++edge)section[edge].z+=line.edges[n][edge]-line.points[n].z;
    if(config.strength>0){const auto before=line.points[n?n-1:n],after=line.points[std::min(n+1,line.points.size()-1)];
        section=roadLineViewSection(section,{after.x-before.x,after.y-before.y,0},forward,camera,line.points[n],config);}
    return section;
}
inline void roadLineRenderSections(const RoadLine& line,const std::vector<float>& lift,float width,Vec3 forward,Vec3 camera,RoadLineViewBend config,std::vector<std::array<Vec3,2>>& out){
    out.resize(line.points.size());const bool fitted=line.edges.size()>=line.points.size();
    for(std::size_t n=0;n<line.points.size();++n)out[n]=roadLineRenderSection(line,lift,n,width,forward,camera,config,fitted);
}
inline float roadDepth(Vec3 point,const CameraFrame& camera){
    constexpr float radians=.01745329252f;const float p=camera.rotation.x*radians,y=camera.rotation.z*radians;
    return (point.x-camera.position.x)*(-std::sin(y)*std::cos(p))+(point.y-camera.position.y)*std::cos(y)*std::cos(p)+(point.z-camera.position.z)*std::sin(p);
}
// Same logarithmic depth convention used by the scene-depth occlusion shader.
inline bool roadLineVisible(float viewDepth,float logDepth,float nearClip,float farClip){
    const float scene=std::pow(farClip/nearClip,std::clamp(logDepth,0.0f,1.0f))*nearClip;
    return viewDepth<=scene+.08f+viewDepth*.0004f;
}
inline float roadLineCoverage(float viewDepth,float logDepth,float nearClip,float farClip){
    const float scene=std::pow(farClip/nearClip,std::clamp(logDepth,0.0f,1.0f))*nearClip;
    const float bias=.08f+viewDepth*.0004f,feather=.12f+viewDepth*.0006f;
    return std::clamp((scene+bias-viewDepth)/feather,0.0f,1.0f);
}
inline float roadDeviceDepth(float depth,float nearClip,float farClip){
    return std::clamp(farClip/(farClip-nearClip)-farClip*nearClip/((farClip-nearClip)*std::max(depth,nearClip)),0.0f,1.0f);
}
}
