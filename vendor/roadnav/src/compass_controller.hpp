#pragma once
#include <windows.h>
#include <Xinput.h>
#include <initializer_list>
#include "compass_input.hpp"
class CompassController {
public:
    using Get=DWORD(WINAPI*)(DWORD,XINPUT_STATE*);
private:
    Get get_=nullptr;bool tried_=false;DWORD lastTry_=0;
    XINPUT_STATE sample_{};
public:
    explicit CompassController(Get get=nullptr):get_(get),tried_(get!=nullptr){}
    const XINPUT_STATE& sample()const{return sample_;} // Diagnostics only; never sent back to the game.
    bool left(bool nativeFallback,bool& raw){
        if(!get_&&(!tried_||GetTickCount()-lastTry_>=2000)){tried_=true;lastTry_=GetTickCount();for(const char* name:{"xinput1_3.dll","xinput1_4.dll","xinput9_1_0.dll"}){
            const auto module=GetModuleHandleA(name);if(module)get_=reinterpret_cast<Get>(GetProcAddress(module,"XInputGetState"));if(get_)break;
        }}
        raw=false;sample_={};
        if(get_)for(DWORD i=0;i<4;++i){XINPUT_STATE state{};if(CompassInput::pollRaw(get_,i,&state)==ERROR_SUCCESS){raw=true;sample_=state;return (state.Gamepad.wButtons&XINPUT_GAMEPAD_DPAD_LEFT)!=0;}}
        return nativeFallback;
    }
};
