#pragma once
#include "../../minhook/src/hde/hde32.h"
#include <array>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cmath>

namespace policeprobe {
template<class Lookup,class Names> void* resolve(Lookup lookup,Names names,unsigned hash,const char* label){
    void* target=lookup?lookup(hash):nullptr;return target?target:(names?names(label):nullptr);
}
struct References {std::vector<std::uintptr_t> calls,globals;};
inline References decode(const unsigned char* code,std::size_t size,std::uintptr_t address){
    References result;
    // HDE reads up to 15 bytes. The caller supplies that much readable padding.
    for(std::size_t offset=0;offset+15<=size;){hde32s instruction{};
        const auto length=hde32_disasm(code+offset,&instruction);
        if(!length||(instruction.flags&F_ERROR))break;
        if(instruction.opcode==0xe8&&(instruction.flags&F_IMM32)){
            const auto displacement=static_cast<std::int32_t>(instruction.imm.imm32);
            result.calls.push_back(static_cast<std::uintptr_t>(static_cast<std::int64_t>(address+offset+length)+displacement));
        }
        if(instruction.flags&F_DISP32)result.globals.push_back(instruction.disp.disp32);
        if((instruction.opcode==0xa1||instruction.opcode==0xa3)&&(instruction.flags&F_IMM32))result.globals.push_back(instruction.imm.imm32);
        offset+=length;
        if(instruction.opcode==0xc3||instruction.opcode==0xc2)break;
    }
    return result;
}
inline bool playerPedTable(const std::array<unsigned char,32>& code,std::uint32_t& table){
    // Captured STORE_WANTED_LEVEL -> player lookup, CE 1.2.0.59.
    const unsigned char prefix[]={0x8b,0x44,0x24,0x04,0x83,0xf8,0x1f,0x77,0x08,0x8b,0x04,0x85};
    if(std::memcmp(code.data(),prefix,sizeof(prefix))||code[16]!=0xc3)return false;
    std::memcpy(&table,code.data()+12,4);return true;
}
class Capture {
    std::uintptr_t base_=0,table_=0;unsigned size_=0,written_=0,samples_=0;
    bool detailed_=false;DWORD sampledAt_=0;unsigned previousStars_=99;char path_[MAX_PATH]{};
    std::vector<IMAGE_SECTION_HEADER> sections_;
    template<class T> static bool read(std::uintptr_t address,T& out){SIZE_T bytes=0;return address>=0x10000&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),&out,sizeof(out),&bytes)&&bytes==sizeof(out);}
    bool inImage(std::uintptr_t address,unsigned bytes=1)const{return address>=base_&&address-base_<size_&&bytes<=size_-(address-base_);}
    bool executable(std::uintptr_t address)const{if(!inImage(address))return false;for(const auto& section:sections_)if((section.Characteristics&IMAGE_SCN_MEM_EXECUTE)&&address-base_>=section.VirtualAddress&&address-base_-section.VirtualAddress<section.Misc.VirtualSize)return true;return false;}
    void line(const char* text){if(!path_[0]||written_>=196608)return;FILE* file=nullptr;fopen_s(&file,path_,"a");if(file){written_+=static_cast<unsigned>(std::strlen(text)+1);std::fprintf(file,"%s\n",text);std::fclose(file);}}
    template<std::size_t Size> void dump(const char* label,std::uintptr_t address,const std::array<unsigned char,Size>& bytes){
        for(unsigned offset=0;offset<Size;offset+=32){char text[220]{};int used=std::snprintf(text,sizeof(text),"Police detail: %s address=%08x +%03x ",label,static_cast<unsigned>(address),offset);
            for(unsigned i=offset;i<std::min<unsigned>(offset+32,static_cast<unsigned>(Size));++i)used+=std::snprintf(text+used,sizeof(text)-used,"%02x",static_cast<unsigned>(bytes[i]));line(text);}
    }
