#pragma once
#include <atomic>
#include "cutscene_mix.hpp"
namespace cutsceneoutput {
std::atomic<float> gain{1};
using Set=HRESULT(WINAPI*)(void*,float,UINT32);
using Get=void(WINAPI*)(void*,float*);
using Destroy=void(WINAPI*)(void*);
struct Voice {void* object=nullptr;void* table[20]{};Set set=nullptr;Get get=nullptr;Destroy destroy=nullptr;float base=1,last=1,pendingBase=1;bool tracked=false,pending=false,retiring=false;};
Voice voices[8]{};SRWLOCK lock=SRWLOCK_INIT;
inline bool close(float a,float b){return std::isfinite(a)&&std::isfinite(b)&&std::fabs(a-b)<=.0001f*std::max(1.0f,std::fabs(b));}
inline bool valid(float level){return std::isfinite(level)&&level>=0&&level<=16777216.0f;}
HRESULT WINAPI SetLevel(void* object,float level,UINT32 operation){
    AcquireSRWLockExclusive(&lock);HRESULT result=E_FAIL;
    for(auto& v:voices)if(v.object==object){
        // Deferred game operations remain unboosted in the engine's queue: a
        // later commit must never replay an old cutscene gain into gameplay.
        result=v.set(object,operation?level:level*gain.load(),operation);
        if(SUCCEEDED(result)&&valid(level)){
            if(operation){v.pending=true;v.pendingBase=level;}
            else {v.base=level;v.pending=false;v.get(object,&v.last);v.tracked=valid(v.last);}
        }
        break;
    }
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
    auto& v=*selected;v.set=reinterpret_cast<Set>(methods[12]);v.get=reinterpret_cast<Get>(methods[13]);v.destroy=reinterpret_cast<Destroy>(methods[18]);v.get(object,&v.base);v.tracked=false;v.pending=false;
    if(!valid(v.base)){ReleaseSRWLockExclusive(&lock);Log("Cutscene audio: invalid mastering baseline; capture rejected");return;}
    std::memcpy(v.table,methods,count*sizeof(void*));v.table[12]=reinterpret_cast<void*>(&SetLevel);v.table[13]=reinterpret_cast<void*>(&GetLevel);v.table[18]=reinterpret_cast<void*>(&DestroyVoice);
    DWORD protection=0;
    if(VirtualProtect(object,sizeof(void*),PAGE_READWRITE,&protection)){
        v.object=object;
        if(InterlockedCompareExchangePointer(reinterpret_cast<void**>(object),v.table,table)!=table)v.object=nullptr;
        DWORD ignored=0;VirtualProtect(object,sizeof(void*),protection,&ignored);
        if(v.object){v.set(object,v.base*gain.load(),0);v.get(object,&v.last);v.tracked=valid(v.last);Log("Cutscene audio: game mastering output captured");}
    }
    ReleaseSRWLockExclusive(&lock);
}
struct Evidence {unsigned outputs=0,verified=0,failed=0,rebased=0;float base=0,before=0,target=0,after=0;};
Evidence Apply(float multiplier){
    multiplier=std::isfinite(multiplier)?std::clamp(multiplier,0.0f,16.0f):1.0f;
    const float previous=gain.exchange(multiplier);Evidence evidence;
    AcquireSRWLockExclusive(&lock);
    for(auto& v:voices)if(v.object){
        ++evidence.outputs;float actual=0;v.get(v.object,&actual);
        if(!valid(actual)){++evidence.failed;continue;}
        // A cached native setter can bypass our private vtable. Reconcile on
        // every normal tick, against the raw output, never the previous boost.
        if(v.pending&&close(actual,v.pendingBase)){v.base=v.pendingBase;v.pending=false;}
        else if(v.tracked&&!close(actual,v.last)){v.base=actual;++evidence.rebased;}
        const float target=std::min(v.base*multiplier,16777216.0f);
        HRESULT result=S_OK;
        if(!close(actual,target))result=v.set(v.object,target,0);
        float after=0;v.get(v.object,&after);v.last=after;v.tracked=valid(after);
        const bool verified=SUCCEEDED(result)&&close(after,target);
        if(verified)++evidence.verified;else ++evidence.failed;
        evidence.base=v.base;evidence.before=actual;evidence.target=target;evidence.after=after;
    }
    ReleaseSRWLockExclusive(&lock);
    static DWORD reported=0;const auto now=GetTickCount();
    if(previous!=multiplier||(multiplier!=1&&now-reported>=5000)){
        reported=now;char message[300];sprintf_s(message,"Cutscene audio output: gain=%.2f outputs=%u verified=%u failed=%u external-writes=%u base=%.5f raw-before=%.5f target=%.5f raw-after=%.5f",multiplier,evidence.outputs,evidence.verified,evidence.failed,evidence.rebased,evidence.base,evidence.before,evidence.target,evidence.after);Log(message);
    }
    return evidence;
}
}
