#pragma once
#include "../../../src/minimap_layout.hpp"
#include "minimap_ring.hpp"
#include "../../minhook/include/MinHook.h"
#include "../../minhook/src/hde/hde32.h"
#include <array>
#include <algorithm>
#include <windows.h>
#include <vector>
#include <atomic>
#include <cstring>
#include <cstdio>
#include <utility>

// No native visibility changes and no Radar Off preference overrides. Keep all
// visibility answers intact; displace only HUD_RADAR's own layout geometry.
class MinimapLayout {
    using Display=bool(__thiscall*)(void*);
    inline static Display original_=nullptr;
    inline static std::atomic<MinimapLayout*> owner_{nullptr};
    std::uintptr_t info_=0,components_=0,frontend_=0,target_=0;
    bool installed_=false,attempted_=false;
    const char* status_="not initialized";
    std::atomic<unsigned> queries_{0};
    struct Saved {std::uintptr_t address=0;minimaplayout::Point point{};};
    Saved layout_{},live_{};
    MinimapRing ring_;
    template<class T> static bool read(std::uintptr_t address,T& value){SIZE_T count=0;return address>=0x10000&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),&value,sizeof(value),&count)&&count==sizeof(value);}
    template<class T> static bool write(std::uintptr_t address,const T& value){SIZE_T count=0;return WriteProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),&value,sizeof(value),&count)&&count==sizeof(value);}
    static void restore(Saved& saved){minimaplayout::Point current{};if(saved.address&&read(saved.address,current)&&minimaplayout::displaced(current))write(saved.address,saved.point);saved={};}
    static void move(std::uintptr_t address,Saved& saved){
        if(saved.address&&saved.address!=address)restore(saved);
        minimaplayout::Point current{};if(!read(address,current)||!minimaplayout::valid(current))return;
        if(!minimaplayout::displaced(current))saved={address,current};
        if(saved.address)write(address,minimaplayout::Point{-10000,-10000});
    }
    void update(void* queried=nullptr){
        unsigned char frontend=1;
        if(!installed_||!read(frontend_,frontend)||frontend!=0){restore(live_);restore(layout_);return;}
        minimaplayout::Info info{};std::uint32_t component=0;
        if(!read(info_+8*sizeof(info),info)||!minimaplayout::radar(info)||!read(components_+info.slot*4,component)||component<0x10000)return;
        if(queried&&reinterpret_cast<std::uintptr_t>(queried)!=component)return;
        if(queried)queries_.fetch_add(1,std::memory_order_relaxed);
        // The map layout and the radar-specific visual callbacks are separate.
        // Keep component alpha/colour and shared native visibility unchanged.
        move(info_+8*sizeof(info)+offsetof(minimaplayout::Info,pos),layout_);
        move(static_cast<std::uintptr_t>(component)+0x18,live_);
    }
    static bool __fastcall displaying(void* component,void*){
        // Always preserve the original answer; the shared query may gate UI
        // work such as wanted stars and notifications elsewhere in the engine.
        const bool result=original_?original_(component):false;
        if(auto* owner=owner_.load(std::memory_order_acquire))owner->update(component);
        return result;
    }
