#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>
namespace awareness {
// A manual waypoint near the mission goal can occlude the original compass
// icon. It must retain the same objective label, without relabelling detours.
inline bool policeSprite(unsigned sprite){return sprite==68||sprite==73;}
inline bool objectiveLabel(bool mission,bool enemy,bool entity,unsigned sprite,bool requested,float missionDistance){
    return mission&&!enemy&&!policeSprite(sprite)&&(requested||(!entity&&sprite<=5)||(sprite==8&&missionDistance<=65));
}
inline bool routeRequestDue(unsigned now,unsigned last){return !last||now-last>=12000;}
// Stock taxi.txt uses 40 degrees for the rear cabin, 30 for its destination
// selection camera. Only widen the nearby rear-view range, leaving other views.
inline float rearTaxiFov(float authored,float desired,bool rear,float distance){
    if(!rear||!std::isfinite(authored)||!std::isfinite(distance)||distance>4.5f||distance<0||desired<40||desired>120||authored<35||authored>55)return authored;
    return std::clamp(desired+(authored-40),40.0f,120.0f);
}
enum class RouteKind {Blue,Custom,Mission,Enemy,Escape,Friend};
inline RouteKind destinationRouteKind(bool enemy,bool friendly,bool waypoint){
    return enemy?RouteKind::Enemy:waypoint?RouteKind::Custom:friendly?RouteKind::Friend:RouteKind::Mission;
}
struct LineTypes {
    bool escape=true,mission=true,friendLine=true,enemy=true,custom=true;
    bool visible(bool master,RouteKind kind)const{
        if(!master)return false;
        switch(kind){case RouteKind::Escape:return escape;case RouteKind::Mission:return mission;
            case RouteKind::Enemy:return enemy;case RouteKind::Custom:return custom;default:return friendLine;}
    }
};
inline RouteKind activeRoute(RouteKind matched,bool chase,bool waypointMatches){return chase?RouteKind::Enemy:waypointMatches?RouteKind::Custom:matched;}
inline std::array<unsigned,3> routeColour(RouteKind kind,bool enabled,unsigned now=0){
    if(kind==RouteKind::Escape)return (now/400)%2?std::array<unsigned,3>{35,110,255}:std::array<unsigned,3>{245,45,45};
    if(!enabled)return {25,180,255};
    switch(kind){case RouteKind::Custom:return {85,210,95};case RouteKind::Mission:return {245,205,55};case RouteKind::Enemy:return {235,55,45};default:return {25,180,255};}
}
struct PedPool {std::uint32_t storage,flags,size,stride;};
inline bool validPool(const PedPool& p,unsigned player,unsigned flag){
    return p.storage>=0x10000&&p.flags>=0x10000&&p.size>0&&p.size<=2048&&p.stride>=0x500&&p.stride<=0x4000&&(player>>8)<p.size&&!(flag&0x80)&&flag==(player&255);
}
inline unsigned poolHandle(unsigned slot,unsigned flag){return flag&0x80?0:(slot<<8)|flag;}
inline bool attacker(bool damage,bool combatOrderToPlayer,bool dead,float distance,bool aggressive=true){return !dead&&distance<=180&&aggressive&&(combatOrderToPlayer||damage);}
// Convert GTA colour/control tokens to plain readable text; substitute the
// dynamic ~a~/~1~ fields captured from the original instruction call.
inline std::string briefText(const std::string& raw,const std::vector<std::string>& inserts){
    std::string out;unsigned next=0;
    for(std::size_t i=0;i<raw.size()&&out.size()<480;){
        if(raw[i]=='~'){
            const auto end=raw.find('~',i+1);
            if(end!=std::string::npos&&end-i<32){const auto token=raw.substr(i+1,end-i-1);
                if(token=="n")out+=' ';
                else if(token=="a"||token=="1"){if(next<inserts.size())out+=inserts[next++];}
                i=end+1;continue;
            }
        }
        const unsigned char ch=static_cast<unsigned char>(raw[i++]);if(ch>=32)out+=static_cast<char>(ch);else out+=' ';
    }
    if(out.size()>480)out.resize(480);
    return out;
}
inline std::vector<std::string> briefLines(std::string text){
    std::vector<std::string> lines;
    while(!text.empty()&&lines.size()<4){
        std::size_t end=std::min<std::size_t>(58,text.size());
        if(end<text.size()){const auto space=text.rfind(' ',end);if(space!=std::string::npos&&space>20)end=space;}
        lines.push_back(text.substr(0,end));text.erase(0,end);while(!text.empty()&&text.front()==' ')text.erase(text.begin());
    }
    if(!text.empty())lines.back()+="...";
    return lines;
}
}
