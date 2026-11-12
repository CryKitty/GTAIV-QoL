#pragma once
#include <atomic>
#include "cutscene_mix.hpp"
namespace cutsceneoutput {
std::atomic<float> gain{1};
using Set=HRESULT(WINAPI*)(void*,float,UINT32);
using Get=void(WINAPI*)(void*,float*);
using Destroy=void(WINAPI*)(void*);
struct Voice {void* object=nullptr;void* table[20]{};Set set=nullptr;Get get=nullptr;Destroy destroy=nullptr;float base=1;bool retiring=false;};
Voice voices[8]{};SRWLOCK lock=SRWLOCK_INIT;
HRESULT WINAPI SetLevel(void* object,float level,UINT32 operation){
    AcquireSRWLockExclusive(&lock);HRESULT result=E_FAIL;
    for(auto& v:voices)if(v.object==object){v.base=level;result=v.set(object,level*gain.load(),operation);break;}
    ReleaseSRWLockExclusive(&lock);return result;
}
void WINAPI GetLevel(void* object,float* level){
    AcquireSRWLockShared(&lock);for(auto& v:voices)if(v.object==object){if(level)*level=v.base;break;}ReleaseSRWLockShared(&lock);
}
void WINAPI DestroyVoice(void* object){
    AcquireSRWLockExclusive(&lock);Destroy original=nullptr;
    Voice* retiring=nullptr;
    for(auto& v:voices)if(v.object==object){original=v.destroy;v.object=nullptr;v.retiring=true;retiring=&v;break;}
    ReleaseSRWLockExclusive(&lock);
    if(original)original(object);
    if(retiring){AcquireSRWLockExclusive(&lock);retiring->retiring=false;ReleaseSRWLockExclusive(&lock);}
}

void Capture(void* object,bool modern){
    if(!object)return;AcquireSRWLockExclusive(&lock);
    for(auto& v:voices)if(v.object==object){ReleaseSRWLockExclusive(&lock);return;}
    Voice* selected=nullptr;for(auto& v:voices)if(!v.object&&!v.retiring){selected=&v;break;}
    void** table=nullptr;void* methods[20]{};const unsigned count=modern?20u:19u;
    if(!selected||!Read(object,&table,sizeof(table))||!Read(table,methods,count*sizeof(void*))||!Executable(methods[12])||!Executable(methods[13])||!Executable(methods[18])){ReleaseSRWLockExclusive(&lock);return;}
    auto& v=*selected;v.set=reinterpret_cast<Set>(methods[12]);v.get=reinterpret_cast<Get>(methods[13]);v.destroy=reinterpret_cast<Destroy>(methods[18]);v.get(object,&v.base);
    std::memcpy(v.table,methods,count*sizeof(void*));v.table[12]=reinterpret_cast<void*>(&SetLevel);v.table[13]=reinterpret_cast<void*>(&GetLevel);v.table[18]=reinterpret_cast<void*>(&DestroyVoice);
    DWORD protection=0;
    if(VirtualProtect(object,sizeof(void*),PAGE_READWRITE,&protection)){
        v.object=object;
        if(InterlockedCompareExchangePointer(reinterpret_cast<void**>(object),v.table,table)!=table)v.object=nullptr;
        DWORD ignored=0;VirtualProtect(object,sizeof(void*),protection,&ignored);
        if(v.object){v.set(object,v.base*gain.load(),0);Log("Cutscene audio: game mastering output captured");}
    }
    ReleaseSRWLockExclusive(&lock);
}
void Apply(float multiplier){
    const float previous=gain.exchange(multiplier);if(previous==multiplier)return;
    AcquireSRWLockExclusive(&lock);for(auto& v:voices)if(v.object)v.set(v.object,v.base*multiplier,0);ReleaseSRWLockExclusive(&lock);
    char message[100];sprintf_s(message,"Cutscene audio: output gain %.2f; gameplay restores 1.00",multiplier);Log(message);
}
}
