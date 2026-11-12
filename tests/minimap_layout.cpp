#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cassert>
#include <cstring>
#include <cstdio>
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
    MinimapLayout mask;mask.enable(nullptr,base);assert(mask.active());
    for(unsigned i=0;i<67;++i){
        assert(display(reinterpret_cast<void*>(table[i]))); // Every visibility answer is preserved.
        const auto p=*reinterpret_cast<minimaplayout::Point*>(static_cast<std::uintptr_t>(table[i])+0x18);
        assert(minimaplayout::displaced(p)==(i==31)); // Only mapped radar geometry moves.
        if(i!=8)assert(info[i].pos.x==.1f&&info[i].pos.y==.8f&&info[i].alpha==255);
        const auto* object=reinterpret_cast<const unsigned char*>(table[i]);
        assert(object[0x59]==255&&object[0x48]==230);
        assert(object[0x47]==100&&object[0x49]==101); // Adjacent fields and RGB stay intact.
    }
    assert(mask.count()>0&&minimaplayout::displaced(info[8].pos));
    *reinterpret_cast<unsigned char*>(static_cast<std::uintptr_t>(table[43])+0x11)=0;
    assert(!display(reinterpret_cast<void*>(table[43]))); // Existing hidden notifications remain hidden, not forced on.
    // Vehicle/fade updates retain their exact native opacity; this mask never
    // changes any component alpha, including the mapped radar component.
    auto* radar=reinterpret_cast<unsigned char*>(table[31]);radar[0x59]=128;radar[0x48]=192;info[8].alpha=200;
    assert(display(radar)&&radar[0x59]==128&&radar[0x48]==192&&info[8].alpha==200);
    image[0x5400]=1;assert(display(reinterpret_cast<void*>(table[31])));
    assert(radar[0x59]==128&&radar[0x48]==192&&info[8].alpha==200);
    assert(info[8].pos.x==.1f&&!minimaplayout::displaced(*reinterpret_cast<minimaplayout::Point*>(static_cast<std::uintptr_t>(table[31])+0x18)));
    image[0x5400]=0;assert(display(reinterpret_cast<void*>(table[31]))&&minimaplayout::displaced(info[8].pos));
    mask.release();assert(radar[0x59]==128&&radar[0x48]==192&&info[8].alpha==200);assert(!mask.active()&&info[8].pos.x==.1f&&display(reinterpret_cast<void*>(table[31])));
    // Alternate CE resolution: the generic table signature is absent, but
    // the native's decoded indexed HUD load and display call identify both.
    std::memset(image+0x1080,0xcc,sizeof(componentCode));
    const unsigned char wanted[]={0xa1,0,0,0,0,0x8b,0x0c,0x85,0,0,0,0,0xe8,0,0,0,0,0xc3};
    std::memcpy(image+0x1100,wanted,sizeof(wanted));const auto selector=static_cast<std::uint32_t>(base+0x5404);std::memcpy(image+0x1101,&selector,4);std::memcpy(image+0x1108,&tableAddress,4);
    const std::int32_t relative=-0x111;std::memcpy(image+0x110d,&relative,4);
    MinimapLayout fallback;fallback.enable(nullptr,base,base+0x1100);assert(fallback.active());
    assert(display(reinterpret_cast<void*>(table[31]))&&minimaplayout::displaced(info[8].pos));
    fallback.release();assert(info[8].pos.x==.1f);
    VirtualFree(image,0,MEM_RELEASE);MH_Uninitialize();
    std::puts("Minimap layout: real x86 visibility answers preserved for all HUD components; radar-only geometry, every native HUD alpha preserved through vehicle/fade changes, live pause and restoration passed");
}
