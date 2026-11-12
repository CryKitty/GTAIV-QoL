#pragma once
#include "../../../src/compass_notifications.hpp"
#include "../../scripthook/GameTypes.h"
#include "../../minhook/include/MinHook.h"
#include <windows.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <mutex>

// Observe this one phone native; preserve the full original context and call.
// No shared HUD visibility, radar callback or phone script state is changed.
class PhoneNotifications {
public:
    using Native=void(__cdecl*)(GameTypes::scrNativeCallContext*);
private:
    inline static std::atomic<PhoneNotifications*> owner_{nullptr};
    inline static Native original_=nullptr,originalPhone_=nullptr,originalDestroy_=nullptr;
    void* target_=nullptr;void* phoneTarget_=nullptr;void* destroyTarget_=nullptr;
    bool phoneInUse_=false;
    mutable std::mutex mutex_;
    compass::MessageNotice notice_{};
    unsigned updates_=0;
    template<class T> static bool read(std::uintptr_t address,T& value){SIZE_T bytes=0;return address>=0x10000&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),&value,sizeof(value),&bytes)&&bytes==sizeof(value);}
    static void __cdecl waiting(GameTypes::scrNativeCallContext* context){
        std::array<std::uint32_t,3> header{};std::uint32_t unread=0;
        const bool valid=read(reinterpret_cast<std::uintptr_t>(context),header)&&header[1]>=1&&header[1]<=32&&read(header[2],unread)&&unread<=1;
        // Read inputs before forwarding: some native contexts alias arguments
        // and return storage. Never alter the native's result or arguments.
        if(original_)original_(context);
        if(valid)if(auto* owner=owner_.load(std::memory_order_acquire)){
            std::lock_guard<std::mutex> lock(owner->mutex_);owner->notice_.update(unread!=0,GetTickCount());++owner->updates_;
        }
    }
    static void __cdecl phoneCreated(GameTypes::scrNativeCallContext* context){
        if(originalPhone_)originalPhone_(context);
        if(auto* owner=owner_.load(std::memory_order_acquire)){std::lock_guard<std::mutex> lock(owner->mutex_);owner->phoneInUse_=true;}
    }
    static void __cdecl phoneDestroyed(GameTypes::scrNativeCallContext* context){
        if(originalDestroy_)originalDestroy_(context);
        if(auto* owner=owner_.load(std::memory_order_acquire)){std::lock_guard<std::mutex> lock(owner->mutex_);owner->phoneInUse_=false;}
    }

public:
    bool active()const{return target_!=nullptr;}
    bool phoneInUse()const{std::lock_guard<std::mutex> lock(mutex_);return phoneInUse_;}
    compass::MessageNotice snapshot()const{std::lock_guard<std::mutex> lock(mutex_);return notice_;}
    unsigned updates()const{std::lock_guard<std::mutex> lock(mutex_);return updates_;}
    void start(void(*log)(const char*),void* testNative=nullptr,void* testPhone=nullptr,void* testDestroy=nullptr){
        if(target_)return;
        const auto module=GetModuleHandleA("ScriptHook.dll");
        using Names=void*(__cdecl*)(const char*);using Hashes=void*(__cdecl*)(std::uint32_t);
        const auto names=module?reinterpret_cast<Names>(GetProcAddress(module,"?GetNativeAddress@Game@@SAPAXPBD@Z")):nullptr;
        const auto hashes=module?reinterpret_cast<Hashes>(GetProcAddress(module,"?GetNativeAddressFromHash@Game@@SAPAXI@Z")):nullptr;
        void* address=testNative?testNative:(names?names("SET_MESSAGES_WAITING"):nullptr);
        if(!address&&hashes)address=hashes(0x7DC061F5u);
        const auto initialized=MH_Initialize();
        if(address&&(initialized==MH_OK||initialized==MH_ERROR_ALREADY_INITIALIZED)){
            owner_.store(this,std::memory_order_release);
            if(MH_CreateHook(address,reinterpret_cast<void*>(&waiting),reinterpret_cast<void**>(&original_))==MH_OK){
                if(MH_EnableHook(address)==MH_OK)target_=address;
                else MH_RemoveHook(address);
            }
            if(!target_){owner_.store(nullptr,std::memory_order_release);original_=nullptr;}
        }
        // The actual handset lifecycle covers SMS menus as well as calls.
        // SCRIPT_IS_USING_MOBILE_PHONE is only set by the calling script.
        const auto installPhone=[&](void* supplied,const char* name,void* detour,Native& original,void*& target){
            void* address=supplied?supplied:(!testNative&&names?names(name):nullptr);
            if(target_&&address&&MH_CreateHook(address,detour,reinterpret_cast<void**>(&original))==MH_OK){
                if(MH_EnableHook(address)==MH_OK)target=address;else MH_RemoveHook(address);
            }
        };
        installPhone(testPhone,"CREATE_MOBILE_PHONE",reinterpret_cast<void*>(&phoneCreated),originalPhone_,phoneTarget_);
        installPhone(testDestroy,"DESTROY_MOBILE_PHONE",reinterpret_cast<void*>(&phoneDestroyed),originalDestroy_,destroyTarget_);
        if(!phoneTarget_||!destroyTarget_){
            // Never leave a create-only observer that could keep input blocked
            // after the handset closes. This pair is installed atomically.
            for(auto target:{phoneTarget_,destroyTarget_})if(target){MH_DisableHook(target);MH_RemoveHook(target);}
            phoneTarget_=destroyTarget_=nullptr;originalPhone_=originalDestroy_=nullptr;
        }
        if(log){char line[100];std::snprintf(line,sizeof(line),"Compass messages: phone-lifecycle observers create=%u destroy=%u",phoneTarget_?1u:0u,destroyTarget_?1u:0u);log(line);}
        if(log)log(target_?"Compass messages: unread-text observer active; native phone calls forwarded unchanged":"Compass messages: SET_MESSAGES_WAITING observer unavailable");
    }
    void stop(){
        for(auto target:{phoneTarget_,destroyTarget_})if(target){MH_DisableHook(target);MH_RemoveHook(target);}phoneTarget_=destroyTarget_=nullptr;originalPhone_=originalDestroy_=nullptr;
        if(target_){MH_DisableHook(target_);MH_RemoveHook(target_);target_=nullptr;}
        owner_.store(nullptr,std::memory_order_release);original_=nullptr;
        std::lock_guard<std::mutex> lock(mutex_);notice_={};updates_=0;phoneInUse_=false;
    }
};
