#pragma once
#include "radar_fill.hpp"
#include "../../minhook/include/MinHook.h"
#include "../../minhook/src/hde/hde32.h"
#include <windows.h>
#include <array>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

// IV-SDK identifies CRadar::DrawFrameFront/Back as void cdecl callbacks.
// Resolve them through their own radar sprites and deferred callback references.
// Suppress the border callbacks and the separate radar-owned visual fill.
// No shared sprite primitive, texture-selection, HUD query or phase is hooked.
class MinimapRing {
    using Frame=void(__cdecl*)();
    inline static std::array<Frame,3> original_{};
    inline static std::atomic<MinimapRing*> owner_{nullptr};
    std::uintptr_t frontend_=0;
    std::array<void*,3> targets_{};
    bool attempted_=false,installed_=false;
    std::atomic<unsigned> calls_{0},hidden_{0},fillCalls_{0},fillHidden_{0};
    const char* status_="not initialized";
    template<class T> static bool read(std::uintptr_t p,T& value){SIZE_T n=0;return p>=0x10000&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(p),&value,sizeof(value),&n)&&n==sizeof(value);}
    static bool omit(unsigned kind){
        auto* owner=owner_.load(std::memory_order_acquire);unsigned char frontend=1;
        if(!owner)return false;
        owner->calls_.fetch_add(1,std::memory_order_relaxed);
        if(kind==2)owner->fillCalls_.fetch_add(1,std::memory_order_relaxed);
        if(!read(owner->frontend_,frontend)||frontend)return false;
        owner->hidden_.fetch_add(1,std::memory_order_relaxed);
        if(kind==2)owner->fillHidden_.fetch_add(1,std::memory_order_relaxed);return true;
    }
    static void __cdecl front(){if(!omit(0))original_[0]();}
    static void __cdecl back(){if(!omit(1))original_[1]();}
    static void __cdecl fill(){if(!omit(2))original_[2]();}
    struct Section {std::uintptr_t base;std::vector<unsigned char> code;};
    static std::uintptr_t callTarget(std::uintptr_t at,const unsigned char* code){std::int32_t relative=0;std::memcpy(&relative,code+1,4);return static_cast<std::uintptr_t>(static_cast<std::int64_t>(at+5)+relative);}
    // A callback entry must have compiler padding, decode cleanly through its
    // exact MOV ECX,ring / CALL SetRenderState, and end with a void cdecl RET.
    // A separate PUSH entry reference proves that the engine queues this routine
    // as a callback; a random nearby function/prologue is not sufficient.
    static std::uintptr_t frame(const Section& section,std::size_t use,const std::vector<Section>& sections){
        const auto& code=section.code;
        const auto lower=use>2048?use-2048:0;
        for(std::size_t start=use;start>lower;){--start;
            if(((section.base+start)&15)!=0||start<2||code[start-1]!=0xcc||code[start-2]!=0xcc)continue;
            const auto entry=section.base+start;bool queued=false;
            for(const auto& text:sections)for(std::size_t i=0;i+5<=text.code.size();++i)if(text.code[i]==0x68){
                std::uint32_t operand=0;std::memcpy(&operand,text.code.data()+i+1,4);
                if(operand==entry){queued=true;break;}
            }
            if(!queued)continue;
            const auto limit=std::min(code.size(),start+4096);
            std::vector<unsigned char> visited(limit-start);std::vector<std::size_t> pending{start};
            bool reached=false,returned=false,valid=true;
            while(!pending.empty()&&valid){auto at=pending.back();pending.pop_back();
                while(at>=start&&at+16<=limit&&!visited[at-start]){
                    visited[at-start]=1;hde32s instruction{};
                    const auto length=hde32_disasm(code.data()+at,&instruction);
                    if(!length||(instruction.flags&F_ERROR)||code[at]==0xcc){valid=false;break;}
                    if(at==use)reached=true;
                    if(instruction.opcode==0xc2){valid=false;break;}
                    if(instruction.opcode==0xc3){returned=true;break;}
                    const bool jump=instruction.opcode==0xe9||instruction.opcode==0xeb;
                    const bool conditional=(instruction.opcode>=0x70&&instruction.opcode<=0x7f)||(instruction.opcode==0x0f&&instruction.opcode2>=0x80&&instruction.opcode2<=0x8f)||(instruction.opcode>=0xe0&&instruction.opcode<=0xe3);
                    if(instruction.opcode==0xff&&instruction.modrm_reg==4){valid=false;break;}
                    if(jump||conditional){
                        const auto offset=(instruction.flags&F_IMM8)?static_cast<std::int32_t>(static_cast<std::int8_t>(instruction.imm.imm8)):static_cast<std::int32_t>(instruction.imm.imm32);
                        const auto destination=static_cast<std::int64_t>(at+length)+offset;
                        if(destination<static_cast<std::int64_t>(start)||destination+16>static_cast<std::int64_t>(limit)){valid=false;break;}
                        pending.push_back(static_cast<std::size_t>(destination));
                        if(jump)break;
                    }
                    at+=length;
                    if(at+16>limit)valid=false;
                }
            }
            if(valid&&reached&&returned)return entry;
        }
        return 0;
    }
