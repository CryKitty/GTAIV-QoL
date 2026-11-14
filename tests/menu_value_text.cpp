#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <vector>
#include "../src/qol_settings_api.h"
int selected=70;
namespace qolclient {int Get(unsigned,int){return selected;}}
bool Read(const void* p,void* out,SIZE_T size){SIZE_T n=0;return ReadProcessMemory(GetCurrentProcess(),p,out,size,&n)&&n==size;}
void Log(const char*){}
#include "../src/menu_value_text.hpp"
#include "../src/version_footer.hpp"
wchar_t captured[8]{};DWORD flagsAfter=0;float xmmAfter=0,sentinel=123.5f;
void __cdecl Capture(const wchar_t* text,int a,int b){assert(a==-1&&b==-1);wcscpy_s(captured,text);}
__declspec(naked) void Probe(){
    __asm {
        push ebx
        sub esp,180h
        xor ebx,ebx
        mov dword ptr [esp+0f0h],00310031h
        mov dword ptr [esp+0f4h],0
        movss xmm0,dword ptr [sentinel]
        stc
        lea eax,[esp+0f0h]
        pushfd
        pop dword ptr [flagsAfter]
        movss dword ptr [xmmAfter],xmm0
        push -1
        push -1
        push eax
        call Capture
        add esp,0ch
        add esp,180h
        pop ebx
        ret
    }
}
const wchar_t* footerInput=nullptr;const wchar_t* footerCaptured=nullptr;
DWORD footerFlags=0;float footerXmm=0;int footerA=0,footerB=0;
__declspec(naked) void __cdecl CaptureFooter(const wchar_t*,int,int){
    __asm {
        mov eax,[esp+4]
        mov dword ptr [footerCaptured],eax
        mov eax,[esp+8]
        mov dword ptr [footerA],eax
        mov eax,[esp+12]
        mov dword ptr [footerB],eax
        pushfd
        pop dword ptr [footerFlags]
        movss dword ptr [footerXmm],xmm0
        ret
    }
}
__declspec(naked) void ProbeFooter(){
    __asm {
        push ebx
        mov eax,dword ptr [footerInput]
        movss xmm0,dword ptr [sentinel]
        stc
        push -1
        push -1
        push eax
        call CaptureFooter
        add esp,12
        pop ebx
        ret
    }
}
int main(){
    {
        const wchar_t* ordinary=L"Ordinary menu help";
        assert(versionfooter::Extend(ordinary)==ordinary);
        assert(versionfooter::Extend(reinterpret_cast<const wchar_t*>(1))==reinterpret_cast<const wchar_t*>(1));
        footerInput=L"Help~n~                        ~p~GTAIV.exe: v1.2.0.59 / Fusion Fix: v5.1.2 / ASI Loader: v9";
        auto code=reinterpret_cast<unsigned char*>(&ProbeFooter);void* site=nullptr;
        const unsigned char pushes[]={0x6a,0xff,0x6a,0xff,0x50};
        for(unsigned i=0;i<80;++i)if(std::memcmp(code+i,pushes,sizeof(pushes))==0){site=code+i;break;}
        assert(site&&versionfooter::Attach(site));ProbeFooter();
        assert(footerCaptured&&std::wcsstr(footerCaptured,L"~p~CryKitty QoL: " QOL_VERSION_WIDE L" " QOL_BUILD_WIDE));
        assert(footerA==-1&&footerB==-1&&(footerFlags&1)&&footerXmm==sentinel);
        footerInput=ordinary;ProbeFooter();assert(footerCaptured==ordinary);
        assert(MH_DisableHook(site)==MH_OK);assert(MH_RemoveHook(site)==MH_OK);versionfooter::continuation=nullptr;
    }
    int screen=0;menuvalue::Option option{};strcpy_s(option.label,"QOL_TAXI");option.display=108;
    menuvalue::Screen pages[73]{};pages[0].data=&option;pages[0].count=pages[0].capacity=1;
    menuvalue::currentScreen=&screen;menuvalue::screens=pages;
    wchar_t text[32]{};
    for(int value:{40,70,85,99,100,119,120}){selected=value;wcscpy_s(text,L"1");menuvalue::Update(0,text);assert(std::wcstol(text,nullptr,10)==value);}
    strcpy_s(option.label,"QOL_CAMHEIGHT");
    for(int value:{0,45,120}){selected=value;menuvalue::Update(0,text);assert(wcscmp(text,qolcamera::presetName(qolcamera::heightPreset(value)))==0);}
    strcpy_s(option.label,"QOL_TAXI");
    for(int value:{-1,0,39,121}){selected=value;wcscpy_s(text,L"stock");menuvalue::Update(0,text);assert(wcscmp(text,L"stock")==0);}
    strcpy_s(option.label,"QOL_CAMHEIGHT");
    for(int height:{0,5,125,200}){selected=height;menuvalue::Update(0,text);assert(wcscmp(text,qolcamera::presetName(qolcamera::heightPreset(height)))==0);}
    selected=201;wcscpy_s(text,L"stock");menuvalue::Update(0,text);assert(wcscmp(text,L"stock")==0);
    strcpy_s(option.label,"QOL_TAXI");
    selected=85;
    for(int invalid:{-1,73}){screen=invalid;menuvalue::Update(0,text);assert(wcscmp(text,L"stock")==0);}screen=0;
    menuvalue::Update(1,text);menuvalue::Update(22,text);assert(wcscmp(text,L"stock")==0);
    strcpy_s(option.label,"QOL_CUTVOICE");menuvalue::Update(0,text);assert(wcscmp(text,L"stock")==0);
    strcpy_s(option.label,"QOL_TAXI");option.display=101;menuvalue::Update(0,text);assert(wcscmp(text,L"stock")==0);option.display=108;
    pages[0].capacity=0;menuvalue::Update(0,text);assert(wcscmp(text,L"stock")==0);pages[0].capacity=1;
    // Exercise the actual naked shim through a real MinHook trampoline. The
    // caller's stack buffer, original LEA, carry flag and XMM0 must survive.
    auto code=reinterpret_cast<unsigned char*>(&Probe);unsigned char* site=nullptr;
    const unsigned char lea[]={0x8D,0x84,0x24,0xF0,0,0,0};
    for(unsigned i=0;i<100;++i)if(std::memcmp(code+i,lea,sizeof(lea))==0){site=code+i;break;}
    assert(site&&menuvalue::Attach(site));
    for(int value:{40,70,120}){selected=value;Probe();assert(std::wcstol(captured,nullptr,10)==value);assert(flagsAfter&1);assert(xmmAfter==sentinel);}
    strcpy_s(option.label,"QOL_OTHER");Probe();assert(wcscmp(captured,L"11")==0);
    assert(MH_DisableHook(site)==MH_OK);assert(MH_RemoveHook(site)==MH_OK);
    std::puts("Taxi numeric text, row filtering and x86 continuation passed");
}
