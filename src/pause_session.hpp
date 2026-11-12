#pragma once
#include <cstdint>
namespace cutscenepause {
struct CinematicClocks {
    int audio=-1,scene=-1;
    bool Hold(int a,int c){audio=a;scene=c;return a>=0&&c>=0;}
    bool Stable(int a,int c)const {
        const auto da=static_cast<std::int64_t>(a)-audio;
        const auto dc=static_cast<std::int64_t>(c)-scene;
        return audio>=0&&scene>=0&&a>=0&&c>=0&&da>=-80&&da<=80&&dc>=-80&&dc<=80;
    }
};
enum class Action { None, Pause, Resume };
struct Session {
    bool owned=false, previous=false;
    Action Observe(bool input,bool active,bool allowed) {
        const bool edge=input&&!previous;previous=input;
        if(owned && (!active||!allowed||edge)) {owned=false;return Action::Resume;}
        if(!owned && active&&allowed&&edge) {owned=true;return Action::Pause;}
        return Action::None;
    }
};
}