public:
    bool active()const{return installed_;}
    std::uintptr_t frontendAddress()const{return frontend_;}
    unsigned count()const{return queries_.load(std::memory_order_relaxed);}
    const char* status()const{return status_;}
    const char* ringStatus()const{return ring_.status();}
    unsigned ringHidden()const{return ring_.hidden();}
    unsigned ringSelections()const{return ring_.selections();}
    unsigned fillCalls()const{return ring_.fillCalls();}
    unsigned fillHidden()const{return ring_.fillHidden();}
    void release(){
        ring_.release();
        if(installed_){MH_DisableHook(reinterpret_cast<void*>(target_));MH_RemoveHook(reinterpret_cast<void*>(target_));}
        owner_.store(nullptr,std::memory_order_release);original_=nullptr;installed_=false;
        restore(live_);restore(layout_);attempted_=false;status_="released";
    }
    void enable(void(*log)(const char*),std::uintptr_t testImage=0,std::uintptr_t testWanted=0){
        if(installed_){update();return;}
        if(attempted_)return;attempted_=true;
        const auto base=testImage?testImage:reinterpret_cast<std::uintptr_t>(GetModuleHandleA(nullptr));
        IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS32 nt{};
        if(!read(base,dos)||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<=0||dos.e_lfanew>4096||!read(base+dos.e_lfanew,nt)||nt.Signature!=IMAGE_NT_SIGNATURE||nt.FileHeader.NumberOfSections>32)return;
        const auto sectionBase=base+dos.e_lfanew+offsetof(IMAGE_NT_HEADERS32,OptionalHeader)+nt.FileHeader.SizeOfOptionalHeader;
        unsigned foundInfo=0,foundComponents=0,foundFrontend=0,foundDisplay=0;
        for(unsigned n=0;n<nt.FileHeader.NumberOfSections;++n){IMAGE_SECTION_HEADER section{};
            if(!read(sectionBase+n*sizeof(section),section)||!(section.Characteristics&IMAGE_SCN_MEM_EXECUTE)||section.VirtualAddress>=nt.OptionalHeader.SizeOfImage||section.Misc.VirtualSize>nt.OptionalHeader.SizeOfImage-section.VirtualAddress||section.Misc.VirtualSize>32*1024*1024)continue;
            std::vector<unsigned char> code(section.Misc.VirtualSize);SIZE_T bytes=0;if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(base+section.VirtualAddress),code.data(),code.size(),&bytes)||bytes!=code.size())continue;
            const auto inImage=[&](std::uint32_t address,std::size_t size){return address>=base&&address-base<nt.OptionalHeader.SizeOfImage&&size<=nt.OptionalHeader.SizeOfImage-(address-base);};
            for(std::size_t i=0;i+16<=code.size();++i){const auto* at=code.data()+i;std::uint32_t operand=0;
                const auto frontend=minimaplayout::frontend(at,code.size()-i);
                if(inImage(frontend,1)){frontend_=frontend;++foundFrontend;}
                const unsigned char display[]={0x80,0x79,0x11,0,0x0f,0x95,0xc0,0xc3};
                if(!std::memcmp(at,display,sizeof(display))){target_=base+section.VirtualAddress+i;++foundDisplay;}
                const unsigned char infoTail[]={0xeb,6,0x8d,0x9b,0,0,0,0,0x8b,0x0e};
                if(at[0]==0xbe&&!std::memcmp(at+5,infoTail,sizeof(infoTail))){std::memcpy(&operand,at+1,4);if(inImage(operand,455*sizeof(minimaplayout::Info))){info_=operand;++foundInfo;}}
                if(at[0]==0x8b&&at[1]==0x3c&&at[2]==0xb5&&at[7]==0x85&&at[8]==0xff&&at[9]==0x74&&at[10]==0x20){std::memcpy(&operand,at+3,4);if(inImage(operand,67*4)){components_=operand;++foundComponents;}}
            }
        }
        // The captured CE wanted-stars native directly indexes the HUD table
        // and calls IsDisplaying. Use those verified references when the generic
        // table/visibility patterns differ or are duplicated in this executable.
        if(foundComponents!=1||foundDisplay!=1){
            using Lookup=void*(__cdecl*)(const char*);const auto module=GetModuleHandleA("ScriptHook.dll");
            const auto lookup=module?reinterpret_cast<Lookup>(GetProcAddress(module,"?GetNativeAddress@Game@@SAPAXPBD@Z")):nullptr;
            const auto handler=testWanted?testWanted:reinterpret_cast<std::uintptr_t>(lookup?lookup("WANTED_STARS_ARE_FLASHING"):nullptr);
            std::array<unsigned char,256> code{};std::vector<std::uintptr_t> tables,queries;
            if(handler&&read(handler,code))for(std::size_t i=0;i+15<=code.size();){hde32s instruction{};const auto length=hde32_disasm(code.data()+i,&instruction);if(!length||(instruction.flags&F_ERROR))break;
                if((instruction.flags&F_DISP32)&&instruction.modrm_mod==0&&instruction.modrm_rm==4&&instruction.sib_base==5&&instruction.sib_scale==2){
                    std::array<std::uint32_t,67> pointers{};unsigned valid=0;
                    if(read(instruction.disp.disp32,pointers)){for(auto pointer:pointers)if(pointer>=0x10000)++valid;
                        if(valid>=10&&std::find(tables.begin(),tables.end(),instruction.disp.disp32)==tables.end())tables.push_back(instruction.disp.disp32);}
                }
                if(instruction.opcode==0xe8&&(instruction.flags&F_IMM32)){
                    const auto target=static_cast<std::uintptr_t>(static_cast<std::int64_t>(handler+i+length)+static_cast<std::int32_t>(instruction.imm.imm32));
                    std::array<unsigned char,8> prefix{};const std::array<unsigned char,8> expected{{0x80,0x79,0x11,0,0x0f,0x95,0xc0,0xc3}};
                    if(read(target,prefix)&&prefix==expected&&std::find(queries.begin(),queries.end(),target)==queries.end())queries.push_back(target);
                }
                i+=length;if(instruction.opcode==0xc3||instruction.opcode==0xc2)break;
            }
            if(tables.size()==1){components_=tables.front();foundComponents=1;}
            if(queries.size()==1){target_=queries.front();foundDisplay=1;}
        }
        if(foundInfo!=1||foundComponents!=1||foundFrontend!=1||foundDisplay!=1){status_="layout signatures unavailable";if(log){char text[160];std::snprintf(text,sizeof(text),"Compass minimap: geometry signatures info=%u table=%u frontend=%u query=%u",foundInfo,foundComponents,foundFrontend,foundDisplay);log(text);}return;}
        const auto init=MH_Initialize();if(init!=MH_OK&&init!=MH_ERROR_ALREADY_INITIALIZED){status_="hook initialization failed";return;}
        owner_.store(this,std::memory_order_release);
        if(MH_CreateHook(reinterpret_cast<void*>(target_),reinterpret_cast<void*>(&displaying),reinterpret_cast<void**>(&original_))!=MH_OK){owner_=nullptr;status_="geometry observer unavailable";return;}
        if(MH_EnableHook(reinterpret_cast<void*>(target_))!=MH_OK){MH_RemoveHook(reinterpret_cast<void*>(target_));owner_=nullptr;original_=nullptr;status_="geometry observer unavailable";return;}
        installed_=true;status_="radar geometry";ring_.enable(frontend_,log,testImage);update();if(log)log("Compass minimap: radar-only position mask active; native visibility, component opacity, palette and preferences unchanged");
    }
};
