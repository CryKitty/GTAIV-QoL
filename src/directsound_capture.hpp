#pragma once
#include "cutscene_output.hpp"
// DirectSound secondary Stop/Play preserves the playback cursor. Primary
// buffers are excluded because stopping them may reset their cursor.
namespace directaudio {
using Play=HRESULT(WINAPI*)(void*,DWORD,DWORD,DWORD);
using Stop=HRESULT(WINAPI*)(void*);
using Status=HRESULT(WINAPI*)(void*,DWORD*);
using CreateBuffer=HRESULT(WINAPI*)(void*,const DSBUFFERDESC*,void**,IUnknown*);
using Duplicate=HRESULT(WINAPI*)(void*,void*,void**);
using Factory=HRESULT(WINAPI*)(LPCGUID,void**,IUnknown*);
struct Interface {void* object=nullptr;void* table[24]{};Play play=nullptr;Stop stop=nullptr;Status status=nullptr;void* setVolume=nullptr;};
struct Buffer {Interface views[2];LONG baseVolume=0;bool resume=false;DWORD reserved=0,priority=0,flags=0;};
struct Device {void* object=nullptr;void* table[12]{};CreateBuffer create=nullptr;Duplicate duplicate=nullptr;};
Buffer buffers[128]{};Device devices[8]{};unsigned bufferCount=0,deviceCount=0;
bool paused=false,overflow=false;
SRWLOCK operation=SRWLOCK_INIT;
Factory factories[2]{};
bool SwapTable(void* object,void** oldTable,void** newTable){
    DWORD protection=0;if(!VirtualProtect(object,sizeof(void*),PAGE_READWRITE,&protection))return false;
    const bool ok=InterlockedCompareExchangePointer(reinterpret_cast<void**>(object),newTable,oldTable)==oldTable;
    DWORD ignored=0;if(!VirtualProtect(object,sizeof(void*),protection,&ignored))Log("DirectSound interface page protection restoration failed");return ok;
}
Buffer* Find(void* object,Interface** view=nullptr){for(unsigned i=0;i<bufferCount;++i)for(auto& v:buffers[i].views)if(v.object==object){if(view)*view=&v;return &buffers[i];}return nullptr;}
HRESULT WINAPI GuardPlay(void* object,DWORD reserved,DWORD priority,DWORD flags){
    AcquireSRWLockExclusive(&operation);Interface* view=nullptr;Buffer* b=Find(object,&view);HRESULT hr=E_FAIL;
    if(b){
        if(paused){b->resume=true;b->reserved=reserved;b->priority=priority;b->flags=flags;hr=DS_OK;}
        else {hr=view->play(object,reserved,priority,flags);if(SUCCEEDED(hr)){b->reserved=reserved;b->priority=priority;b->flags=flags;}}
    }
    ReleaseSRWLockExclusive(&operation);return hr;
}
HRESULT WINAPI GuardStop(void* object){
    AcquireSRWLockExclusive(&operation);Interface* view=nullptr;Buffer* b=Find(object,&view);HRESULT hr=E_FAIL;
    if(b){b->resume=false;hr=view->stop(object);}ReleaseSRWLockExclusive(&operation);return hr;
}
HRESULT WINAPI GuardStatus(void* object,DWORD* status){
    AcquireSRWLockExclusive(&operation);Interface* view=nullptr;Buffer* b=Find(object,&view);HRESULT hr=E_FAIL;
    if(b){hr=view->status(object,status);if(SUCCEEDED(hr)&&status&&paused&&b->resume)*status|=DSBSTATUS_PLAYING|((b->flags&DSBPLAY_LOOPING)?DSBSTATUS_LOOPING:0);}
    ReleaseSRWLockExclusive(&operation);return hr;
}
using SetVolume=HRESULT(WINAPI*)(void*,LONG);
HRESULT WINAPI GuardVolume(void* object,LONG value){
    AcquireSRWLockExclusive(&operation);Interface* view=nullptr;Buffer* b=Find(object,&view);HRESULT result=E_FAIL;
    if(b){b->baseVolume=value;result=reinterpret_cast<SetVolume>(view->setVolume)(object,cutscenemix::decibels(value,cutsceneoutput::gain.load()));}
    ReleaseSRWLockExclusive(&operation);return result;
}
HRESULT WINAPI GuardGetVolume(void* object,LONG* value){
    AcquireSRWLockShared(&operation);Buffer* b=Find(object);if(b&&value)*value=b->baseVolume;ReleaseSRWLockShared(&operation);return b?DS_OK:E_FAIL;
}
void ApplyVolume(){
    AcquireSRWLockExclusive(&operation);for(unsigned i=0;i<bufferCount;++i){auto& b=buffers[i];auto& v=b.views[0];if(v.object)reinterpret_cast<SetVolume>(v.setVolume)(v.object,cutscenemix::decibels(b.baseVolume,cutsceneoutput::gain.load()));}ReleaseSRWLockExclusive(&operation);
}
bool Prepare(Interface& v,void* object,unsigned methods){
    void** table=nullptr;if(!Read(object,&table,sizeof(table))||!Read(table,v.table,methods*sizeof(void*))||!Executable(v.table[12])||!Executable(v.table[18])||!Executable(v.table[9]))return false;
    v.object=object;v.play=reinterpret_cast<Play>(v.table[12]);v.stop=reinterpret_cast<Stop>(v.table[18]);v.status=reinterpret_cast<Status>(v.table[9]);
    v.setVolume=v.table[15];v.table[15]=reinterpret_cast<void*>(&GuardVolume);v.table[6]=reinterpret_cast<void*>(&GuardGetVolume);
    v.table[12]=reinterpret_cast<void*>(&GuardPlay);v.table[18]=reinterpret_cast<void*>(&GuardStop);v.table[9]=reinterpret_cast<void*>(&GuardStatus);
    if(!SwapTable(object,table,v.table)){v.object=nullptr;return false;}return true;
}
void CaptureBuffer(void* object){
    if(!object)return;
    auto base=static_cast<IDirectSoundBuffer*>(object);DSBCAPS caps{};caps.dwSize=sizeof(caps);
    if(FAILED(base->GetCaps(&caps))||(caps.dwFlags&DSBCAPS_PRIMARYBUFFER))return;
    AcquireSRWLockExclusive(&operation);
    if(Find(object)){ReleaseSRWLockExclusive(&operation);return;}
    if(bufferCount>=128){overflow=true;ReleaseSRWLockExclusive(&operation);Log("DirectSound buffer registry full; pause disabled");return;}
    IDirectSoundBuffer8* extended=nullptr;const bool modern=SUCCEEDED(base->QueryInterface(IID_IDirectSoundBuffer8,reinterpret_cast<void**>(&extended)))&&extended;
    Buffer& entry=buffers[bufferCount];base->GetVolume(&entry.baseVolume);base->AddRef();
    // The retained interfaces and private tables live until process exit. This
    // bounded experiment never scans unknown memory for existing sound objects.
    if(!Prepare(entry.views[0],object,modern&&extended==object?24u:21u)){
        base->Release();if(extended)extended->Release();ReleaseSRWLockExclusive(&operation);Log("DirectSound buffer capture failed");return;
    }
    if(modern&&extended!=object&&!Prepare(entry.views[1],extended,24u)){
        // The controlling base interface remains usable; it pauses the shared buffer.
        extended->Release();Log("DirectSound extended interface restart guard unavailable");
    }
    reinterpret_cast<SetVolume>(entry.views[0].setVolume)(object,cutscenemix::decibels(entry.baseVolume,cutsceneoutput::gain.load()));
    ++bufferCount;
    if(extended==object)extended->Release(); // base reference is sufficient here
    char line[160];sprintf_s(line,"DirectSound secondary buffer captured object=%p bytes=%lu count=%u",object,caps.dwBufferBytes,bufferCount);Log(line);
    ReleaseSRWLockExclusive(&operation);
}
HRESULT WINAPI Create(void* object,const DSBUFFERDESC* desc,void** result,IUnknown* outer){
    CreateBuffer original=nullptr;AcquireSRWLockShared(&operation);for(unsigned i=0;i<deviceCount;++i)if(devices[i].object==object)original=devices[i].create;ReleaseSRWLockShared(&operation);
    if(!original)return E_FAIL;const HRESULT hr=original(object,desc,result,outer);
    if(SUCCEEDED(hr)&&result&&*result&&!outer)CaptureBuffer(*result);return hr;
}
HRESULT WINAPI Copy(void* object,void* source,void** result){
    Duplicate original=nullptr;AcquireSRWLockShared(&operation);for(unsigned i=0;i<deviceCount;++i)if(devices[i].object==object)original=devices[i].duplicate;ReleaseSRWLockShared(&operation);
    if(!original)return E_FAIL;const HRESULT hr=original(object,source,result);if(SUCCEEDED(hr)&&result&&*result)CaptureBuffer(*result);return hr;
}
void CaptureDevice(void* object,bool modern){
    if(!object)return;AcquireSRWLockExclusive(&operation);
    for(unsigned i=0;i<deviceCount;++i)if(devices[i].object==object){ReleaseSRWLockExclusive(&operation);return;}
    if(deviceCount>=8){overflow=true;ReleaseSRWLockExclusive(&operation);return;}
    Device& d=devices[deviceCount];void** table=nullptr;
    if(!Read(object,&table,sizeof(table))||!Read(table,d.table,(modern?12u:11u)*sizeof(void*))||!Executable(d.table[3])||!Executable(d.table[5])){ReleaseSRWLockExclusive(&operation);return;}
    d.object=object;d.create=reinterpret_cast<CreateBuffer>(d.table[3]);d.duplicate=reinterpret_cast<Duplicate>(d.table[5]);d.table[3]=reinterpret_cast<void*>(&Create);d.table[5]=reinterpret_cast<void*>(&Copy);
    static_cast<IUnknown*>(object)->AddRef();
    if(!SwapTable(object,table,d.table)){static_cast<IUnknown*>(object)->Release();d.object=nullptr;ReleaseSRWLockExclusive(&operation);return;}
    ++deviceCount;ReleaseSRWLockExclusive(&operation);Log("DirectSound game device captured; secondary buffer creation monitored");
}
template<int Index> HRESULT WINAPI Make(LPCGUID guid,void** result,IUnknown* outer){
    const HRESULT hr=factories[Index](guid,result,outer);
    if(SUCCEEDED(hr)&&result&&*result&&!outer)CaptureDevice(*result,Index==1);return hr;
}
void Install(){
    HMODULE module=LoadLibraryA("dsound.dll");if(!module){Log("DirectSound module unavailable");return;}
    const MH_STATUS init=MH_Initialize();if(init!=MH_OK&&init!=MH_ERROR_ALREADY_INITIALIZED)return;
    const char* names[]={"DirectSoundCreate","DirectSoundCreate8"};void* wrappers[]={reinterpret_cast<void*>(&Make<0>),reinterpret_cast<void*>(&Make<1>)};
    for(unsigned i=0;i<2;++i){void* target=reinterpret_cast<void*>(GetProcAddress(module,names[i]));void* trampoline=nullptr;
        if(target&&MH_CreateHook(target,wrappers[i],&trampoline)==MH_OK){factories[i]=reinterpret_cast<Factory>(trampoline);if(MH_EnableHook(target)==MH_OK)Log("DirectSound factory capture installed");else {MH_RemoveHook(target);factories[i]=nullptr;}}
    }
}
bool Available(){AcquireSRWLockShared(&operation);const bool result=bufferCount>0&&!overflow;ReleaseSRWLockShared(&operation);return result;}
bool StopAll(){
    AcquireSRWLockExclusive(&operation);if(overflow){ReleaseSRWLockExclusive(&operation);return false;}paused=true;bool ok=true;unsigned stopped=0;
    for(unsigned i=0;i<bufferCount;++i){auto& b=buffers[i];auto& v=b.views[0];DWORD status=0;
        if(FAILED(v.status(v.object,&status))){ok=false;continue;}
        if(!(status&DSBSTATUS_PLAYING)||b.resume)continue;
        b.flags=(b.flags&~DSBPLAY_LOOPING)|((status&DSBSTATUS_LOOPING)?DSBPLAY_LOOPING:0);
        if(SUCCEEDED(v.stop(v.object))){b.resume=true;++stopped;}else ok=false;
    }
    char line[160];sprintf_s(line,"DirectSound pause: captured=%u playing_stopped=%u success=%u",bufferCount,stopped,ok?1u:0u);Log(line);
    ReleaseSRWLockExclusive(&operation);return ok;
}
bool ResumeAll(){
    AcquireSRWLockExclusive(&operation);bool ok=true;unsigned resumed=0;
    for(unsigned i=0;i<bufferCount;++i){auto& b=buffers[i];if(!b.resume)continue;auto& v=b.views[0];
        if(SUCCEEDED(v.play(v.object,b.reserved,b.priority,b.flags))){b.resume=false;++resumed;}else ok=false;
    }
    if(ok)paused=false;
    if(resumed){char line[120];sprintf_s(line,"DirectSound resume: buffers=%u; preserved playback cursors",resumed);Log(line);}ReleaseSRWLockExclusive(&operation);return ok;
}
bool Stopped(){AcquireSRWLockShared(&operation);const bool result=paused;ReleaseSRWLockShared(&operation);return result;}
}
