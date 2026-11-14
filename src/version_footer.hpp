#pragma once
#include <cstdint>
#include <algorithm>
#include "version_footer_text.hpp"
#include "qol_version.hpp"
namespace versionfooter {
void* continuation=nullptr;
inline bool readText(const wchar_t* source,std::wstring& text){
    text.clear();
    while(text.size()<4096){
        const auto at=source+text.size();MEMORY_BASIC_INFORMATION region{};
        if(!VirtualQuery(at,&region,sizeof(region))||region.State!=MEM_COMMIT||(region.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
        const auto available=(reinterpret_cast<std::uintptr_t>(region.BaseAddress)+region.RegionSize-reinterpret_cast<std::uintptr_t>(at))/sizeof(wchar_t);
        const auto count=std::min<std::size_t>(std::min<std::size_t>(available,256),4096-text.size());
        wchar_t part[256]{};if(!count||!Read(at,part,count*sizeof(wchar_t)))return false;
        for(std::size_t i=0;i<count;++i){if(!part[i])return true;text+=part[i];}
    }
    return false;
}
const wchar_t* __cdecl Extend(const wchar_t* source){
    try{
        std::wstring text;if(!source||!readText(source,text))return source;
        static thread_local std::wstring extra;
        extra=qolfooter::extend(text,QOL_VERSION_WIDE,QOL_BUILD_WIDE);
        if(extra==text)return source;
        // Only remove identity from the QoL row after a real native footer
        // was extended. ExtraInfo Off/missing hooks retain PR #14's fallback.
        using Seen=void(__cdecl*)();const auto core=GetModuleHandleA("GTAIV-QoL.asi");
        const auto seen=core?reinterpret_cast<Seen>(GetProcAddress(core,"QoLSettings_FooterSeen")):nullptr;
        if(seen)seen();return extra.c_str();
    }catch(...){return source;}
}
// At PUSH -1 / PUSH -1 / PUSH EAX, after FusionFix's mid-hook has replaced EAX
// with its complete footer. Its original renderer draws the extended string.
__declspec(naked) void DrawFooter(){
    __asm {
        pushfd
        pushad
        mov ebp,esp
        sub esp,528
        and esp,0fffffff0h
        fxsave [esp]
        cld
        push dword ptr [ebp+28]
        call Extend
        add esp,4
        mov dword ptr [ebp+28],eax
        fxrstor [esp]
        mov esp,ebp
        popad
        popfd
        jmp dword ptr [continuation]
    }
}
bool Attach(void* site){
    const auto init=MH_Initialize();if(init!=MH_OK&&init!=MH_ERROR_ALREADY_INITIALIZED)return false;
    if(MH_CreateHook(site,reinterpret_cast<void*>(&DrawFooter),&continuation)!=MH_OK)return false;
    if(MH_EnableHook(site)!=MH_OK){MH_RemoveHook(site);continuation=nullptr;return false;}return true;
}
void Install(){
    // Strict CE executable gate is applied by the companion before this call.
    // Search after ExtraInfo's mid-hook so asynchronous FusionFix installation
    // and either hook order leave both hooks' instruction ranges disjoint.
    const unsigned char draw[]={0x6a,0xff,0x6a,0xff,0x50,0x83,0xec,0x08,0xf3,0x0f,0x11,0x44,0x24,0,0xf3,0x0f,0x10,0x44,0x24,0,0xf3,0x0f,0x11,0x04,0x24,0xe8,0,0,0,0,0x83,0xc4,0x14};
    const auto site=menuvalue::Unique(draw,"xxxxxxxxxxxxx?xxxxx?xxxxxx????xxx",sizeof(draw));
    if(!site||!Attach(site)){Log("native version footer extension unavailable; QoL row keeps build identity");return;}
    Log("native Game footer extension ready; waiting for FusionFix version text");
}
}
