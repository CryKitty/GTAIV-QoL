#pragma once
#include "radar_fill.hpp"
#include "../../minhook/include/MinHook.h"
#include <atomic>
#include <cmath>
#include <intrin.h>

namespace radararcs {
struct Resolution {std::uintptr_t target=0,parent=0;std::vector<std::uintptr_t> returns;unsigned candidates=0;};
inline bool displacement(const std::vector<radarfill::Instruction>& body,unsigned value){
    return std::any_of(body.begin(),body.end(),[&](const auto& i){return (i.decoded.flags&F_DISP32)&&i.decoded.disp.disp32==value&&i.decoded.modrm_mod==2;});
}
// The CE capture identifies a five-argument cdecl leaf, called eight times by
// the radar's health/armour callback. Resolve the relationship, not captured VAs.
// Both ped fields, both side constants, caller cleanup, leaf stack/float ABI and
// a fully decoded leaf must agree. Never suppress the parent lifecycle callback.
inline Resolution resolve(const std::vector<radarfill::Section>& sections,std::uintptr_t parent){
    Resolution out;out.parent=parent;
    const auto body=radarfill::decode(parent,sections);
    if(!displacement(body,0xa84)||!displacement(body,0xb84))return out;
    struct Candidate {std::uintptr_t target;std::vector<std::uintptr_t> returns;unsigned sides=0;};
    std::vector<Candidate> candidates;
    for(std::size_t n=1;n<body.size();++n){const auto& i=body[n];const auto& before=body[n-1];
        if(i.decoded.opcode!=0xe8||before.address+before.decoded.len!=i.address||before.decoded.opcode!=0x6a||before.decoded.imm.imm8>1)continue;
        bool cleanup=false;
        for(std::size_t k=n+1;k<body.size()&&body[k].address<i.address+21;++k){const auto& d=body[k].decoded;
            if(d.opcode==0x83&&d.modrm==0xc4&&d.imm.imm8==20){cleanup=true;break;}
            if(d.opcode==0xe8||d.opcode==0xc3)break;
        }
        if(!cleanup)continue;
        const auto target=radarfill::callTarget(i);
        auto found=std::find_if(candidates.begin(),candidates.end(),[&](const auto& c){return c.target==target;});
        if(found==candidates.end()){candidates.push_back({target,{},0});found=candidates.end()-1;}
        found->returns.push_back(i.address+i.decoded.len);found->sides|=1u<<before.decoded.imm.imm8;
    }
    for(const auto& c:candidates){
        if(c.returns.size()<6||c.sides!=3)continue;
        std::array<unsigned char,29> prefix{};
        if(!radarfill::read(c.target,prefix))continue;
        const unsigned char start[]={0x81,0xec};
        if(std::memcmp(prefix.data(),start,2)||prefix[6]!=0xa1||prefix[11]!=0x33||prefix[12]!=0xc4||prefix[13]!=0x89||prefix[14]!=0x84||prefix[15]!=0x24||prefix[20]!=0xf3||prefix[21]!=0x0f||prefix[22]!=0x10||prefix[24]!=0x24)continue;
        std::uint32_t frame=0,saved=0,argument=0;std::memcpy(&frame,prefix.data()+2,4);std::memcpy(&saved,prefix.data()+16,4);std::memcpy(&argument,prefix.data()+25,4);
        // MOVSS xmm0,[esp+frame+8] is the second (float) stack argument.
        if(frame<64||frame>1024||saved!=frame-4||prefix[23]!=0x84||argument!=frame+8)continue;
        const auto leaf=radarfill::decode(c.target,sections);
        if(leaf.empty()||!displacement(leaf,0xb84))continue;
        ++out.candidates;out.target=c.target;out.returns=c.returns;
    }
    if(out.candidates!=1){out.target=0;out.returns.clear();}return out;
}
}

