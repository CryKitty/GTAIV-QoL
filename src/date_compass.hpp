#pragma once
#include <array>
#include <cstdint>
#include <cmath>
#include <cstring>
#include "../vendor/roadnav/src/marker_geometry.hpp"
namespace datecompass {
constexpr unsigned venueCount=29,argumentWords=64,venueGlobal=32995,recordWords=29;
struct Preferences {bool valid=false;unsigned girlfriend=0,ped=0;std::array<int,venueCount> scores{};};
// Actual go_on_date argument layout: ped, flags, girlfriend ID, ...,
// at byte 56 an array count followed by 29 per-place preference scores.
inline Preferences preferences(const std::array<std::uint32_t,argumentWords>& words){
    Preferences result;if(!words[0]||words[2]>4||words[14]!=venueCount)return result;
    for(unsigned i=0;i<venueCount;++i){if(words[15+i]>100)return result;result.scores[i]=static_cast<int>(words[15+i]);}
    result.valid=true;result.ped=words[0];result.girlfriend=words[2];return result;
}
struct Session {
    Preferences value{};std::uint32_t captured=0;bool seen=false;
    void begin(Preferences prefs,std::uint32_t now){value=prefs;captured=now;seen=false;}
    Preferences current(bool active,std::uint32_t now){
        if(active)seen=true;else if(seen||now-captured>10000){value={};seen=false;}
        return active?value:Preferences{};
    }
};
struct Venue {unsigned sprite=0;roadnav::Vec3 point{};};
using Table=std::array<std::uint32_t,1+venueCount*recordWords>;
inline bool venues(const Table& words,std::array<Venue,venueCount>& out){
    if(words[0]!=venueCount)return false;
    std::array<Venue,venueCount> parsed{};
    for(unsigned i=0;i<venueCount;++i){const auto* row=words.data()+1+i*recordWords;
        const auto* name=reinterpret_cast<const unsigned char*>(row);bool ended=false;
        for(unsigned c=0;c<32;++c){if(!name[c]){ended=c>0;break;}if(name[c]<32||name[c]>126)return false;}
        if(!ended||row[8]>=128||row[9]>9)return false;
        parsed[i].sprite=row[8];std::array<float,3> xyz{};std::memcpy(xyz.data(),row+10,sizeof(xyz));
        parsed[i].point={xyz[0],xyz[1],xyz[2]};
        const auto p=parsed[i].point;
        if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)||std::fabs(p.x)>10000||std::fabs(p.y)>10000||std::fabs(p.z)>2000||(p.x==0&&p.y==0))return false;
    }
    out=parsed;return true;
}
enum class Preference {Unknown,Neutral,Liked,Disliked};
// go_on_date::sub_52612 rejects scores below 35. Dating scripts use 50
// as their ordinary baseline; reserve green for preferences above that
// baseline and show accepted baseline/weak scores (35..50) as neutral white.
inline Preference preference(int score){return score<0||score>100?Preference::Unknown:score<35?Preference::Disliked:score<=50?Preference::Neutral:Preference::Liked;}
inline unsigned colour(Preference value){return value==Preference::Liked?8u:value==Preference::Disliked?9u:0xffffffffu;}
inline Preference match(const Preferences& prefs,const std::array<Venue,venueCount>& places,bool tableValid,unsigned sprite,roadnav::Vec3 point){
    if(!prefs.valid||!tableValid)return Preference::Unknown;
    int found=-1;
    for(unsigned i=0;i<venueCount;++i)if(places[i].sprite==sprite&&roadnav::distance2D(places[i].point,point)<35&&std::fabs(places[i].point.z-point.z)<12){
        if(found>=0)return Preference::Unknown; // Ambiguous shared locations stay white.
        found=static_cast<int>(i);
    }
    return found<0?Preference::Unknown:preference(prefs.scores[static_cast<unsigned>(found)]);
}
}
