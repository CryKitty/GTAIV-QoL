#pragma once
#include "cutscene_output.hpp"
#include "../vendor/minhook/include/MinHook.h"
// XAudio2 <=2.7 uses COM; 2.8/2.9 use the exported XAudio2Create factory.
// Start/Stop slot layout and class/interface IDs
// are defined by the DirectX XAudio2 ABI (see docs/CUTSCENE_PAUSE.md references).
// No voice buffers, sample data, or internal engine fields are modified.
namespace audioengine {
using Start=HRESULT(WINAPI*)(void*);
using Stop=void(WINAPI*)(void*);
struct Engine {void* object=nullptr;Start start=nullptr;Stop stop=nullptr;bool stopped=false;bool modern=false;void* master=nullptr;void* guardedTable[16]{};unsigned blockedStarts=0;};
Engine engines[4]{};
LONG count=0;
bool overflow=false;
SRWLOCK registry=SRWLOCK_INIT,operation=SRWLOCK_INIT;
using Factory=HRESULT(WINAPI*)(REFCLSID,LPUNKNOWN,DWORD,REFIID,LPVOID*);
Factory factory=nullptr;
volatile LONG factoryEvents=0;
const GUID audioIID={0x8bcf1f58,0x9fe7,0x4583,{0x8a,0xc6,0xe2,0xad,0xc4,0x65,0xc8,0xbb}};
const GUID classes[]={
 {0xfac23f48,0x31f5,0x45a8,{0xb4,0x9b,0x52,0x25,0xd6,0x14,0x01,0xaa}},
 {0xe21a7345,0xeb21,0x468e,{0xbe,0x50,0x80,0x4d,0xb9,0x7c,0xf7,0x08}},
 {0xb802058a,0x464a,0x42db,{0xbc,0x10,0xb6,0x50,0xd6,0xf2,0x58,0x6a}},
 {0x4c5e637a,0x16c7,0x4de3,{0x9c,0x46,0x5e,0xd2,0x21,0x81,0x96,0x2d}},
 {0x03219e78,0x5bc3,0x44d1,{0xb9,0x2e,0xf6,0x3d,0x89,0xcc,0x65,0x26}},
 {0x4c9b6dde,0x6809,0x46e6,{0xa2,0x78,0x9b,0x6a,0x97,0x58,0x86,0x70}},
 {0x3eda9b49,0x2085,0x498b,{0x9b,0xb2,0x39,0xa6,0x77,0x84,0x93,0xde}},
 {0x5a508685,0xa254,0x4fba,{0x9b,0x82,0x9a,0x24,0xb0,0x03,0x06,0xaf}}
};
bool Equal(REFGUID a,REFGUID b){return std::memcmp(&a,&b,sizeof(GUID))==0;}
bool AudioClass(REFCLSID cls){for(const auto& candidate:classes)if(Equal(cls,candidate))return true;return false;}
HRESULT WINAPI GuardedStart(void* object){
    AcquireSRWLockExclusive(&operation);AcquireSRWLockShared(&registry);
    Engine* engine=nullptr;for(LONG i=0;i<count;++i)if(engines[i].object==object){engine=&engines[i];break;}
    ReleaseSRWLockShared(&registry);
    HRESULT hr=E_FAIL;
    if(engine){
        if(engine->stopped){++engine->blockedStarts;hr=S_OK;if(engine->blockedStarts==1)Log("game StartEngine request deferred while coordinated pause owns audio");}
        else hr=engine->start(object);
    }
    ReleaseSRWLockExclusive(&operation);return hr;
}
using LegacyMaster=HRESULT(WINAPI*)(void*,void**,UINT32,UINT32,UINT32,UINT32,const void*);
using ModernMaster=HRESULT(WINAPI*)(void*,void**,UINT32,UINT32,UINT32,LPCWSTR,const void*,UINT32);
HRESULT WINAPI MasterLegacy(void* object,void** result,UINT32 channels,UINT32 rate,UINT32 flags,UINT32 device,const void* effects){
    LegacyMaster original=nullptr;AcquireSRWLockShared(&registry);for(LONG i=0;i<count;++i)if(engines[i].object==object)original=reinterpret_cast<LegacyMaster>(engines[i].master);ReleaseSRWLockShared(&registry);
    if(!original)return E_FAIL;const auto hr=original(object,result,channels,rate,flags,device,effects);if(SUCCEEDED(hr)&&result)cutsceneoutput::Capture(*result,false);return hr;
}
HRESULT WINAPI MasterModern(void* object,void** result,UINT32 channels,UINT32 rate,UINT32 flags,LPCWSTR device,const void* effects,UINT32 category){
    ModernMaster original=nullptr;AcquireSRWLockShared(&registry);for(LONG i=0;i<count;++i)if(engines[i].object==object)original=reinterpret_cast<ModernMaster>(engines[i].master);ReleaseSRWLockShared(&registry);
    if(!original)return E_FAIL;const auto hr=original(object,result,channels,rate,flags,device,effects,category);if(SUCCEEDED(hr)&&result)cutsceneoutput::Capture(*result,true);return hr;
}
void Capture(void* object,bool modern=false){
    const unsigned startSlot=modern?8u:11u,stopSlot=modern?9u:12u;
    const unsigned methodCount=modern?13u:16u;
    void** table=nullptr;void* methods[16]{};
    if(!Read(object,&table,sizeof(table))||!Read(table,methods,methodCount*sizeof(void*))||!Executable(methods[1])||!Executable(methods[startSlot])||!Executable(methods[stopSlot])){Log("XAudio2 capture rejected: invalid interface methods");return;}
    AcquireSRWLockExclusive(&registry);
    for(LONG i=0;i<count;++i)if(engines[i].object==object){ReleaseSRWLockExclusive(&registry);return;}
    if(count>=4){overflow=true;ReleaseSRWLockExclusive(&registry);Log("XAudio2 capture overflow; coordinated pause disabled");return;}
    static_cast<IUnknown*>(object)->AddRef(); // Own a stable reference until process exit.
    Engine& entry=engines[count];entry.object=object;entry.start=reinterpret_cast<Start>(methods[startSlot]);entry.stop=reinterpret_cast<Stop>(methods[stopSlot]);
    std::memcpy(entry.guardedTable,methods,methodCount*sizeof(void*));entry.guardedTable[startSlot]=reinterpret_cast<void*>(&GuardedStart);
    entry.modern=modern;entry.master=methods[modern?7:10];entry.guardedTable[modern?7:10]=modern?reinterpret_cast<void*>(&MasterModern):reinterpret_cast<void*>(&MasterLegacy);
    DWORD protection=0;
    if(!VirtualProtect(object,sizeof(void*),PAGE_READWRITE,&protection)){entry.object=nullptr;ReleaseSRWLockExclusive(&registry);static_cast<IUnknown*>(object)->Release();Log("audio restart guard unavailable; engine capture rejected");return;}
    const bool swapped=InterlockedCompareExchangePointer(reinterpret_cast<void**>(object),entry.guardedTable,table)==table;
    DWORD ignored=0;const bool restored=VirtualProtect(object,sizeof(void*),protection,&ignored)!=0;
    if(!swapped){entry.object=nullptr;ReleaseSRWLockExclusive(&registry);static_cast<IUnknown*>(object)->Release();Log("audio interface changed; engine capture rejected");return;}
    ++count;if(!restored)Log("audio restart guard installed; page protection restoration failed");
    ReleaseSRWLockExclusive(&registry);
    char line[160];sprintf_s(line,"XAudio2 engine captured ABI=%s object=%p start=%p stop=%p",modern?"2.8/2.9":"legacy",object,methods[startSlot],methods[stopSlot]);Log(line);
}
HRESULT WINAPI Create(REFCLSID cls,LPUNKNOWN outer,DWORD context,REFIID iid,LPVOID* result){
    const HRESULT hr=factory(cls,outer,context,iid,result);
    if(InterlockedIncrement(&factoryEvents)<=24){char line[220];sprintf_s(line,"COM creation cls=%08lX-%04X-%04X iid=%08lX-%04X-%04X audio_class=%u hr=%08lX",cls.Data1,cls.Data2,cls.Data3,iid.Data1,iid.Data2,iid.Data3,AudioClass(cls)?1u:0u,static_cast<unsigned long>(hr));Log(line);}
    if(SUCCEEDED(hr)&&result&&*result&&!outer&&AudioClass(cls)){
        if(Equal(iid,audioIID))Capture(*result);
        else {void* audio=nullptr;if(SUCCEEDED(static_cast<IUnknown*>(*result)->QueryInterface(audioIID,&audio))&&audio){Capture(audio);static_cast<IUnknown*>(audio)->Release();}}
    }
    return hr;
}
bool Available(){AcquireSRWLockShared(&registry);const bool ok=count>0&&!overflow;ReleaseSRWLockShared(&registry);return ok;}
// Engine entries never move or lose their retained reference. Operation lock
// serializes pause/resume, without holding the registry lock during engine calls.
bool StopAll(){
    AcquireSRWLockExclusive(&operation);AcquireSRWLockShared(&registry);const LONG n=count;const bool ok=n>0&&!overflow;ReleaseSRWLockShared(&registry);
    if(ok)for(LONG i=0;i<n;++i)if(!engines[i].stopped){engines[i].stopped=true;engines[i].blockedStarts=0;engines[i].stop(engines[i].object);}
    ReleaseSRWLockExclusive(&operation);Log(ok?"XAudio2 StopEngine called; restart guard owns captured engines":"pause rejected: no captured XAudio2 engine");return ok;
}
bool ResumeAll(){
    AcquireSRWLockExclusive(&operation);AcquireSRWLockShared(&registry);const LONG n=count;ReleaseSRWLockShared(&registry);bool ok=true,changed=false;
    for(LONG i=0;i<n;++i)if(engines[i].stopped){const HRESULT hr=engines[i].start(engines[i].object);if(SUCCEEDED(hr)){engines[i].stopped=false;changed=true;char line[120];sprintf_s(line,"audio restart guard released; deferred_game_starts=%u",engines[i].blockedStarts);Log(line);}else ok=false;}
    ReleaseSRWLockExclusive(&operation);if(changed)Log("XAudio2 StartEngine: audio processing resumed");if(!ok)Log("XAudio2 StartEngine failed; recovery will retry");return ok;
}
bool Stopped(){AcquireSRWLockShared(&operation);AcquireSRWLockShared(&registry);const LONG n=count;ReleaseSRWLockShared(&registry);bool any=false;for(LONG i=0;i<n;++i)if(engines[i].stopped)any=true;ReleaseSRWLockShared(&operation);return any;}
// Modern XAudio2 does not go through COM. Intercept only the game's imports,
// including dynamically resolved factories. Never create a separate audio engine.
using ModernFactory=HRESULT(WINAPI*)(void**,UINT32,UINT32);
using Resolver=FARPROC(WINAPI*)(HMODULE,LPCSTR);
Resolver resolver=nullptr;
void* modernFactories[3]{};
void* modernTargets[3]{};
int ModernModule(HMODULE module){
    char path[MAX_PATH]{};if(!module||!GetModuleFileNameA(module,path,MAX_PATH))return -1;
    const char* name=path;for(const char* at=path;*at;++at)if(*at=='\\'||*at=='/')name=at+1;
    const char* names[]={"xaudio2_8.dll","xaudio2_9.dll","xaudio2_9redist.dll"};
    for(int i=0;i<3;++i)if(_stricmp(name,names[i])==0)return i;
    return -1;
}
template<int Index> HRESULT WINAPI ModernCreate(void** result,UINT32 flags,UINT32 processor){
    const auto original=reinterpret_cast<ModernFactory>(InterlockedCompareExchangePointer(&modernFactories[Index],nullptr,nullptr));
    if(!original)return E_FAIL;
    const HRESULT hr=original(result,flags,processor);
    char line[150];sprintf_s(line,"XAudio2Create modern module=%d hr=%08lX",Index,static_cast<unsigned long>(hr));Log(line);
    if(SUCCEEDED(hr)&&result&&*result)Capture(*result,true);
    return hr;
}
FARPROC WrapModern(HMODULE module,FARPROC original){
    const int index=ModernModule(module);if(index<0||!original)return original;
    FARPROC wrappers[]={reinterpret_cast<FARPROC>(&ModernCreate<0>),reinterpret_cast<FARPROC>(&ModernCreate<1>),reinterpret_cast<FARPROC>(&ModernCreate<2>)};
    if(original==wrappers[index])return original;
    if(reinterpret_cast<void*>(original)==modernTargets[index])return wrappers[index];
    void* address=reinterpret_cast<void*>(original);
    void* previous=InterlockedCompareExchangePointer(&modernFactories[index],address,nullptr);
    if(previous&&previous!=address){Log("modern audio factory changed; capture skipped");return original;}
    Log("XAudio2Create modern factory intercepted; awaiting game engine creation");return wrappers[index];
}
bool DetourModern(int index,void* target){
    if(index<0||index>=3||!target||!Executable(target))return false;
    if(modernTargets[index])return modernTargets[index]==target;
    void* wrappers[]={reinterpret_cast<void*>(&ModernCreate<0>),reinterpret_cast<void*>(&ModernCreate<1>),reinterpret_cast<void*>(&ModernCreate<2>)};
    const MH_STATUS initialized=MH_Initialize();
    if(initialized!=MH_OK&&initialized!=MH_ERROR_ALREADY_INITIALIZED){Log("modern audio factory detour initialization failed");return false;}
    void* trampoline=nullptr;
    const MH_STATUS created=MH_CreateHook(target,wrappers[index],&trampoline);
    if(created!=MH_OK){char line[120];sprintf_s(line,"modern audio factory detour creation failed status=%d",static_cast<int>(created));Log(line);return false;}
    // Publish the forwarding trampoline before the hook can receive calls.
    void* previous=InterlockedExchangePointer(&modernFactories[index],trampoline);
    modernTargets[index]=target;
    const MH_STATUS enabled=MH_EnableHook(target);
    if(enabled!=MH_OK){modernTargets[index]=nullptr;InterlockedExchangePointer(&modernFactories[index],previous);MH_RemoveHook(target);Log("modern audio factory detour activation failed");return false;}
    char line[160];sprintf_s(line,"modern XAudio2 factory detour active module=%d target=%p; captures calls from all game modules",index,target);Log(line);return true;
}
void InstallModernExports(){
    const char* names[]={"xaudio2_8.dll","xaudio2_9.dll","xaudio2_9redist.dll"};
    for(int i=0;i<3;++i){
        // Load the supported system/game DLL, never create a new engine. Holding
        // the module prevents the export/trampoline becoming invalid later.
        HMODULE module=LoadLibraryA(names[i]);
        if(!module){char line[120];sprintf_s(line,"modern audio module unavailable %s error=%lu",names[i],GetLastError());Log(line);continue;}
        const FARPROC target=GetProcAddress(module,"XAudio2Create");
        if(!target||!DetourModern(i,reinterpret_cast<void*>(target))){Log("modern audio export capture unavailable; import capture remains enabled");}
    }
}
FARPROC WINAPI Resolve(HMODULE module,LPCSTR name){
    const FARPROC original=resolver(module,name);
    // GetProcAddress accepts ordinals encoded as low pointer values.
    if(reinterpret_cast<std::uintptr_t>(name)>0xffffu&&std::strcmp(name,"XAudio2Create")==0)return WrapModern(module,original);
    return original;
}
bool PatchSlot(void** slot,void* original,void* replacement){
    DWORD protection=0;if(!VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&protection))return false;
    void* before=InterlockedCompareExchangePointer(slot,replacement,original);DWORD ignored=0;
    if(!VirtualProtect(slot,sizeof(void*),protection,&ignored))Log("audio import original page protection could not be restored");
    return before==original;
}
bool Install(){
    auto base=reinterpret_cast<unsigned char*>(GetModuleHandleA(nullptr));auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);auto nt=reinterpret_cast<IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);const DWORD size=nt->OptionalHeader.SizeOfImage;
    const auto fits=[&](DWORD rva,std::size_t length){return rva<size&&length<=size-rva;};
    const auto& dir=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];if(!fits(dir.VirtualAddress,dir.Size)||dir.Size<sizeof(IMAGE_IMPORT_DESCRIPTOR)){Log("audio factory capture unavailable: import directory missing");return false;}
    unsigned installed=0;
    for(DWORD off=0;off+sizeof(IMAGE_IMPORT_DESCRIPTOR)<=dir.Size;off+=sizeof(IMAGE_IMPORT_DESCRIPTOR)){
        IMAGE_IMPORT_DESCRIPTOR desc{};if(!Read(base+dir.VirtualAddress+off,&desc,sizeof(desc))||!desc.Name)break;
        if(!desc.OriginalFirstThunk||!desc.FirstThunk)continue;
        for(DWORD j=0;fits(desc.OriginalFirstThunk+j,sizeof(IMAGE_THUNK_DATA32))&&fits(desc.FirstThunk+j,sizeof(void*));j+=sizeof(IMAGE_THUNK_DATA32)){
            IMAGE_THUNK_DATA32 name{};if(!Read(base+desc.OriginalFirstThunk+j,&name,sizeof(name))||!name.u1.AddressOfData)break;if(IMAGE_SNAP_BY_ORDINAL32(name.u1.Ordinal))continue;
            if(!fits(name.u1.AddressOfData,2+sizeof("CoCreateInstance")))continue;
            char text[sizeof("CoCreateInstance")]{};if(!Read(base+name.u1.AddressOfData+2,text,sizeof(text)))continue;text[sizeof(text)-1]=0;
            void** slot=reinterpret_cast<void**>(base+desc.FirstThunk+j);void* original=nullptr;
            if(!Read(slot,&original,sizeof(original))||!Executable(original))continue;
            void* replacement=nullptr;
            if(std::strcmp(text,"CoCreateInstance")==0&&!factory){factory=reinterpret_cast<Factory>(original);replacement=reinterpret_cast<void*>(&Create);}
            else if(std::strcmp(text,"GetProcAddress")==0&&!resolver){resolver=reinterpret_cast<Resolver>(original);replacement=reinterpret_cast<void*>(&Resolve);}
            else if(std::strcmp(text,"XAudio2Create")==0){
                HMODULE module=nullptr;
                if(GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCSTR>(original),&module))replacement=reinterpret_cast<void*>(WrapModern(module,reinterpret_cast<FARPROC>(original)));
                if(replacement==original)replacement=nullptr;
            }
            if(replacement&&PatchSlot(slot,original,replacement)){++installed;char line[120];sprintf_s(line,"audio import capture installed: %s",text);Log(line);}
        }
    }
    Log(installed?"audio factory capture ready: legacy COM and modern XAudio2Create paths":"audio factory capture unavailable: no supported imports");return installed!=0;
}
}
