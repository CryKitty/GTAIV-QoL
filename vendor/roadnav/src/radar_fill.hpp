#pragma once
#include "../../minhook/src/hde/hde32.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <vector>

namespace radarfill {
struct Section {std::uintptr_t base;std::vector<unsigned char> bytes;bool executable=false;};
template<class T> bool read(std::uintptr_t p,T& value){SIZE_T n=0;return p>=0x10000&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(p),&value,sizeof(value),&n)&&n==sizeof(value);}
inline bool codeAddress(std::uintptr_t p,const std::vector<Section>& sections){for(const auto& section:sections)if(section.executable&&p>=section.base&&p-section.base<section.bytes.size())return true;return false;}
struct Instruction {std::uintptr_t address;hde32s decoded;};
enum class Body { Callback, Queue };
// A queue method can tail-call DrawBlips, or return with thiscall stack cleanup.
// Neither makes its earlier callback requests invalid. A visual callback must
// still return without stack cleanup, including through an external tail call.
inline std::vector<Instruction> decode(std::uintptr_t entry,const std::vector<Section>& sections,Body body=Body::Callback,unsigned tailDepth=0){
    const Section* section=nullptr;for(const auto& s:sections)if(s.executable&&entry>=s.base&&entry-s.base<s.bytes.size())section=&s;
    if(!section)return {};
    const auto start=static_cast<std::size_t>(entry-section->base),end=std::min(section->bytes.size(),start+4096);
    std::vector<unsigned char> visited(end-start);std::vector<std::size_t> pending{start};std::vector<Instruction> result;
    bool returned=false;
    while(!pending.empty()){auto at=pending.back();pending.pop_back();
        while(at>=start&&at+16<=end&&!visited[at-start]){
            visited[at-start]=1;hde32s instruction{};const auto length=hde32_disasm(section->bytes.data()+at,&instruction);
            if(!length||(instruction.flags&F_ERROR)||instruction.opcode==0xcc||(instruction.opcode==0xc2&&body==Body::Callback))return {};
            result.push_back({section->base+at,instruction});
            if(instruction.opcode==0xc3||instruction.opcode==0xc2){returned=true;break;}
            const bool jump=instruction.opcode==0xe9||instruction.opcode==0xeb;
            const bool conditional=(instruction.opcode>=0x70&&instruction.opcode<=0x7f)||(instruction.opcode==0x0f&&instruction.opcode2>=0x80&&instruction.opcode2<=0x8f)||(instruction.opcode>=0xe0&&instruction.opcode<=0xe3);
            if(instruction.opcode==0xff&&instruction.modrm_reg==4)return {};
            if(jump||conditional){
                const auto relative=(instruction.flags&F_IMM8)?static_cast<std::int32_t>(static_cast<std::int8_t>(instruction.imm.imm8)):static_cast<std::int32_t>(instruction.imm.imm32);
                const auto target=static_cast<std::int64_t>(at+length)+relative;
                if(target<static_cast<std::int64_t>(start)||target+16>static_cast<std::int64_t>(end)){
                    const auto destination=static_cast<std::uintptr_t>(static_cast<std::int64_t>(section->base)+target);
                    if(!jump||!codeAddress(destination,sections))return {};
                    if(body==Body::Callback&&(tailDepth>=4||decode(destination,sections,body,tailDepth+1).empty()))return {};
                    returned=true;break;
                }
                pending.push_back(static_cast<std::size_t>(target));if(jump)break;
            }
            at+=length;if(at+16>end)return {};
        }
    }
    if(!returned)return {};
    std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.address<b.address;});return result;
}
inline std::uintptr_t callTarget(const Instruction& i){return static_cast<std::uintptr_t>(static_cast<std::int64_t>(i.address+i.decoded.len)+static_cast<std::int32_t>(i.decoded.imm.imm32));}
inline bool name(std::uintptr_t descriptor,const char* expected){
    for(std::size_t i=0;i<96;++i){char c=0;if(!read(descriptor+8+i,c)||c!=expected[i])return false;if(!c)return true;}return false;
}
// Resolve MSVC x86 RTTI structurally, rather than applying version-specific VAs.
inline std::vector<std::uintptr_t> radarTables(const std::vector<Section>& sections){
    constexpr char radarName[]=".?AVCRenderPhaseRadar@@";
    std::vector<std::uintptr_t> types,locators,tables;
    for(const auto& s:sections)if(!s.executable)for(std::size_t i=8;i+sizeof(radarName)<=s.bytes.size();++i)
        if(!std::memcmp(s.bytes.data()+i,radarName,sizeof(radarName)))types.push_back(s.base+i-8);
    if(types.size()!=1)return {};
    for(const auto& s:sections)if(!s.executable)for(std::size_t i=0;i+20<=s.bytes.size();i+=4){
        std::array<std::uint32_t,5> locator{};std::memcpy(locator.data(),s.bytes.data()+i,sizeof(locator));
        if(locator[0]||locator[1]||locator[2]||locator[3]!=types[0])continue;
        std::array<std::uint32_t,4> hierarchy{};
        if(!read(locator[4],hierarchy)||hierarchy[0]||hierarchy[2]<2||hierarchy[2]>32)continue;
        bool root=false,base=false;
        for(unsigned b=0;b<hierarchy[2];++b){std::uint32_t record=0,type=0;
            if(!read(static_cast<std::uintptr_t>(hierarchy[3])+b*4,record)||!read(record,type))continue;
            root=root||type==types[0];base=base||name(type,".?AVCRenderPhase@@");
        }
        if(root&&base)locators.push_back(s.base+i);
    }
    for(const auto& s:sections)if(!s.executable)for(std::size_t i=0;i+4+15*4<=s.bytes.size();i+=4){std::uint32_t locator=0;std::memcpy(&locator,s.bytes.data()+i,4);
        if(std::find(locators.begin(),locators.end(),locator)==locators.end())continue;
        bool valid=true;
        for(unsigned slot=0;slot<15;++slot){std::uint32_t method=0;std::memcpy(&method,s.bytes.data()+i+4+slot*4,4);if(!codeAddress(method,sections)){valid=false;break;}}
        if(valid)tables.push_back(s.base+i+4);
    }
    return tables;
}
struct Request {std::uintptr_t address,callback,constructor;};
struct Resolution {
    std::uintptr_t callback=0,phase=0;
    unsigned slot=0,candidates=0,tables=0,entries=0,decoded=0,queues=0,rejectedBody=0,rejectedShared=0;
};
inline std::vector<Request> requests(const std::vector<Instruction>& instructions){
    std::vector<Request> result;std::uintptr_t pushed=0,pushAt=0;
    for(const auto& i:instructions){
        if(i.decoded.opcode==0x68){pushed=i.decoded.imm.imm32;pushAt=i.address;}
        if(i.decoded.opcode==0xe8){if(pushed&&i.address-pushAt<=32)result.push_back({pushAt,pushed,callTarget(i)});pushed=0;}
    }
    return result;
}
// The independently resolved front AND back frames identify their queue owner
// even when CE lacks the old version's exact RTTI hierarchy/vtable layout.
// Require a padded procedure entry and a complete decoded path reaching both
// references; merely finding two immediate values nearby is insufficient.
inline std::vector<std::uintptr_t> queueEntries(const std::vector<Section>& sections,std::uintptr_t front,std::uintptr_t back){
    std::vector<std::uintptr_t> result;
    for(const auto& s:sections)if(s.executable)for(std::size_t at=2;at+16<=s.bytes.size();++at){
        if(s.bytes[at]!=0x68)continue;
        std::uint32_t value=0;std::memcpy(&value,s.bytes.data()+at+1,4);if(value!=back)continue;
        const auto lower=at>4096?at-4096:2;
        for(auto begin=at;begin>=lower;--begin){
            if(((s.base+begin)&15)||s.bytes[begin-1]!=0xcc||s.bytes[begin-2]!=0xcc)continue;
            const auto entry=s.base+begin;const auto instructions=decode(entry,sections,Body::Queue);
            bool foundBack=false,foundFront=false;
            for(const auto& i:instructions)if(i.decoded.opcode==0x68){
                foundBack=foundBack||(i.address==s.base+at&&i.decoded.imm.imm32==back);
                foundFront=foundFront||i.decoded.imm.imm32==front;
            }
            if(foundBack&&foundFront){if(std::find(result.begin(),result.end(),entry)==result.end())result.push_back(entry);break;}
        }
    }
    return result;
}
inline void dump(std::uintptr_t entry,const std::vector<Section>& sections,void(*log)(const char*),unsigned limit){
    if(!log)return;
    for(const auto& s:sections)if(s.executable&&entry>=s.base&&entry-s.base<s.bytes.size()){
        const auto offset=entry-s.base;limit=static_cast<unsigned>(std::min<std::size_t>(limit,s.bytes.size()-offset));
        for(unsigned at=0;at<limit;at+=48){char line[180];const auto count=std::min(48u,limit-at);
            const auto prefix=std::snprintf(line,sizeof(line),"Compass radar code: entry=%08x +%03x ",static_cast<unsigned>(entry),at);
            for(unsigned i=0;i<count;++i)std::snprintf(line+prefix+i*2,sizeof(line)-prefix-i*2,"%02x",s.bytes[offset+at+i]);
            log(line);
        }
        return;
    }
}
inline Resolution resolve(const std::vector<Section>& sections,std::uintptr_t front,std::uintptr_t back,void(*log)(const char*)=nullptr){
    Resolution result;const auto tables=radarTables(sections);result.tables=static_cast<unsigned>(tables.size());
    auto entries=queueEntries(sections,front,back);
    if(tables.size()==1)for(unsigned slot=0;slot<15;++slot){std::uint32_t method=0;
        if(read(tables[0]+slot*4,method)&&std::find(entries.begin(),entries.end(),method)==entries.end())entries.push_back(method);
    }
    result.entries=static_cast<unsigned>(entries.size());
    for(const auto method:entries){
        const auto instructions=decode(method,sections,Body::Queue);if(instructions.empty())continue;++result.decoded;
        const auto queued=requests(instructions);
        std::vector<std::uintptr_t> constructors;
        for(const auto& f:queued)if(f.callback==front)for(const auto& b:queued)if(b.callback==back&&f.constructor==b.constructor&&std::find(constructors.begin(),constructors.end(),f.constructor)==constructors.end())constructors.push_back(f.constructor);
        if(constructors.size()!=1)continue;
        std::uintptr_t frontAt=0,backAt=0;unsigned frontCount=0,backCount=0;
        for(const auto& r:queued)if(r.constructor==constructors[0]){if(r.callback==front){frontAt=r.address;++frontCount;}if(r.callback==back){backAt=r.address;++backCount;}}
        if(frontCount!=1||backCount!=1||frontAt<=backAt)continue;
        ++result.queues;result.phase=method;
        if(tables.size()==1)for(unsigned slot=0;slot<15;++slot){std::uint32_t target=0;if(read(tables[0]+slot*4,target)&&target==method)result.slot=slot;}
        if(log){char line[180];std::snprintf(line,sizeof(line),"Compass radar queue: entry=%08x ctor=%08x requests=%u RTTI-tables=%u",static_cast<unsigned>(method),static_cast<unsigned>(constructors[0]),static_cast<unsigned>(queued.size()),result.tables);log(line);dump(method,sections,log,1536);}
        for(const auto& request:queued){
            if(request.constructor!=constructors[0]||request.address<=backAt||request.address>=frontAt||request.callback==front||request.callback==back||!codeAddress(request.callback,sections))continue;
            // Inline callback construction also counts: a common state restore
            // can be queued once through a constructor and once through MOV.
            unsigned references=0;
            for(const auto& i:instructions)if((i.decoded.flags&F_IMM32)&&!(i.decoded.flags&F_RELATIVE)&&i.decoded.imm.imm32==request.callback)++references;
            if(references!=1)continue;
            if(log){char line[150];std::snprintf(line,sizeof(line),"Compass radar candidate: entry=%08x queued-at=%08x",static_cast<unsigned>(request.callback),static_cast<unsigned>(request.address));log(line);dump(request.callback,sections,log,1024);}
            if(decode(request.callback,sections).empty()){++result.rejectedBody;continue;}
            // Reject a function called or queued by another part of the engine.
            bool shared=false;
            for(const auto& text:sections)if(text.executable)for(std::size_t at=0;at+16<=text.bytes.size();++at){
                if(text.bytes[at]!=0x68&&text.bytes[at]!=0xe8)continue;
                hde32s op{};if(!hde32_disasm(text.bytes.data()+at,&op)||(op.flags&F_ERROR))continue;
                const Instruction i{text.base+at,op};
                const bool reference=(op.opcode==0x68&&op.imm.imm32==request.callback)||(op.opcode==0xe8&&callTarget(i)==request.callback);
                if(reference&&std::none_of(instructions.begin(),instructions.end(),[&](const auto& own){return own.address==i.address;})){shared=true;break;}
            }
            if(shared){++result.rejectedShared;continue;}
            ++result.candidates;result.callback=request.callback;
        }
    }
    // Both the owning queue and visual callback must be unique.
    if(result.candidates!=1||result.queues!=1)result.callback=0;
    if(log){char line[240];std::snprintf(line,sizeof(line),"Compass radar resolver: tables=%u entries=%u decoded=%u queues=%u candidates=%u rejected-body=%u rejected-shared=%u",result.tables,result.entries,result.decoded,result.queues,result.candidates,result.rejectedBody,result.rejectedShared);log(line);
        if(!result.queues){dump(front,sections,log,256);dump(back,sections,log,256);
            // Preserve the exact surrounding queue code even if its CFG was
            // rejected. Bounded captures avoid another blind resolver revision.
            unsigned captured=0;
            for(const auto& section:sections)if(section.executable)for(std::size_t at=0;at+5<section.bytes.size()&&captured<3;++at)if(section.bytes[at]==0x68){std::uint32_t value=0;std::memcpy(&value,section.bytes.data()+at+1,4);if(value==back){dump(section.base+(at>512?at-512:0),sections,log,2048);++captured;}}
        }
    }
    return result;
}
}
