#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <string>
static std::string lastLog;
static void CaptureLog(const char* text){lastLog=text;}
#include "../vendor/roadnav/src/minimap_layout.hpp"
int main(){
    auto* image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x10000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));assert(image);
    const auto base=reinterpret_cast<std::uintptr_t>(image);
    auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(image);dos->e_magic=IMAGE_DOS_SIGNATURE;dos->e_lfanew=0x80;
    auto* nt=reinterpret_cast<IMAGE_NT_HEADERS32*>(image+0x80);nt->Signature=IMAGE_NT_SIGNATURE;nt->FileHeader.NumberOfSections=1;nt->FileHeader.SizeOfOptionalHeader=sizeof(IMAGE_OPTIONAL_HEADER32);nt->OptionalHeader.SizeOfImage=0x10000;
    auto* section=IMAGE_FIRST_SECTION(nt);section->VirtualAddress=0x1000;section->Misc.VirtualSize=0x1000;section->Characteristics=IMAGE_SCN_MEM_EXECUTE|IMAGE_SCN_MEM_READ;
    std::memset(image+0x1000,0xcc,0x1000);
    const unsigned char getter[]={0x80,0x79,0x11,0,0x0f,0x95,0xc0,0xc3};std::memcpy(image+0x1000,getter,sizeof(getter));
    const unsigned char infoCode[]={0xbe,0,0,0,0,0xeb,6,0x8d,0x9b,0,0,0,0,0x8b,0x0e};std::memcpy(image+0x1040,infoCode,sizeof(infoCode));
    const auto infoAddress=static_cast<std::uint32_t>(base+0x2000);std::memcpy(image+0x1041,&infoAddress,4);
    const unsigned char componentCode[]={0x8b,0x3c,0xb5,0,0,0,0,0x85,0xff,0x74,0x20};std::memcpy(image+0x1080,componentCode,sizeof(componentCode));
    const auto tableAddress=static_cast<std::uint32_t>(base+0x5200);std::memcpy(image+0x1083,&tableAddress,4);
    const unsigned char menuCode[]={0x8a,0x1d,0,0,0,0,0xc6,5,0,0,0,0,1,0x7f};std::memcpy(image+0x10c0,menuCode,sizeof(menuCode));
    const auto menuAddress=static_cast<std::uint32_t>(base+0x5400);std::memcpy(image+0x10c2,&menuAddress,4);
    auto* info=reinterpret_cast<minimaplayout::Info*>(image+0x2000);auto* table=reinterpret_cast<std::uint32_t*>(image+0x5200);
    for(unsigned i=0;i<67;++i){
        info[i]={i,{.1f,.8f},{.2f,.2f},0,255};table[i]=static_cast<std::uint32_t>(base+0x5800+i*0x180);
        image[0x5800+i*0x180+0x11]=1;
        image[0x5800+i*0x180+0x59]=255;image[0x5800+i*0x180+0x48]=230;
        image[0x5800+i*0x180+0x47]=100;image[0x5800+i*0x180+0x49]=101;
        *reinterpret_cast<minimaplayout::Point*>(image+0x5800+i*0x180+0x18)={.1f,.8f};
    }
    info[8].slot=31;
    using Display=bool(__thiscall*)(void*);const auto display=reinterpret_cast<Display>(image+0x1000);
    MinimapLayout mask;mask.enable(CaptureLog,base);assert(mask.active());
    // First playable query: mapped radar only is hidden, no first pause needed.
    for(unsigned i=0;i<67;++i){
        assert(display(reinterpret_cast<void*>(table[i]))==(i!=31));
        const auto point=*reinterpret_cast<minimaplayout::Point*>(static_cast<std::uintptr_t>(table[i])+0x18);
        assert(point.x==.1f&&point.y==.8f&&info[i].pos.x==.1f&&info[i].pos.y==.8f);
        assert(info[i].alpha==255&&image[0x5800+i*0x180+0x59]==255);
    }
    assert(lastLog.find("phase=first-radar-query")!=std::string::npos&&lastLog.find("geometry-writes=0")!=std::string::npos);
    // Native hidden preferences and unrelated notifications remain unchanged.
    auto* radar=reinterpret_cast<unsigned char*>(table[31]);
    *reinterpret_cast<unsigned char*>(static_cast<std::uintptr_t>(table[43])+0x11)=0;
    assert(!display(reinterpret_cast<void*>(table[43])));
    for(unsigned cycle=0;cycle<3;++cycle){
        image[0x5400]=1;assert(display(radar)); // frontend map stays normal
        mask.preference(false,cycle,CaptureLog);assert(!mask.active()&&display(radar));
        assert(lastLog.find("mask=1->0")!=std::string::npos);
        image[0x5400]=0;mask.enable(nullptr,base);assert(!mask.active()&&display(radar));
        mask.preference(true,cycle,nullptr);mask.enable(nullptr,base);assert(mask.active()&&!display(radar));
    }
    // A replacement radar is recognized before another script tick; no native
    // coordinate or alpha was ever displaced/copied into a broken layout.
    auto* replacement=image+0xb000;std::memcpy(replacement,radar,0x180);
    table[31]=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(replacement));
    assert(!display(replacement)&&display(radar));mask.release();assert(display(replacement));
    table[31]=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(radar));
    radar[0x11]=0;mask.enable(nullptr,base);mask.release();assert(!display(radar));radar[0x11]=1;
    // Alternate CE resolution: the generic table signature is absent, but
    // the native's decoded indexed HUD load and display call identify both.
    std::memset(image+0x1080,0xcc,sizeof(componentCode));
    const unsigned char wanted[]={0xa1,0,0,0,0,0x8b,0x0c,0x85,0,0,0,0,0xe8,0,0,0,0,0xc3};
    std::memcpy(image+0x1100,wanted,sizeof(wanted));const auto selector=static_cast<std::uint32_t>(base+0x5404);std::memcpy(image+0x1101,&selector,4);std::memcpy(image+0x1108,&tableAddress,4);
    const std::int32_t relative=-0x111;std::memcpy(image+0x110d,&relative,4);
    MinimapLayout fallback;fallback.enable(nullptr,base,base+0x1100);assert(fallback.active());
    assert(!display(reinterpret_cast<void*>(table[31]))&&info[8].pos.x==.1f);
    fallback.release();assert(info[8].pos.x==.1f);
    VirtualFree(image,0,MEM_RELEASE);MH_Uninitialize();
    std::puts("Radar visibility: first query, unchanged native geometry, isolated component, frontend and repeat Off/On passed");
}