public:
    using Lookup=void*(__cdecl*)(unsigned);
    using Names=void*(__cdecl*)(const char*);
    void start(Lookup lookup,Names names){
        const DWORD setupStarted=GetTickCount();table_=0;path_[0]=0;sections_.clear();
        if(!lookup&&!names)return;base_=reinterpret_cast<std::uintptr_t>(GetModuleHandleA(nullptr));
        IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS32 nt{};
        if(!read(base_,dos)||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<=0||dos.e_lfanew>4096||!read(base_+dos.e_lfanew,nt)||nt.Signature!=IMAGE_NT_SIGNATURE||nt.FileHeader.NumberOfSections>32)return;
        size_=nt.OptionalHeader.SizeOfImage;sections_.resize(nt.FileHeader.NumberOfSections);
        for(unsigned i=0;i<sections_.size();++i)if(!read(base_+dos.e_lfanew+offsetof(IMAGE_NT_HEADERS32,OptionalHeader)+nt.FileHeader.SizeOfOptionalHeader+i*sizeof(IMAGE_SECTION_HEADER),sections_[i]))return;
        const DWORD length=GetModuleFileNameA(nullptr,path_,MAX_PATH);if(!length||length>=MAX_PATH){path_[0]=0;return;}
        char* slash=std::strrchr(path_,'\\');if(!slash){path_[0]=0;return;}slash[1]=0;strcat_s(path_,"GTAIV-QoL-police.log");
        FILE* file=nullptr;if(fopen_s(&file,path_,"w")!=0||!file){path_[0]=0;return;}std::fclose(file);written_=samples_=0;sampledAt_=0;previousStars_=99;
        char heading[160];std::snprintf(heading,sizeof(heading),"Police detail capture v1: read-only; image=%08x; circle fields not yet verified",static_cast<unsigned>(base_));line(heading);
        char ini[MAX_PATH]{};strcpy_s(ini,path_);char* iniSlash=std::strrchr(ini,'\\');if(iniSlash)strcpy_s(iniSlash+1,MAX_PATH-static_cast<std::size_t>(iniSlash+1-ini),"GTAIV-QoL.ini");
        detailed_=GetPrivateProfileIntA("Diagnostics","PoliceCodeCapture",0,ini)!=0;
        std::vector<std::pair<std::uintptr_t,unsigned>> pending;
        const unsigned hashes[]={0x3cee0376u,0x00746edfu,0x07d97f81u,0x12aa6d71u};
        const char* labels[]={"SET_FAKE_WANTED_CIRCLE","WANTED_STARS_ARE_FLASHING","REPORT_POLICE_SPOTTING_SUSPECT","STORE_WANTED_LEVEL"};
        for(unsigned i=0;i<4;++i){if(!detailed_&&i!=3)continue;const unsigned hash=hashes[i];
            void* target=resolve(lookup,names,hash,labels[i]);
            const auto address=reinterpret_cast<std::uintptr_t>(target);
            char resolved[180];std::snprintf(resolved,sizeof(resolved),"Police native resolution: %s address=%08x executable=%u",labels[i],static_cast<unsigned>(address),executable(address)?1u:0u);line(resolved);
            if(executable(address))pending.push_back({address,0});
        }
        std::vector<std::uintptr_t> visited,globals,circleGlobals;std::uintptr_t circleSetter=0;
        for(std::size_t n=0;n<pending.size()&&visited.size()<16;++n){const auto entry=pending[n];
            if(std::find(visited.begin(),visited.end(),entry.first)!=visited.end()||!executable(entry.first)||!inImage(entry.first,1024))continue;
            std::array<unsigned char,1024> code{};if(!read(entry.first,code))continue;visited.push_back(entry.first);if(detailed_)dump("code",entry.first,code);
            const auto references=decode(code.data(),code.size(),entry.first);
            if(n==0&&!references.calls.empty())circleSetter=references.calls.front();
            if(entry.first==circleSetter)circleGlobals=references.globals;
            std::array<unsigned char,32> playerLookup{};std::memcpy(playerLookup.data(),code.data(),playerLookup.size());std::uint32_t playerTable=0;
            if(playerPedTable(playerLookup,playerTable)&&inImage(playerTable,32*4))table_=playerTable;
            if(entry.second<2)for(auto call:references.calls)if(executable(call)&&pending.size()<48)pending.push_back({call,entry.second+1});
            for(auto global:references.globals)if(inImage(global,64)&&!executable(global)&&std::find(globals.begin(),globals.end(),global)==globals.end()&&globals.size()<24)globals.push_back(global);
        }
        // Capture consumers of referenced scalar globals, including the radar's
        // selection between fake and real search circles. No game calls/writes.
        unsigned xrefs=0;
        for(const auto& section:sections_){if(!detailed_)break;if(!(section.Characteristics&IMAGE_SCN_MEM_EXECUTE)||section.Misc.VirtualSize>32*1024*1024||!inImage(base_+section.VirtualAddress,section.Misc.VirtualSize))continue;
            std::vector<unsigned char> code(section.Misc.VirtualSize);SIZE_T bytes=0;
            if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(base_+section.VirtualAddress),code.data(),code.size(),&bytes)||bytes!=code.size())continue;
            for(std::size_t i=48;i+144<=code.size()&&xrefs<32;++i){std::uint32_t operand=0;std::memcpy(&operand,code.data()+i,4);
                if(std::find(circleGlobals.begin(),circleGlobals.end(),operand)==circleGlobals.end())continue;
                std::array<unsigned char,192> window{};std::memcpy(window.data(),code.data()+i-48,window.size());dump("global-xref",base_+section.VirtualAddress+i-48,window);++xrefs;
            }
        }
        for(auto global:globals){if(!detailed_)break;std::array<unsigned char,64> bytes{};if(read(global,bytes))dump("global-data",global,bytes);}
        char setup[120];std::snprintf(setup,sizeof(setup),"Police startup: reader-setup-ms=%lu detailed-code-capture=%u",GetTickCount()-setupStarted,detailed_?1u:0u);line(setup);
    }
    bool circle(unsigned playerIndex,unsigned stars,float& x,float& y,float& z,float& radius){
        std::uint32_t ped=0,info=0,owner=0,level=99;
        if(!table_||playerIndex>=32||!stars||stars>6||!read(table_+playerIndex*4,ped)||
           !read(static_cast<std::uintptr_t>(ped)+0x598,info)||!read(static_cast<std::uintptr_t>(info)+0x228,owner)||owner<0x10000||
           !read(static_cast<std::uintptr_t>(owner)+0x70+0x384,level)||level!=stars)return false;
        // Prototype: +30 is the last reported position in the live captures;
        // +40 follows the current player and is deliberately not used.
        std::array<float,3> center{};
        if(!read(static_cast<std::uintptr_t>(owner)+0xa0,center))return false;
        for(float value:center)if(!std::isfinite(value)||std::fabs(value)>15000)return false;
        x=center[0];y=center[1];z=center[2];
        // Captured radar consumer indexes this radius table by wanted level.
        // Require the complete instruction before reading its operand.
        const std::array<unsigned char,9> expected{{0xf3,0x0f,0x10,0x1c,0xb5,0xf8,0xb6,0x03,0x01}};
        std::array<unsigned char,9> instruction{};
        radius=0;
        if(read(base_+0x60bb73,instruction)&&instruction==expected){
            float value=0;if(read(base_+0xc3b6f8+stars*4,value)&&std::isfinite(value)&&value>=50&&value<=3000)radius=value;
        }
        return true;
    }
    void sample(unsigned playerIndex,unsigned stars,float x,float y,float z){
        if(!path_[0]||samples_>=32||stars>6)return;const DWORD now=GetTickCount();
        if(previousStars_==stars&&(!stars||now-sampledAt_<4000))return;
        sampledAt_=now;previousStars_=stars;++samples_;std::uint32_t object=0;
        char heading[180];std::snprintf(heading,sizeof(heading),"Police live sample: t=%lu stars=%u player=%.2f %.2f %.2f",now,stars,x,y,z);line(heading);
        // The earlier flashing-stars native reads a HUD component, not CWanted.
        // Follow the chain captured from STORE_WANTED_LEVEL and verify the
        // actual wanted-level field against that native before reading data.
        std::uint32_t ped=0,info=0,owner=0,level=99;
        if(!table_||playerIndex>=32||!read(table_+playerIndex*4,ped)||!read(static_cast<std::uintptr_t>(ped)+0x598,info)||
           !read(static_cast<std::uintptr_t>(info)+0x228,owner)||owner<0x10000)return;
        object=owner+0x70;
        if(!read(static_cast<std::uintptr_t>(object)+0x384,level)||level!=stars)return;
        std::array<unsigned char,0x388> bytes{};if(read(object,bytes))dump("wanted-object",object,bytes);
    }
};
}
