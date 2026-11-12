#pragma once
#include <windows.h>
#include <Xinput.h>
#include <array>
#include <atomic>
#include "../../minhook/include/MinHook.h"
#include "phone_notifications.hpp"
// Consume only D-pad Left at the controller boundary. Our own hold detector
// reads the unfiltered trampoline. Right, axes, triggers and packet IDs pass
// unchanged; a live frontend byte releases Left even when scripts are paused.
class CompassInput {
public:
    using Get=DWORD(WINAPI*)(DWORD,XINPUT_STATE*);
private:
    inline static std::array<Get,6> originals_{};
    inline static std::array<void*,6> targets_{};
    inline static std::atomic<bool> reserved_{false};
    inline static std::atomic<DWORD> at_{0};
    inline static std::atomic<std::uintptr_t> frontend_{0};
    inline static std::atomic<PhoneNotifications*> phone_{nullptr};
    inline static DWORD retryAt_=0;
    inline static thread_local bool readingRaw_=false;
    static bool consume(){
        if(readingRaw_||!reserved_.load()||GetTickCount()-at_.load()>250)return false;
        auto* phone=phone_.load();if(!phone||phone->phoneInUse())return false;
        unsigned char frontend=1;SIZE_T bytes=0;const auto address=frontend_.load();
        return address&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),&frontend,1,&bytes)&&bytes==1&&!frontend;
    }
    template<unsigned Index> static DWORD WINAPI state(DWORD index,XINPUT_STATE* output){
        const auto original=originals_[Index];const auto result=original?original(index,output):ERROR_DEVICE_NOT_CONNECTED;
        if(result==ERROR_SUCCESS&&output&&consume())output->Gamepad.wButtons&=static_cast<WORD>(~XINPUT_GAMEPAD_DPAD_LEFT);
        return result;
    }
public:
    static Get raw(Get entry){for(unsigned i=0;i<targets_.size();++i)if(targets_[i]==reinterpret_cast<void*>(entry))return originals_[i];return entry;}
    static DWORD pollRaw(Get entry,DWORD index,XINPUT_STATE* output){
        const bool previous=readingRaw_;readingRaw_=true;
        const auto result=raw(entry)(index,output);readingRaw_=previous;return result;
    }
    static void publish(bool enabled,std::uintptr_t frontend,PhoneNotifications* phone){
        frontend_.store(frontend);phone_.store(phone);at_.store(GetTickCount());reserved_.store(enabled);
    }
    static bool attach(Get entry,unsigned slot){
        if(!entry||slot>=targets_.size())return false;
        for(auto target:targets_)if(target==reinterpret_cast<void*>(entry))return true;
        const auto init=MH_Initialize();if(init!=MH_OK&&init!=MH_ERROR_ALREADY_INITIALIZED)return false;
        const std::array<Get,6> detours{{state<0>,state<1>,state<2>,state<3>,state<4>,state<5>}};
        auto target=reinterpret_cast<void*>(entry);
        if(MH_CreateHook(target,reinterpret_cast<void*>(detours[slot]),reinterpret_cast<void**>(&originals_[slot]))!=MH_OK)return false;
        targets_[slot]=target;
        if(MH_EnableHook(target)==MH_OK)return true;
        targets_[slot]=nullptr;MH_RemoveHook(target);originals_[slot]=nullptr;return false;
    }
    static void start(void(*log)(const char*)){const auto now=GetTickCount();if(retryAt_&&now-retryAt_<2000)return;retryAt_=now;
        const std::array<const char*,3> modules{{"xinput1_3.dll","xinput1_4.dll","xinput9_1_0.dll"}};
        for(unsigned i=0;i<modules.size();++i){const auto module=GetModuleHandleA(modules[i]);if(!module)continue;
            // GTA and controller mods can use either the named entry or the
            // extended-state ordinal. Both share the XINPUT_STATE prefix.
            for(unsigned extended=0;extended<2;++extended){const unsigned slot=i+extended*3;if(targets_[slot])continue;
                auto get=reinterpret_cast<Get>(GetProcAddress(module,extended?MAKEINTRESOURCEA(100):"XInputGetState"));
                bool prior=false;for(auto target:targets_)if(target&&target==reinterpret_cast<void*>(get))prior=true;
                if(get&&!prior&&attach(get,slot)&&log)log("Compass input: D-pad Left reserved for waypoints during gameplay; raw hold input preserved");
            }
        }
    }
    static void stop(){reserved_.store(false);for(unsigned i=0;i<targets_.size();++i)if(targets_[i]){MH_DisableHook(targets_[i]);MH_RemoveHook(targets_[i]);targets_[i]=nullptr;originals_[i]=nullptr;}phone_.store(nullptr);retryAt_=0;}
};