public:
    const char* status()const{return status_;}
    unsigned hidden()const{return hidden_.load(std::memory_order_relaxed);}
    unsigned selections()const{return calls_.load(std::memory_order_relaxed);}
    bool active()const{return installed_;}
    unsigned fillHidden()const{return fillHidden_.load(std::memory_order_relaxed);}
    unsigned fillCalls()const{return fillCalls_.load(std::memory_order_relaxed);}
    void release(){
        for(auto* target:targets_)if(target){MH_DisableHook(target);MH_RemoveHook(target);}
        targets_={};owner_.store(nullptr,std::memory_order_release);
        installed_=false;attempted_=false;status_="released";
    }
    void enable(std::uintptr_t frontend,void(*log)(const char*),std::uintptr_t testImage=0){
        if(attempted_)return;attempted_=true;frontend_=frontend;
        const auto base=testImage?testImage:reinterpret_cast<std::uintptr_t>(GetModuleHandleA(nullptr));
        IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS32 nt{};
        if(!frontend_||!read(base,dos)||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<=0||dos.e_lfanew>4096||!read(base+dos.e_lfanew,nt)||nt.Signature!=IMAGE_NT_SIGNATURE||nt.FileHeader.NumberOfSections>32){status_="image unavailable";return;}
        const auto size=nt.OptionalHeader.SizeOfImage;
        const auto inImage=[&](std::uint32_t p,std::size_t bytes){return p>=base&&p-base<size&&bytes<=size-(p-base);};
        const auto headers=base+dos.e_lfanew+offsetof(IMAGE_NT_HEADERS32,OptionalHeader)+nt.FileHeader.SizeOfOptionalHeader;
        std::vector<Section> sections;
        for(unsigned n=0;n<nt.FileHeader.NumberOfSections;++n){IMAGE_SECTION_HEADER section{};
            if(!read(headers+n*sizeof(section),section)||!(section.Characteristics&IMAGE_SCN_MEM_EXECUTE)||section.VirtualAddress>=size||section.Misc.VirtualSize>size-section.VirtualAddress||section.Misc.VirtualSize>32*1024*1024)continue;
            Section text{base+section.VirtualAddress,std::vector<unsigned char>(section.Misc.VirtualSize)};SIZE_T bytes=0;
            if(ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(text.base),text.code.data(),text.code.size(),&bytes)&&bytes==text.code.size())sections.push_back(std::move(text));
        }
        std::array<std::vector<std::uintptr_t>,2> sprites,fallbacks,frames;
        std::vector<std::uintptr_t> states;
        const auto add=[](auto& values,std::uintptr_t value){if(std::find(values.begin(),values.end(),value)==values.end())values.push_back(value);};
        for(const auto& text:sections)for(std::size_t i=0;i+31<=text.code.size();++i){const auto* at=text.code.data()+i;
            const unsigned char state[]={0x56,0x8b,0xf1,0x8b,0x0d};
            if(!std::memcmp(at,state,sizeof(state))&&at[9]==0xff&&at[10]==0x36)add(states,text.base+i);
            if(((at[0]==0x68&&at[5]==0xb9)||(at[0]==0xb9&&at[5]==0x68))&&at[10]==0xe8){
                std::uint32_t name=0,sprite=0;std::memcpy(&name,at+(at[0]==0x68?1:6),4);std::memcpy(&sprite,at+(at[0]==0xb9?1:6),4);
                std::array<char,15> nameText{};
                if(inImage(name,nameText.size())&&inImage(sprite,4)&&read(name,nameText)){
                    if(!std::strcmp(nameText.data(),"radarringfront"))add(sprites[0],sprite);
                    if(!std::strcmp(nameText.data(),"radarringback"))add(sprites[1],sprite);
                }
            }
            // Plugin-SDK's exact ring-specific deletion references.
            if(at[0]==0xb9&&at[5]==0xe8&&at[10]==0xe8&&at[15]==0x5f&&at[16]==0x5e&&at[17]==0xc7&&at[18]==5){std::uint32_t sprite=0;std::memcpy(&sprite,at+1,4);if(inImage(sprite,4))add(fallbacks[1],sprite);}
            if(at[0]==0xb9&&at[5]==0xe8&&at[10]==0x68&&at[15]==0xb9&&at[20]==0xe8&&at[25]==0xe8&&at[30]==0x5f){std::uint32_t sprite=0;std::memcpy(&sprite,at+1,4);if(inImage(sprite,4))add(fallbacks[0],sprite);}
        }
        for(unsigned n=0;n<2;++n)if(sprites[n].empty()&&fallbacks[n].size()==1)sprites[n]=fallbacks[n];
        if(states.size()==1&&sprites[0].size()==1&&sprites[1].size()==1&&sprites[0][0]!=sprites[1][0]){
            for(const auto& text:sections)for(std::size_t i=0;i+10<=text.code.size();++i){const auto* at=text.code.data()+i;
                if(at[0]!=0xb9||at[5]!=0xe8||callTarget(text.base+i+5,at+5)!=states[0])continue;
                std::uint32_t sprite=0;std::memcpy(&sprite,at+1,4);
                for(unsigned n=0;n<2;++n)if(sprite==sprites[n][0]){const auto entry=frame(text,i,sections);if(entry)add(frames[n],entry);}
            }
        }
        if(frames[0].size()!=1||frames[1].size()!=1||frames[0][0]==frames[1][0]){
            status_="frame callbacks unavailable";
            if(log){char line[200];std::snprintf(line,sizeof(line),"Compass minimap: frame signatures sprites=%u,%u state=%u callbacks=%u,%u; shared draw functions untouched",static_cast<unsigned>(sprites[0].size()),static_cast<unsigned>(sprites[1].size()),static_cast<unsigned>(states.size()),static_cast<unsigned>(frames[0].size()),static_cast<unsigned>(frames[1].size()));log(line);}return;
        }
        // The fill is a separate queued visual callback. Identify it by the
        // queue owner containing both borders and its no-args queue constructor,
        // preserving GPS updates and common render-state callbacks in that phase.
        std::vector<radarfill::Section> image;
        for(unsigned n=0;n<nt.FileHeader.NumberOfSections;++n){IMAGE_SECTION_HEADER section{};
            if(!read(headers+n*sizeof(section),section)||!(section.Characteristics&IMAGE_SCN_MEM_READ)||section.VirtualAddress>=size||section.Misc.VirtualSize>size-section.VirtualAddress||section.Misc.VirtualSize>32*1024*1024)continue;
            radarfill::Section part{base+section.VirtualAddress,std::vector<unsigned char>(section.Misc.VirtualSize),(section.Characteristics&IMAGE_SCN_MEM_EXECUTE)!=0};SIZE_T bytes=0;
            if(ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(part.base),part.bytes.data(),part.bytes.size(),&bytes)&&bytes==part.bytes.size())image.push_back(std::move(part));
        }
        const auto fillTarget=radarfill::resolve(image,frames[0][0],frames[1][0],log);
        if(log){char line[220];std::snprintf(line,sizeof(line),"Compass minimap: typed radar queue phase=%08x slot=%u visual-fill=%08x candidates=%u",static_cast<unsigned>(fillTarget.phase),fillTarget.slot,static_cast<unsigned>(fillTarget.callback),fillTarget.candidates);log(line);}
        const auto init=MH_Initialize();if(init!=MH_OK&&init!=MH_ERROR_ALREADY_INITIALIZED){status_="frame hook initialization failed";return;}
        const std::array<void*,3> detours{{reinterpret_cast<void*>(&front),reinterpret_cast<void*>(&back),reinterpret_cast<void*>(&fill)}};
        const std::array<std::uintptr_t,3> resolved{{frames[0][0],frames[1][0],fillTarget.callback}};
        owner_.store(this,std::memory_order_release);
        for(unsigned n=0;n<3;++n){if(!resolved[n])continue;auto* target=reinterpret_cast<void*>(resolved[n]);
            if(MH_CreateHook(target,detours[n],reinterpret_cast<void**>(&original_[n]))!=MH_OK){release();status_="frame hook unavailable";return;}targets_[n]=target;
        }
        for(auto* target:targets_)if(target&&MH_EnableHook(target)!=MH_OK){release();status_="frame hook unavailable";return;}
        installed_=true;status_=fillTarget.callback?"radar border and visual fill":"radar border only; fill unresolved";
        if(log)log("Compass minimap: radar-specific callbacks filtered; GPS generation and shared HUD rendering preserved");
    }
};
