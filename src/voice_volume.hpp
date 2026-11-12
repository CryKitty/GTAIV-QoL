#pragma once
#include <fstream>
#include <atomic>
#include <memory>
#include <unordered_map>
#include "voice_metadata.hpp"
#include "qol_settings_client.hpp"
namespace vocalgain {
using Getter=void*(__thiscall*)(void*,uint32_t);
Getter atOffset=nullptr,byHash=nullptr;
struct Record {voicevolume::Entry entry;std::unordered_map<std::string,std::unique_ptr<std::vector<unsigned char>>> gains;};
std::vector<std::unique_ptr<Record>> records;
std::unordered_multimap<uint64_t,size_t> candidates;
SRWLOCK lock=SRWLOCK_INIT;
const QolSettingsApi* settings=nullptr;
unsigned copies=0;
bool installed=false;
std::atomic<unsigned> offsetCalls{0},hashCalls{0},candidateCalls{0},bodyMismatches{0};
void* Filter(void* original,uint32_t query=0,bool byName=false){
    if(!original||!settings||!settings->Get(QOL_VOCAL_SLIDERS))return original;
    unsigned char header[15]{};if(!Read(original,header,sizeof(header))||(header[0]!=4&&header[0]!=12))return original;
    const uint64_t key=(static_cast<uint64_t>(voicevolume::U32(header+1))<<8)|header[0];
    const auto range=candidates.equal_range(key);if(range.first==range.second)return original;
    ++candidateCalls;
    AcquireSRWLockExclusive(&lock);
    void* result=original;
    try { for(auto it=range.first;it!=range.second;++it){auto& record=*records[it->second];
        if (!record.entry.cutscene) continue; // Never change normal gameplay speech.
        const int percent=settings->Get(QOL_CUTSCENE_VOICE);
        if(percent>=100||percent<0)break; // Rescue: unproven amplification must not alter stock speech.
        unsigned char actual[512]{};
        // Resolve identity through the actual getter argument, not serialized bank/wave
        // hashes: the engine replaces those references in loaded simple-sound records.
        const auto& expected=record.entry.bytes;
        const size_t headerEnd=voicevolume::HeaderEnd(expected);
        if((byName?voicevolume::NameHash(record.entry.name)!=query:record.entry.offset!=query)||!headerEnd)continue;
        if(!Read(original,actual,expected.size())||std::memcmp(actual,expected.data(),5)||
           std::memcmp(actual+11,expected.data()+11,headerEnd-11)){
            const unsigned mismatch=++bodyMismatches;
            if(mismatch<=8){char line[260];sprintf_s(line,"voice metadata rejected: %s query=%08X mode=%s header_bytes=%u",record.entry.name.c_str(),query,byName?"hash":"offset",static_cast<unsigned>(headerEnd));Log(line);}
            continue;
        }
        // Cache the complete live record and selected gain so no resolved reference is stale.
        std::string gainKey(reinterpret_cast<const char*>(&percent),sizeof(percent));
        gainKey.append(reinterpret_cast<const char*>(actual),expected.size());
        auto found=record.gains.find(gainKey);
        if(found==record.gains.end()){
            if(copies>=8192)break;
            auto live=record.entry;live.bytes.assign(actual,actual+expected.size());
            auto bytes=voicevolume::Gain(live,percent);if(bytes.empty())break;
            auto saved=std::make_unique<std::vector<unsigned char>>(std::move(bytes));
            found=record.gains.emplace(gainKey,std::move(saved)).first;++copies;
            if(copies<=24){char line[220];sprintf_s(line,"voice gain metadata: %s percent=%d kind=%s path=%s",record.entry.name.c_str(),percent,record.entry.cutscene?"cutscene centre":"speech","leaf-attenuation");Log(line);}
        }
        result=found->second->data();break;
    }
    } catch(const std::bad_alloc&){result=original;}
    ReleaseSRWLockExclusive(&lock);return result;
}
void* __fastcall Offset(void* manager,void*,uint32_t value){++offsetCalls;return Filter(atOffset(manager,value),value,false);}
void* __fastcall Hash(void* manager,void*,uint32_t value){++hashCalls;return Filter(byHash(manager,value),value,true);}
void Status(){
    AcquireSRWLockShared(&lock);const unsigned saved=copies;ReleaseSRWLockShared(&lock);
    char line[240];sprintf_s(line,"voice status ready=%u offset_calls=%u hash_calls=%u candidates=%u body_mismatches=%u copies=%u enabled=%d gameplay=%d cutscene=%d",installed?1u:0u,offsetCalls.load(),hashCalls.load(),candidateCalls.load(),bodyMismatches.load(),saved,settings?settings->Get(QOL_VOCAL_SLIDERS):-1,settings?settings->Get(QOL_GAMEPLAY_VOICE):-1,settings?settings->Get(QOL_CUTSCENE_VOICE):-1);Log(line);
}
bool Install(const char* exe){
    if(installed)return true;
    settings=qolclient::Api();if(!settings){Log("voice gain unavailable: shared settings API missing");return false;}
    std::string folder=exe;const auto slash=folder.find_last_of("\\/");if(slash==folder.npos)return false;folder.resize(slash+1);
    std::vector<voicevolume::Entry> entries;
    for(const char* path:{"pc\\audio\\config\\sounds.dat15","TLAD\\pc\\audio\\config\\ep1_sounds.dat15","TBoGT\\pc\\audio\\config\\ep2_sounds.dat15"}){
        std::ifstream file(folder+path,std::ios::binary|std::ios::ate);if(!file)continue;
        const auto size=file.tellg();if(size<24||size>32*1024*1024)continue;
        std::vector<unsigned char> data(static_cast<size_t>(size));file.seekg(0);
        if(!file.read(reinterpret_cast<char*>(data.data()),static_cast<std::streamsize>(data.size()))||!voicevolume::ParseDat(data,entries)){Log("voice gain: invalid audio metadata file ignored");continue;}
    }
    if(entries.empty()){Log("voice gain unavailable: no validated speech/centre metadata");return false;}
    auto base=reinterpret_cast<unsigned char*>(GetModuleHandleA(nullptr));auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);auto nt=reinterpret_cast<IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);auto sections=IMAGE_FIRST_SECTION(nt);
    unsigned matches=0;unsigned char* site=nullptr;
    // CE call pair from Parik27/IV.EFLC.Rainbomizer src/audio.cc.
    for(unsigned s=0;s<nt->FileHeader.NumberOfSections;++s){const auto& sec=sections[s];if(!(sec.Characteristics&IMAGE_SCN_MEM_EXECUTE)||sec.VirtualAddress>=nt->OptionalHeader.SizeOfImage||sec.Misc.VirtualSize>nt->OptionalHeader.SizeOfImage-sec.VirtualAddress)continue;
        std::vector<unsigned char> code(sec.Misc.VirtualSize);if(!Read(base+sec.VirtualAddress,code.data(),code.size()))continue;
        for(size_t i=0;i+12<code.size();++i)if(code[i]==0x8B&&code[i+1]==0xF1&&code[i+2]==0x74&&code[i+4]==0xE8&&code[i+9]==0xEB&&code[i+11]==0xE8){site=base+sec.VirtualAddress+i;++matches;}
    }
    char line[180];sprintf_s(line,"voice metadata discovery call_pairs=%u candidate_records=%u",matches,static_cast<unsigned>(entries.size()));Log(line);
    if(matches!=1)return false;
    int32_t first=0,second=0;if(!Read(site+5,&first,4)||!Read(site+12,&second,4))return false;
    void* target1=site+9+first;void* target2=site+16+second;
    if(target1==target2||!Executable(target1)||!Executable(target2))return false;
    const auto init=MH_Initialize();if(init!=MH_OK&&init!=MH_ERROR_ALREADY_INITIALIZED)return false;
    void *trampoline1=nullptr,*trampoline2=nullptr;
    if(MH_CreateHook(target1,reinterpret_cast<void*>(&Offset),&trampoline1)!=MH_OK)return false;
    if(MH_CreateHook(target2,reinterpret_cast<void*>(&Hash),&trampoline2)!=MH_OK){MH_RemoveHook(target1);return false;}
    for(auto& entry:entries){const auto key=(static_cast<uint64_t>(voicevolume::U32(entry.bytes.data()+1))<<8)|entry.bytes[0];candidates.emplace(key,records.size());auto record=std::make_unique<Record>();record->entry=std::move(entry);records.push_back(std::move(record));}
    atOffset=reinterpret_cast<Getter>(trampoline1);byHash=reinterpret_cast<Getter>(trampoline2);
    if(MH_EnableHook(target1)!=MH_OK||MH_EnableHook(target2)!=MH_OK){MH_DisableHook(target1);MH_DisableHook(target2);MH_RemoveHook(target1);MH_RemoveHook(target2);records.clear();candidates.clear();return false;}
    installed=true;Log("voice gain ready: CUTSCENES_*_CENTRE only; gameplay speech unchanged; original files untouched; new sounds use selected gain");return true;
}
}
