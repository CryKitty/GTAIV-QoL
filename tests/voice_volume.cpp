#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cassert>
#include "../vendor/minhook/include/MinHook.h"
void Log(const char*){}
bool Read(const void* source,void* out,SIZE_T size){SIZE_T copied=0;return ReadProcessMemory(GetCurrentProcess(),source,out,size,&copied)&&copied==size;}
bool Executable(const void*){return false;}
#include "../src/voice_volume.hpp"
int values[6]={1,1,1,1,200,50};
int32_t __cdecl Get(uint32_t id){return id<6?values[id]:-1;}
int32_t __cdecl Set(uint32_t,int32_t){return 0;}
void __cdecl Ready(uint32_t){}
int main(){
    const QolSettingsApi api{sizeof(QolSettingsApi),1,Get,Set,Ready};vocalgain::settings=&api;
    voicevolume::Entry entry;entry.name="CUTSCENES_TEST_CENTRE";entry.cutscene=true;entry.bytes.resize(29);entry.bytes[0]=12;entry.bytes[1]=20;entry.bytes[11]=1;
    const int16_t baseline=-700;std::memcpy(entry.bytes.data()+15,&baseline,2);
    vocalgain::candidates.emplace((20ull<<8)|12,0);auto record=std::make_unique<vocalgain::Record>();record->entry=entry;vocalgain::records.push_back(std::move(record));
    auto raw=entry.bytes;auto output=static_cast<unsigned char*>(vocalgain::Filter(raw.data()));
    assert(output!=raw.data());int16_t volume=0;std::memcpy(&volume,output+15,2);assert(volume==-1302);assert(raw==entry.bytes);
    assert(vocalgain::Filter(raw.data())==output);assert(vocalgain::copies==1);
    // Growing the registry must never relocate metadata retained by a playing sound.
    for(unsigned i=0;i<128;++i)vocalgain::records.push_back(std::make_unique<vocalgain::Record>());
    assert(vocalgain::Filter(raw.data())==output);std::memcpy(&volume,output+15,2);assert(volume==-1302);
    values[QOL_VOCAL_SLIDERS]=0;assert(vocalgain::Filter(raw.data())==raw.data());values[QOL_VOCAL_SLIDERS]=1;
    values[QOL_CUTSCENE_VOICE]=100;assert(vocalgain::Filter(raw.data())==raw.data());values[QOL_CUTSCENE_VOICE]=50;
    // Runtime bookkeeping differs from disk; preserve it in the gained copy.
    raw[5]=0x31;raw[9]=0x42;
    auto live=static_cast<unsigned char*>(vocalgain::Filter(raw.data()));
    assert(live!=raw.data()&&live!=output&&live[5]==0x31&&live[9]==0x42);
    assert(vocalgain::Filter(raw.data())==live);
    // Resolved bank/wave references are live data, not serialized hashes.
    raw.back()^=1;auto resolved=static_cast<unsigned char*>(vocalgain::Filter(raw.data()));
    assert(resolved!=raw.data()&&resolved!=live&&resolved[28]==raw[28]);
    assert(vocalgain::Filter(raw.data(),1,false)==raw.data());
    assert(vocalgain::Filter(raw.data(),voicevolume::NameHash(entry.name),true)==resolved);
    assert(vocalgain::Filter(raw.data(),voicevolume::NameHash("CUTSCENES_OTHER_CENTRE"),true)==raw.data());
    raw[11]^=2;assert(vocalgain::Filter(raw.data())==raw.data());raw=entry.bytes;
    values[QOL_CUTSCENE_VOICE]=800;assert(vocalgain::Filter(raw.data())==raw.data());
    values[QOL_CUTSCENE_VOICE]=50;
    vocalgain::copies=8192;values[QOL_CUTSCENE_VOICE]=25;assert(vocalgain::Filter(raw.data())==raw.data());
    vocalgain::copies=0;vocalgain::records[0]->entry.cutscene=false;assert(vocalgain::Filter(raw.data())==raw.data());
    assert(vocalgain::Filter(nullptr)==nullptr);assert(vocalgain::Filter(reinterpret_cast<void*>(1))==reinterpret_cast<void*>(1));
    return 0;
}
