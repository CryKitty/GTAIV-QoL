#pragma once
#include <cstddef>
#include <cwchar>
#include "driving_camera_height.hpp"
#include "../vendor/minhook/include/MinHook.h"

namespace menuvalue {
// CE layouts and the numeric-buffer hook contract follow FusionFix settings.ixx
// (SettingsTables and InitializeMenuAPI). The pinned older host selects display
// 108 but has no value-text callback; GTA's stock number is not the FOV.
struct Option {
    unsigned char action;char label[16];unsigned char padding;
    short preference;unsigned char scaler,display;
};
struct Screen {char header[16];Option* data;unsigned short count,capacity;};
static_assert(sizeof(Option)==22&&offsetof(Option,preference)==18,"CE option layout");
static_assert(sizeof(Screen)==24,"CE screen layout");
int* currentScreen=nullptr;
Screen* screens=nullptr;
void* continuation=nullptr;

void __cdecl Update(unsigned rowOffset,wchar_t* buffer){
    int screen=-1;Screen page{};Option option{};
    if(!currentScreen||!screens||!Read(currentScreen,&screen,sizeof(screen))||screen<0||screen>=73||
       !Read(screens+screen,&page,sizeof(page))||!page.data||page.count>page.capacity||page.count>50||
       rowOffset%sizeof(Option)||rowOffset/sizeof(Option)>=page.count||
       !Read(page.data+rowOffset/sizeof(Option),&option,sizeof(option))||option.display!=108||
       (std::memcmp(option.label,"QOL_TAXI\0",9)!=0&&std::memcmp(option.label,"QOL_CAMHEIGHT\0",14)!=0))return;
    if(std::memcmp(option.label,"QOL_CAMHEIGHT\0",14)==0){
        const int height=qolclient::Get(QOL_DRIVING_CAMERA_HEIGHT,0);if(height<0||height>200)return;
        const auto text=qolcamera::presetName(qolcamera::heightPreset(height));SIZE_T written=0;
        WriteProcessMemory(GetCurrentProcess(),buffer,text,(std::wcslen(text)+1)*sizeof(wchar_t),&written);return;
    }
    const int value=qolclient::Get(QOL_TAXI_FOV,-1);
    if(value<40||value>120)return;
    // Integer-only formatting keeps the printed value exact, including 120.
    wchar_t text[4]{};unsigned length=0;
    if(value>=100)text[length++]=L'1';
    text[length++]=static_cast<wchar_t>(L'0'+(value/10)%10);
    text[length++]=static_cast<wchar_t>(L'0'+value%10);
    SIZE_T written=0;
    WriteProcessMemory(GetCurrentProcess(),buffer,text,(length+1)*sizeof(wchar_t),&written);
}

// Enter at the LEA just after the game's add esp,0Ch. Preserve every register,
// flags and FP/SIMD state; MinHook's trampoline executes the displaced LEA.
__declspec(naked) void DrawValue(){
    __asm {
        pushfd
        pushad
        mov ebp,esp
        sub esp,528
        and esp,0fffffff0h
        fxsave [esp]
        cld
        lea eax,[ebp+0114h] // original esp + F0h (36 bytes saved)
        push eax
        push ebx
        call Update
        add esp,8
        fxrstor [esp]
        mov esp,ebp
        popad
        popfd
        jmp dword ptr [continuation]
    }
}

unsigned char* Unique(const unsigned char* pattern,const char* mask,std::size_t length){
    auto base=reinterpret_cast<unsigned char*>(GetModuleHandleA(nullptr));
    auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto nt=reinterpret_cast<IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
    auto sections=IMAGE_FIRST_SECTION(nt);unsigned count=0;unsigned char* result=nullptr;
    for(unsigned s=0;s<nt->FileHeader.NumberOfSections;++s){const auto& sec=sections[s];
        if(!(sec.Characteristics&IMAGE_SCN_MEM_EXECUTE)||sec.VirtualAddress>=nt->OptionalHeader.SizeOfImage||
           sec.Misc.VirtualSize>nt->OptionalHeader.SizeOfImage-sec.VirtualAddress)continue;
        std::vector<unsigned char> bytes(sec.Misc.VirtualSize);
        if(!Read(base+sec.VirtualAddress,bytes.data(),bytes.size()))continue;
        for(std::size_t i=0;i+length<=bytes.size();++i){bool match=true;
            for(std::size_t j=0;j<length;++j)if(mask[j]!='?'&&bytes[i+j]!=pattern[j]){match=false;break;}
            if(match){result=base+sec.VirtualAddress+i;++count;}
        }
    }
    return count==1?result:nullptr;
}
bool Attach(void* site){
    const auto init=MH_Initialize();if(init!=MH_OK&&init!=MH_ERROR_ALREADY_INITIALIZED)return false;
    if(MH_CreateHook(site,reinterpret_cast<void*>(&DrawValue),&continuation)!=MH_OK)return false;
    if(MH_EnableHook(site)!=MH_OK){MH_RemoveHook(site);continuation=nullptr;return false;}
    return true;
}
void Install(){
    // Called only after the companion's strict 1.2.0.59 executable check.
    const unsigned char number[]={0x83,0xC4,0x0C,0x8D,0x84,0x24,0xF0,0,0,0,0x6A,0xFF,0x6A,0xFF,0x50,0xE9};
    const unsigned char scroll[]={0x0F,0xB7,0x14,0xCD,0,0,0,0,0x8B,0xC2};
    auto site=Unique(number,"xxxxxxxxxxxxxxxx",sizeof(number));
    auto table=Unique(scroll,"xxxx????xx",sizeof(scroll));
    DWORD screenAddress=0,optionsAddress=0;
    if(!site||!table||!Read(table-7,&screenAddress,4)||!Read(table+4,&optionsAddress,4)||optionsAddress<0x14){
        Log("taxi FOV value text unavailable: unique CE renderer/table signatures missing");return;
    }
    currentScreen=reinterpret_cast<int*>(screenAddress);
    screens=reinterpret_cast<Screen*>(optionsAddress-0x14);
    int screen=-1;Screen page{};
    if(!Read(currentScreen,&screen,4)||!Read(screens,&page,sizeof(page))||!Attach(site+3)){
        currentScreen=nullptr;screens=nullptr;Log("taxi FOV value text unavailable: table validation or hook failed");return;
    }
    Log("taxi FOV value text ready: live degrees on QOL_TAXI numeric slider only");
}
}
