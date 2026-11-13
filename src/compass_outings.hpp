#pragma once
#include "compass.hpp"
namespace compass {
enum class OutingActivity {None,Food,Drink,Pool,Bowling,Darts,Show,Strip,Heli,Boat};
struct OutingVenues {
    bool dating=false,friendActivity=false;
    std::array<bool,128> sprites{};
    bool contains(unsigned sprite)const{return sprite<sprites.size()&&sprites[sprite];}
    void add(OutingActivity activity){
        friendActivity=activity!=OutingActivity::None||friendActivity;
        for(unsigned sprite=0;sprite<sprites.size();++sprite){bool match=false;
            switch(activity){
            case OutingActivity::Food:match=sprite==21||sprite==22||sprite==57;break;
            case OutingActivity::Drink:match=sprite==47;break;
            case OutingActivity::Pool:match=sprite==46;break;
            case OutingActivity::Bowling:match=sprite==49;break;
            case OutingActivity::Darts:match=sprite==52;break;
            case OutingActivity::Show:match=sprite==70||sprite==71;break;
            case OutingActivity::Strip:match=sprite==66;break;
            case OutingActivity::Heli:match=sprite==56;break;
            case OutingActivity::Boat:match=sprite==48;break;
            default:break;
            }
            sprites[sprite]=sprites[sprite]||match;
        }
    }
};
template<class Active> OutingVenues outingVenues(Active active){
    OutingVenues result;
    for(const char* script:{"go_on_date","date_activity","date_food"})if(active(script))result.dating=true;
    if(result.dating)for(unsigned sprite=0;sprite<128;++sprite)result.sprites[sprite]=socialVenue(sprite);
    struct ActivityScript {const char* name;OutingActivity activity;};
    using A=OutingActivity;
    // Exact activity scripts, not resident friend managers or generic hangouts:
    // those cannot identify which venue the current outing requested.
    for(const auto& script:{
        ActivityScript{"romandarts",A::Darts},{"romandrink",A::Drink},{"romaneat",A::Food},{"romanpool",A::Pool},{"romanshow",A::Show},{"romanstrip",A::Strip},{"romantenpin",A::Bowling},
        {"jacobdarts",A::Darts},{"jacobdrink",A::Drink},{"jacobeat",A::Food},{"jacobpool",A::Pool},{"jacobshow",A::Show},{"jacobstrip",A::Strip},
        {"bruciedrink",A::Drink},{"brucieeat",A::Food},{"brucielift",A::Heli},{"brucieshow",A::Show},{"bruciestrip",A::Strip},{"brucietenpin",A::Bowling},
        {"dwaynedrink",A::Drink},{"dwayneeat",A::Food},{"dwayneshow",A::Show},{"dwaynestrip",A::Strip},{"dwaynetenpin",A::Bowling},
        {"packiedarts",A::Darts},{"packiedrink",A::Drink},{"packiepool",A::Pool},{"packieshow",A::Show},{"packiestrip",A::Strip},{"packietenpin",A::Bowling},
        {"jimdarts",A::Darts},{"jimshow",A::Show},{"jimdrink",A::Drink},{"jimeat",A::Food},{"jimpool",A::Pool},{"jimstrip",A::Strip},
        {"claydarts",A::Darts},{"clayshow",A::Show},{"claydrink",A::Drink},{"clayeat",A::Food},{"claypool",A::Pool},{"claystrip",A::Strip},
        {"terrydarts",A::Darts},{"terryshow",A::Show},{"terrydrink",A::Drink},{"terryeat",A::Food},{"terrypool",A::Pool},{"terrystrip",A::Strip}})
        if(active(script.name))result.add(script.activity);
    return result;
}
}