class RadarArcs {
    using Draw=void(__cdecl*)(unsigned,float,unsigned,unsigned,unsigned);
    inline static Draw original_=nullptr;
    inline static std::atomic<RadarArcs*> owner_{nullptr};
    std::uintptr_t target_=0,frontend_=0,image_=0;
    std::vector<std::uintptr_t> returns_;
    void(*log_)(const char*)=nullptr;
    std::atomic<unsigned> calls_[2]{},hidden_[2]{},states_{0},otherCalls_{0};
    static void __cdecl draw(unsigned side,float amount,unsigned colour,unsigned overlay,unsigned extra){
        auto* self=owner_.load(std::memory_order_acquire);
        if(!self){if(original_)original_(side,amount,colour,overlay,extra);return;}
        unsigned char frontend=1;const bool suppress=radarfill::read(self->frontend_,frontend)&&frontend==0;
        const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
        const bool known=std::find(self->returns_.begin(),self->returns_.end(),caller)!=self->returns_.end();
        const auto kind=side?0u:1u;const auto calls=++self->calls_[kind];if(suppress)++self->hidden_[kind];
        if(!known)++self->otherCalls_;
        const auto bit=1u<<(kind*4+(suppress?1u:0u)+(known?0u:2u));
        if(!(self->states_.fetch_or(bit)&bit)&&self->log_){char line[420];
            std::snprintf(line,sizeof(line),"Compass circular draw: t=%lu element=%s source=five-argument-native-arc suppressed=%u frontend=%u calls=%u hidden=%u amount=%.4f colour=%08x overlay=%u extra=%u caller-rva=%08x caller-known=%u geometry-writes=0",GetTickCount(),side?"health-front-fill":"armour-back-fill",suppress?1u:0u,static_cast<unsigned>(frontend),calls,self->hidden_[kind].load(),static_cast<double>(std::isfinite(amount)?amount:0),colour,overlay,extra,static_cast<unsigned>(caller-self->image_),known?1u:0u);self->log_(line);
        }
        // This leaf has a fixed radar anchor, even if a second phase calls it.
        // Do not restrict suppression to just the parent's known call sites.
        if(!suppress)original_(side,amount,colour,overlay,extra);
    }
public:
    bool active()const{return target_!=0;}
    unsigned calls(unsigned side)const{return calls_[side?0:1].load();}
    unsigned hidden(unsigned side)const{return hidden_[side?0:1].load();}
    void release(){
        if(target_){MH_DisableHook(reinterpret_cast<void*>(target_));MH_RemoveHook(reinterpret_cast<void*>(target_));
            if(log_){char line[260];std::snprintf(line,sizeof(line),"Compass circular release: health=%u/%u armour=%u/%u other-phase-calls=%u native-arc-restored=1 geometry-writes=0",hidden(1),calls(1),hidden(0),calls(0),otherCalls_.load());log_(line);}}
        target_=0;owner_.store(nullptr,std::memory_order_release);
    }
    void enable(const std::vector<radarfill::Section>& sections,std::uintptr_t parent,std::uintptr_t frontend,std::uintptr_t image,void(*log)(const char*)){
        const auto resolved=radararcs::resolve(sections,parent);frontend_=frontend;image_=image;log_=log;returns_=resolved.returns;states_=0;
        if(!resolved.target){if(log)log("Compass circular coverage: native-arc=unresolved health-front-fill=unfiltered armour-back-fill=unfiltered geometry-writes=0");return;}
        const auto init=MH_Initialize();if(init!=MH_OK&&init!=MH_ERROR_ALREADY_INITIALIZED){if(log)log("Compass circular coverage: native-arc=hook-init-failed suppressed=0");return;}
        auto* target=reinterpret_cast<void*>(resolved.target);
        if(MH_CreateHook(target,reinterpret_cast<void*>(&draw),reinterpret_cast<void**>(&original_))!=MH_OK){if(log)log("Compass circular coverage: native-arc=hook-create-failed suppressed=0");return;}
        owner_.store(this,std::memory_order_release);
        if(MH_EnableHook(target)!=MH_OK){owner_=nullptr;MH_RemoveHook(target);if(log)log("Compass circular coverage: native-arc=hook-enable-failed suppressed=0");return;}target_=resolved.target;
        if(log){char line[300];std::snprintf(line,sizeof(line),"Compass circular coverage: native-arc=ready parent-rva=%08x leaf-rva=%08x five-arg-sites=%u health-front-fill=filtered armour-back-fill=filtered parent-content=original geometry-writes=0",static_cast<unsigned>(parent-image),static_cast<unsigned>(target_-image),static_cast<unsigned>(returns_.size()));log(line);}
    }
};
