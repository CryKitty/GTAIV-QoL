#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <cassert>
#include "../vendor/minhook/include/MinHook.h"
void Log(const char*){}
bool Read(const void* p,void* out,SIZE_T n){SIZE_T got=0;return ReadProcessMemory(GetCurrentProcess(),p,out,n,&got)&&got==n;}
bool Executable(const void* p){MEMORY_BASIC_INFORMATION m{};return p&&VirtualQuery(p,&m,sizeof(m))&&(m.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE));}
#include "../src/cutscene_timeline.hpp"
int main(){
    using namespace cinematictime;
    // A real x86 thiscall getter: movzx eax,byte ptr [ecx]; inc eax; ret.
    // This exercises ECX forwarding through the fastcall detour and trampoline.
    unsigned char code[]={0x0F,0xB6,0x81,0,0,0,0,0x40,0xC3};
    void* memory=VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);assert(memory);
    std::memcpy(memory,code,sizeof(code));FlushInstructionCache(GetCurrentProcess(),memory,sizeof(code));
    unsigned char audio=40,unrelated=80;entity=&audio;
    assert(MH_Initialize()==MH_OK);void* trampoline=nullptr;
    assert(MH_CreateHook(memory,reinterpret_cast<void*>(&Get),&trampoline)==MH_OK);
    original=reinterpret_cast<Getter>(trampoline);installed=true;
    assert(MH_EnableHook(memory)==MH_OK);const auto call=reinterpret_cast<Getter>(memory);
    assert(call(entity)==41&&Begin());audio=140;
    assert(call(entity)==41&&call(&unrelated)==81);
    End();assert(call(entity)==41);audio=150;assert(call(entity)==51);
    assert(Begin());audio=200;assert(call(entity)==51);End();audio=210;assert(call(entity)==61);
    Reset();assert(call(entity)==211);audio=5;assert(call(entity)==6);
    assert(MH_DisableHook(memory)==MH_OK&&MH_RemoveHook(memory)==MH_OK);
    VirtualFree(memory,0,MEM_RELEASE);return 0;
}
