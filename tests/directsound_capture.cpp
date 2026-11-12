#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include <mmsystem.h>
#include <dsound.h>
#include <cstdio>
#include <cstring>
#include <cassert>
#include "../vendor/minhook/include/MinHook.h"
void Log(const char*){}
bool Read(const void* p,void* out,SIZE_T n){SIZE_T got=0;return ReadProcessMemory(GetCurrentProcess(),p,out,n,&got)&&got==n;}
bool Executable(const void* p){MEMORY_BASIC_INFORMATION m{};return p&&VirtualQuery(p,&m,sizeof(m))&&(m.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE));}
#include "../src/directsound_capture.hpp"
struct Fake {void** table;unsigned refs=1,starts=0,stops=0,cursor=1234;DWORD flags=0,caps=0;bool playing=false,fail=false,modern=true;LONG volume=-1000;};
ULONG WINAPI Add(void* object){return ++static_cast<Fake*>(object)->refs;}
ULONG WINAPI Release(void* object){return --static_cast<Fake*>(object)->refs;}
HRESULT WINAPI Query(void* object,REFIID,void** out){auto f=static_cast<Fake*>(object);if(!f->modern)return E_NOINTERFACE;Add(object);*out=object;return S_OK;}
HRESULT WINAPI Caps(void* object,DSBCAPS* caps){caps->dwFlags=static_cast<Fake*>(object)->caps;caps->dwBufferBytes=48000;return S_OK;}
HRESULT WINAPI Status(void* object,DWORD* out){auto f=static_cast<Fake*>(object);*out=(f->playing?DSBSTATUS_PLAYING:0)|((f->flags&DSBPLAY_LOOPING)?DSBSTATUS_LOOPING:0);return S_OK;}
HRESULT WINAPI Play(void* object,DWORD,DWORD,DWORD flags){auto f=static_cast<Fake*>(object);if(f->fail)return E_FAIL;++f->starts;f->playing=true;f->flags=flags;return S_OK;}
HRESULT WINAPI Stop(void* object){auto f=static_cast<Fake*>(object);if(f->fail)return E_FAIL;++f->stops;f->playing=false;return S_OK;}
HRESULT WINAPI Cursor(void* object,DWORD* play,DWORD* write){if(play)*play=static_cast<Fake*>(object)->cursor;if(write)*write=static_cast<Fake*>(object)->cursor+100;return S_OK;}
Fake* nextBuffer=nullptr;
HRESULT WINAPI CreateBuffer(void*,const DSBUFFERDESC*,void** out,IUnknown*){*out=nextBuffer;return S_OK;}
HRESULT WINAPI Duplicate(void*,void*,void** out){*out=nextBuffer;return S_OK;}
HRESULT WINAPI GetVolume(void* object,LONG* value){*value=static_cast<Fake*>(object)->volume;return DS_OK;}
HRESULT WINAPI SetVolume(void* object,LONG value){static_cast<Fake*>(object)->volume=value;return DS_OK;}
int main(){
    namespace d=directaudio;
    void* table[24]{};table[6]=reinterpret_cast<void*>(&GetVolume);table[15]=reinterpret_cast<void*>(&SetVolume);table[0]=reinterpret_cast<void*>(&Query);table[1]=reinterpret_cast<void*>(&Add);table[2]=reinterpret_cast<void*>(&Release);table[3]=reinterpret_cast<void*>(&Caps);table[4]=reinterpret_cast<void*>(&Cursor);table[9]=reinterpret_cast<void*>(&Status);table[12]=reinterpret_cast<void*>(&Play);table[18]=reinterpret_cast<void*>(&Stop);
    Fake primary{table};primary.caps=DSBCAPS_PRIMARYBUFFER;d::CaptureBuffer(&primary);assert(d::bufferCount==0&&primary.refs==1);
    void* devTable[12]{};devTable[1]=reinterpret_cast<void*>(&Add);devTable[2]=reinterpret_cast<void*>(&Release);devTable[3]=reinterpret_cast<void*>(&CreateBuffer);devTable[5]=reinterpret_cast<void*>(&Duplicate);
    Fake device{devTable};d::CaptureDevice(&device,true);assert(device.refs==2);
    Fake audio{table};nextBuffer=&audio;void* out=nullptr;DSBUFFERDESC desc{};
    auto create=reinterpret_cast<d::CreateBuffer>(device.table[3]);assert(SUCCEEDED(create(&device,&desc,&out,nullptr))&&out==&audio&&d::Available());
    cutsceneoutput::Apply(.5f);d::ApplyVolume();assert(audio.volume<-1500);
    d::GuardVolume(&audio,-2000);assert(audio.volume<-2500);
    LONG requested=0;d::GuardGetVolume(&audio,&requested);assert(requested==-2000);
    cutsceneoutput::Apply(1);d::ApplyVolume();assert(audio.volume==-2000);
    assert(audio.refs==2);d::CaptureBuffer(&audio);assert(audio.refs==2&&d::bufferCount==1);
    auto play=reinterpret_cast<d::Play>(audio.table[12]);auto stop=reinterpret_cast<d::Stop>(audio.table[18]);auto status=reinterpret_cast<d::Status>(audio.table[9]);
    assert(SUCCEEDED(play(&audio,0,0,DSBPLAY_LOOPING))&&audio.playing&&audio.starts==1);
    assert(d::StopAll()&&d::Stopped()&&!audio.playing&&audio.cursor==1234);
    DWORD state=0;assert(SUCCEEDED(status(&audio,&state))&&(state&DSBSTATUS_PLAYING)&&(state&DSBSTATUS_LOOPING));
    assert(SUCCEEDED(play(&audio,0,0,DSBPLAY_LOOPING))&&!audio.playing&&audio.starts==1);
    assert(d::ResumeAll()&&audio.playing&&audio.cursor==1234&&audio.starts==2&&audio.flags==DSBPLAY_LOOPING);
    assert(d::StopAll());assert(SUCCEEDED(stop(&audio)));assert(d::ResumeAll()&&!audio.playing&&audio.starts==2); // game's stop cancels our resume
    Fake idle{table};idle.modern=false;nextBuffer=&idle;
    auto duplicate=reinterpret_cast<d::Duplicate>(device.table[5]);assert(SUCCEEDED(duplicate(&device,&audio,&out))&&d::bufferCount==2);
    assert(d::StopAll()&&!idle.playing);auto idlePlay=reinterpret_cast<d::Play>(idle.table[12]);assert(SUCCEEDED(idlePlay(&idle,0,0,0))&&!idle.playing);
    idle.fail=true;assert(!d::ResumeAll()&&d::Stopped());idle.fail=false;assert(d::ResumeAll()&&idle.playing&&idle.flags==0&&idle.cursor==1234);
    assert(d::StopAll()&&d::ResumeAll()&&idle.starts==2); // repeated pauses retain position
    audio.fail=true;audio.playing=true;assert(!d::StopAll());audio.fail=false;assert(d::ResumeAll());
    return 0;
}
