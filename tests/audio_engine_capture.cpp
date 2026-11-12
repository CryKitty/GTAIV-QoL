#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cassert>
void Log(const char*){}
bool Read(const void* p,void* out,SIZE_T n){SIZE_T got=0;return ReadProcessMemory(GetCurrentProcess(),p,out,n,&got)&&got==n;}
bool Executable(const void* p){MEMORY_BASIC_INFORMATION m{};return p&&VirtualQuery(p,&m,sizeof(m))&&(m.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE));}
#include "../src/audio_engine_capture.hpp"
struct Fake {void** table;unsigned refs=1,starts=0,stops=0;bool fail=false;};
ULONG WINAPI Add(void* p){return ++static_cast<Fake*>(p)->refs;}
HRESULT WINAPI FakeStart(void* p){auto f=static_cast<Fake*>(p);++f->starts;return f->fail?E_FAIL:S_OK;}
void WINAPI FakeStop(void* p){++static_cast<Fake*>(p)->stops;}
ULONG WINAPI Release(void* p){return --static_cast<Fake*>(p)->refs;}
HRESULT WINAPI Query(void* p,REFIID iid,void** out){if(!audioengine::Equal(iid,audioengine::audioIID))return E_NOINTERFACE;*out=p;Add(p);return S_OK;}
Fake* factoryResult=nullptr;
HRESULT WINAPI FakeFactory(REFCLSID,LPUNKNOWN,DWORD,REFIID,LPVOID* out){*out=factoryResult;return S_OK;}
HRESULT modernResult=S_OK;
UINT32 seenFlags=0,seenProcessor=0;
__declspec(noinline) HRESULT WINAPI FakeModern(void** out,UINT32 flags,UINT32 processor){seenFlags=flags;seenProcessor=processor;*out=SUCCEEDED(modernResult)?factoryResult:nullptr;return modernResult;}
FARPROC WINAPI FakeResolver(HMODULE,LPCSTR name){
    if(reinterpret_cast<std::uintptr_t>(name)<=0xffffu)return reinterpret_cast<FARPROC>(&FakeFactory);
    return std::strcmp(name,"XAudio2Create")==0?reinterpret_cast<FARPROC>(&FakeModern):reinterpret_cast<FARPROC>(&FakeFactory);
}
int main(){
    using namespace audioengine;
    assert(!Available()&&!Stopped()&&!StopAll());
    void* table[16]{};table[0]=reinterpret_cast<void*>(&Query);table[1]=reinterpret_cast<void*>(&Add);table[2]=reinterpret_cast<void*>(&Release);table[11]=reinterpret_cast<void*>(&FakeStart);table[12]=reinterpret_cast<void*>(&FakeStop);
    Fake object{table};factoryResult=&object;factory=FakeFactory;void* out=nullptr;
    GUID other{};assert(SUCCEEDED(Create(other,nullptr,0,audioIID,&out)));assert(count==0);
    // Known class requested through another interface is queried for IXAudio2.
    assert(SUCCEEDED(Create(classes[7],nullptr,0,other,&out)));assert(count==1&&object.refs==2);
    assert(SUCCEEDED(Create(classes[7],nullptr,0,audioIID,&out)));assert(count==1&&object.refs==2&&Available());
    Capture(&object);assert(count==1&&object.refs==2);
    assert(StopAll()&&Stopped()&&object.stops==1);assert(StopAll()&&object.stops==1);
    const auto guardedLegacy=reinterpret_cast<Start>(object.table[11]);
    assert(guardedLegacy(&object)==S_OK&&object.starts==0&&engines[0].blockedStarts==1);
    object.fail=true;assert(!ResumeAll()&&Stopped());
    object.fail=false;assert(ResumeAll()&&!Stopped()&&object.starts==2);
    assert(ResumeAll()&&object.starts==2);
    assert(guardedLegacy(&object)==S_OK&&object.starts==3);
    // Modern ABI has thirteen methods; legacy has sixteen.
    // Distinct tables ensure old offsets can never accidentally pass this test.
    void* modernTable[13]{};modernTable[1]=reinterpret_cast<void*>(&Add);modernTable[2]=reinterpret_cast<void*>(&Release);
    modernTable[8]=reinterpret_cast<void*>(&FakeStart);modernTable[9]=reinterpret_cast<void*>(&FakeStop);
    Fake modern{modernTable};factoryResult=&modern;
    HMODULE module=LoadLibraryA("xaudio2_9.dll");assert(module&&ModernModule(module)==1);
    resolver=FakeResolver;
    const auto wrapped=reinterpret_cast<ModernFactory>(Resolve(module,"XAudio2Create"));
    assert(wrapped!=FakeModern);
    modernResult=E_FAIL;assert(FAILED(wrapped(&out,17,3)));assert(count==1&&seenFlags==17&&seenProcessor==3);
    modernResult=S_OK;assert(SUCCEEDED(wrapped(&out,0,1)));assert(count==2&&modern.refs==2&&seenFlags==0&&seenProcessor==1);
    assert(SUCCEEDED(wrapped(&out,0,1))&&count==2&&modern.refs==2);
    assert(Resolve(module,"OtherExport")==reinterpret_cast<FARPROC>(&FakeFactory));
    assert(Resolve(module,MAKEINTRESOURCEA(1))==reinterpret_cast<FARPROC>(&FakeFactory));
    assert(Resolve(GetModuleHandleA(nullptr),"XAudio2Create")==reinterpret_cast<FARPROC>(&FakeModern));
    assert(StopAll()&&modern.stops==1&&object.stops==2);
    const auto guardedModern=reinterpret_cast<Start>(modern.table[8]);
    assert(guardedModern(&modern)==S_OK&&modern.starts==0&&engines[1].blockedStarts==1);
    assert(ResumeAll()&&modern.starts==1&&!Stopped());
    // Calls from another module bypass the game executable's import wrappers.
    // An export detour must catch that route without recursion or a second engine.
    modernFactories[1]=nullptr;
    assert(DetourModern(1,reinterpret_cast<void*>(&FakeModern)));
    ModernFactory volatile externalCall=&FakeModern;
    modernResult=E_FAIL;assert(FAILED(externalCall(&out,29,7))&&seenFlags==29&&seenProcessor==7);
    modernResult=S_OK;assert(SUCCEEDED(externalCall(&out,0,1))&&count==2&&modern.refs==2);
    assert(Resolve(module,"XAudio2Create")==reinterpret_cast<FARPROC>(&ModernCreate<1>));
    assert(StopAll()&&ResumeAll()&&modern.stops==2&&modern.starts==2);
    assert(MH_DisableHook(reinterpret_cast<void*>(&FakeModern))==MH_OK);
    assert(MH_RemoveHook(reinterpret_cast<void*>(&FakeModern))==MH_OK);
    FreeLibrary(module);
    return 0;
}
