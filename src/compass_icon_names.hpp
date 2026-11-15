#pragma once
#include <string>
#include <string_view>
namespace compassicons {
inline bool valid(std::string_view name){
    if(name.empty()||name.size()>63)return false;
    for(char c:name)if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'))return false;
    if(name.substr(0,6)=="radar_")return name.size()>6;
    constexpr std::string_view prefix="episode_blip_";
    if(name.substr(0,prefix.size())!=prefix)return false;
    const auto number=name.substr(prefix.size());
    if(number.empty()||number.size()>2||number[0]=='0')return false;
    unsigned value=0;for(char c:number){if(c<'0'||c>'9')return false;value=value*10+static_cast<unsigned>(c-'0');}
    return value>=1&&value<=15;
}
// IV-SDK eRadarSprite 95..109 and the game's episode taxi texture lookup use
// these exact resource names. They are real textures, never invented initials.
inline const char* episode(unsigned sprite){
    const char* const names[]={"episode_blip_1","episode_blip_2","episode_blip_3","episode_blip_4","episode_blip_5","episode_blip_6","episode_blip_7","episode_blip_8","episode_blip_9","episode_blip_10","episode_blip_11","episode_blip_12","episode_blip_13","episode_blip_14","episode_blip_15"};
    return sprite>=95&&sprite<=109?names[sprite-95]:nullptr;
}
inline std::string resolve(unsigned sprite,const std::string& live){return episode(sprite)?episode(sprite):valid(live)?live:"";}
enum class Draw {Texture=1,Pending=2,Unknown=3};
inline Draw draw(const std::string& name,bool ready){return ready?Draw::Texture:name.empty()?Draw::Unknown:Draw::Pending;}
}
