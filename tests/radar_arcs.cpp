#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <string>
#include "../vendor/roadnav/src/radar_arcs.hpp"
static std::string trace;
static void Log(const char* line){trace+=line;trace+='\n';}
int main(){
    auto* image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x5000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));assert(image);
    const auto base=reinterpret_cast<std::uintptr_t>(image);
    const auto put=[&](unsigned at,std::uint32_t n){std::memcpy(image+at,&n,4);};
    const auto absolute=[&](unsigned at,unsigned target){put(at,static_cast<std::uint32_t>(base+target));};
    const auto call=[&](unsigned at,unsigned target){image[at]=0xe8;put(at+1,target-at-5);};
    std::memset(image,0xcc,0x3000);std::memset(image+0x3000,0,0x2000);
    // Synthetic native parent: CPU/lifetime work and eight 5-argument cdecl
    // visual calls. It deliberately reads both fields that identify the CE
    // health/armour callback; no copied private log/function body is shipped.
    unsigned at=0x100;
    const unsigned char parent[]={0x53,0xbb,0,0,0,0,0x8b,0x83,0x84,0x0a,0,0,0x8b,0x83,0x84,0x0b,0,0,0xff,5,0,0,0,0};
    std::memcpy(image+at,parent,sizeof(parent));absolute(at+2,0x3000);absolute(at+20,0x3c00);at+=sizeof(parent);
    std::vector<unsigned> sites;
    for(unsigned n=0;n<8;++n){
        image[at++]=0x6a;image[at++]=1; // extra
        image[at++]=0x6a;image[at++]=0; // overlay
        image[at++]=0x68;put(at,0xff123456);at+=4;
        image[at++]=0x68;put(at,0x3f000000);at+=4; // .5f
        image[at++]=0x6a;image[at++]=static_cast<unsigned char>(n%2);
        sites.push_back(at);call(at,0x800);at+=5;
        image[at++]=0x83;image[at++]=0xc4;image[at++]=20;
    }
    image[at++]=0x5b;image[at]=0xc3;
    // Exact ABI shape, synthetic implementation: float is second stack arg,
    // includes the native armour-field use, records all five args and pixels.
    const unsigned char leaf[]={0x81,0xec,0xd4,0,0,0,0xa1,0,0,0,0,0x33,0xc4,0x89,0x84,0x24,0xd0,0,0,0,0xf3,0x0f,0x10,0x84,0x24,0xdc,0,0,0,0x53,0xbb,0,0,0,0,0x8b,0x83,0x84,0x0b,0,0,0x5b};
    std::memcpy(image+0x800,leaf,sizeof(leaf));absolute(0x807,0x3c04);absolute(0x81f,0x3000);
    unsigned end=0x800+sizeof(leaf);
    // Read each caller argument through the original frame and save it.
    for(unsigned n=0;n<5;++n){image[end++]=0x8b;image[end++]=0x84;image[end++]=0x24;put(end,0xd8+n*4);end+=4;image[end++]=0xa3;absolute(end,0x3c10+n*4);end+=4;}
    image[end++]=0xff;image[end++]=5;absolute(end,0x3c30);end+=4;
    image[end++]=0x81;image[end++]=0xc4;put(end,0xd4);end+=4;image[end]=0xc3;
    const auto sections=[&](){return std::vector<radarfill::Section>{{base,std::vector<unsigned char>(image,image+0x3000),true}};};
    auto found=radararcs::resolve(sections(),base+0x100);assert(found.target==base+0x800&&found.returns.size()==8);
    // A second matching leaf, half the calls, no longer meets the full relation.
    call(sites[0],0x900);call(sites[1],0x900);call(sites[2],0x900);
    assert(!radararcs::resolve(sections(),base+0x100).target);
    for(unsigned n=0;n<3;++n)call(sites[n],0x800);
    image[0x819]=0xdd;assert(!radararcs::resolve(sections(),base+0x100).target);image[0x819]=0xdc; // wrong float argument
    image[end]=0xc2;image[end+1]=20;image[end+2]=0;assert(!radararcs::resolve(sections(),base+0x100).target);image[end]=0xc3;
    using Parent=void(__cdecl*)();using Arc=void(__cdecl*)(unsigned,float,unsigned,unsigned,unsigned);
    const auto parentFn=reinterpret_cast<Parent>(image+0x100);const auto arc=reinterpret_cast<Arc>(image+0x800);
    auto& cpu=*reinterpret_cast<unsigned*>(image+0x3c00);auto& pixels=*reinterpret_cast<unsigned*>(image+0x3c30);
    const auto args=reinterpret_cast<unsigned*>(image+0x3c10);
    RadarArcs mask;mask.enable(sections(),base+0x100,base+0x3c40,base,Log);assert(mask.active());
    parentFn();assert(cpu==1&&pixels==0&&mask.hidden(1)==4&&mask.hidden(0)==4);
    // Another phase calling this SAME fixed-anchor leaf must also be hidden.
    arc(1,.25f,0xffabcdef,1,0);assert(pixels==0&&mask.hidden(1)==5);
    image[0x3c40]=1;parentFn();assert(cpu==2&&pixels==8);
    assert(args[0]==1&&args[1]==0x3f000000&&args[2]==0xff123456&&args[3]==0&&args[4]==1);
    mask.release();image[0x3c40]=0;parentFn();assert(cpu==3&&pixels==16);
    arc(0,.75f,0xff876543,1,0);assert(pixels==17&&args[1]==0x3f400000&&args[2]==0xff876543&&args[3]==1&&args[4]==0);
    for(unsigned cycle=0;cycle<3;++cycle){mask.enable(sections(),base+0x100,base+0x3c40,base,Log);assert(mask.active());const auto before=pixels;parentFn();assert(pixels==before);mask.release();parentFn();assert(pixels==before+8);}
    assert(trace.find("element=health-front-fill source=five-argument-native-arc suppressed=1")!=std::string::npos);
    assert(trace.find("element=armour-back-fill source=five-argument-native-arc suppressed=1")!=std::string::npos);
    assert(trace.find("caller-known=0")!=std::string::npos&&trace.find("native-arc-restored=1")!=std::string::npos);
    std::puts("Native radar arcs: independent typed leaf suppressed; parent lifecycle preserved; other-phase, frontend, Off/On and exact five-argument forwarding passed");
    VirtualFree(image,0,MEM_RELEASE);MH_Uninitialize();
}
