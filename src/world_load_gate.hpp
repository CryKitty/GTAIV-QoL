#pragma once
namespace worldload {
struct State {
    bool fadedIn=false,playing=false,control=false,ped=false;
    bool ready()const{return fadedIn&&playing&&control&&ped;}
};
// ScriptHook/D3D ticks can run during the loading screen. They are not a world
// readiness signal. Latch only after a usable player; reset on script lifecycle.
struct Gate {
    bool entered=false;
    template<class Initialize> bool allow(State state,Initialize initialize){
        if(!entered){if(!state.ready())return false;initialize();entered=true;}
        return true;
    }
    void reset(){entered=false;}
};
}
