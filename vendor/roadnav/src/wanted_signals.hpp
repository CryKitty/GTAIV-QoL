#pragma once
#include "../../../src/wanted_compass.hpp"
namespace wantednative {
inline wantedcompass::Signals sample(unsigned player,unsigned stars){
    using Native=void(__cdecl*)(GameTypes::scrNativeCallContext*);
    using Lookup=void*(__cdecl*)(const char*);
    static Native grey=nullptr,flash=nullptr;static bool tried=false;
    if(!tried){tried=true;const auto module=GetModuleHandleA("ScriptHook.dll");
        const auto lookup=module?reinterpret_cast<Lookup>(GetProcAddress(module,"?GetNativeAddress@Game@@SAPAXPBD@Z")):nullptr;
        grey=lookup?reinterpret_cast<Native>(lookup("PLAYER_HAS_GREYED_OUT_STARS")):nullptr;
        flash=lookup?reinterpret_cast<Native>(lookup("PLAYER_HAS_FLASHING_STARS_ABOUT_TO_DROP")):nullptr;}
    if(!grey||!flash)return {};
    if(!stars)return {true,0,false,false};
    NativeContext visibility;visibility.Push(player);grey(&visibility);
    NativeContext countdown;countdown.Push(player);flash(&countdown);
    return {true,stars,visibility.GetResult<unsigned char>()!=0,countdown.GetResult<unsigned char>()!=0};
}
}
