#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <thread>
#include "../vendor/roadnav/src/minimap_ring.hpp"
int main(){
    auto* image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x10000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));assert(image);
    const auto base=reinterpret_cast<std::uintptr_t>(image);
    const auto address=[&](unsigned offset){return static_cast<std::uint32_t>(base+offset);};
    const auto put=[&](unsigned offset,std::uint32_t value){std::memcpy(image+offset,&value,4);};
    const auto call=[&](unsigned at,unsigned target){image[at]=0xe8;put(at+1,target-at-5);};
    auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(image);dos->e_magic=IMAGE_DOS_SIGNATURE;dos->e_lfanew=0x80;
    auto* nt=reinterpret_cast<IMAGE_NT_HEADERS32*>(image+0x80);nt->Signature=IMAGE_NT_SIGNATURE;nt->FileHeader.NumberOfSections=2;nt->FileHeader.SizeOfOptionalHeader=sizeof(IMAGE_OPTIONAL_HEADER32);nt->OptionalHeader.SizeOfImage=0x10000;
    auto* section=IMAGE_FIRST_SECTION(nt);section->VirtualAddress=0x1000;section->Misc.VirtualSize=0x1000;section->Characteristics=IMAGE_SCN_MEM_EXECUTE|IMAGE_SCN_MEM_READ;
    section[1].VirtualAddress=0x3000;section[1].Misc.VirtualSize=0x1000;section[1].Characteristics=IMAGE_SCN_MEM_READ|IMAGE_SCN_MEM_WRITE;
    std::memset(image+0x1000,0xcc,0x1000);
    const unsigned char stateCode[]={0x56,0x8b,0xf1,0x8b,0x0d,0,0,0,0,0xff,0x36,0x58,0x5e,0xff,5,0,0,0,0,0xc3};
    std::memcpy(image+0x1000,stateCode,sizeof(stateCode));put(0x1005,address(0x3000));put(0x100f,address(0x3010));
    // An untextured meter draw and a border draw deliberately use independent
    // functions. Ordinary HUD/compass draws use these SAME shared functions.
    const unsigned char counter[]={0xff,5,0,0,0,0,0xc3};
    std::memcpy(image+0x1080,counter,sizeof(counter));put(0x1082,address(0x3014));
    std::memcpy(image+0x10c0,counter,sizeof(counter));put(0x10c2,address(0x3018));
    const auto makeFrame=[&](unsigned start,unsigned sprite){
        // Conditional early return before texture use exercises control flow.
        const unsigned char prologue[]={0x55,0x8b,0xec,0x33,0xc0,0x85,0xc0,0x74,2,0x5d,0xc3,0xb9,0,0,0,0};
        std::memcpy(image+start,prologue,sizeof(prologue));put(start+12,address(sprite));
        call(start+16,0x1000);call(start+21,0x1080);call(start+26,0x10c0);
        image[start+31]=0x5d;image[start+32]=0xc3;
    };
    makeFrame(0x1100,0x3200);makeFrame(0x1180,0x3204);
    // Actual deferred callback references, independently of texture references.
    image[0x1200]=0x68;put(0x1201,address(0x1100));image[0x1205]=0x68;put(0x1206,address(0x1180));
    std::memcpy(image+0x3100,"radarringfront",15);std::memcpy(image+0x3120,"radarringback",14);
    const unsigned char textureInit[]={0x68,0,0,0,0,0xb9,0,0,0,0,0xe8,0,0,0,0};
    std::memcpy(image+0x1280,textureInit,sizeof(textureInit));put(0x1281,address(0x3100));put(0x1286,address(0x3200));
    std::memcpy(image+0x12c0,textureInit,sizeof(textureInit));put(0x12c1,address(0x3120));put(0x12c6,address(0x3204));
    // Regression: the real radar queue has a separate fill callback, outside
    // both frame callbacks. A shared state restore is used by gameplay AND map.
    std::memcpy(image+0x1700,counter,sizeof(counter));put(0x1702,address(0x3020));
    std::memcpy(image+0x1ec0,counter,sizeof(counter));put(0x1ec2,address(0x3024));
    image[0x1e80]=0xc2;image[0x1e81]=4;image[0x1e82]=0;image[0x1f00]=0xc3;
    image[0x1600]=0x55;image[0x1601]=0x8b;image[0x1602]=0xec;unsigned queued=0x1603;
    const auto queue=[&](unsigned callback){image[queued]=0x68;put(queued+1,address(callback));image[queued+5]=0xb9;put(queued+6,address(0x3c00));call(queued+10,0x1e80);queued+=15;};
    queue(0x1ec0);queue(0x1180);queue(0x1700);queue(0x1ec0);queue(0x1100);image[queued++]=0x5d;image[queued]=0xc3;
    // MSVC x86 RTTI: exact Radar type, base hierarchy, complete-object locator,
    // vtable and independently located render-queue method (slot can vary).
    std::memcpy(image+0x3408,".?AVCRenderPhaseRadar@@",23);
    std::memcpy(image+0x3508,".?AVCRenderPhase@@",18);
    put(0x3600,address(0x3400));put(0x3640,address(0x3500));put(0x3700,address(0x3600));put(0x3704,address(0x3640));
    put(0x3808,2);put(0x380c,address(0x3700));put(0x390c,address(0x3400));put(0x3910,address(0x3800));put(0x3a00,address(0x3900));
    for(unsigned slot=0;slot<15;++slot)put(0x3a04+slot*4,address(slot==8?0x1600:0x1f00));
    const auto snapshot=[&](){return std::vector<radarfill::Section>{{base+0x1000,std::vector<unsigned char>(image+0x1000,image+0x2000),true},{base+0x3000,std::vector<unsigned char>(image+0x3000,image+0x4000),false}};};
    auto resolution=radarfill::resolve(snapshot(),base+0x1100,base+0x1180);assert(resolution.callback==base+0x1700&&resolution.slot==8);
    // CE need not expose the old release's RTTI/vtable shape. Independently
    // validated front/back requests still prove this specific queue's owner.
    image[0x340c]='X';resolution=radarfill::resolve(snapshot(),base+0x1100,base+0x1180);
    assert(resolution.callback==base+0x1700&&resolution.tables==0&&resolution.queues==1);
    // A real queue can end in a tail call to DrawBlips. Do not discard all of
    // the earlier queued draws because the jump leaves the method's body.
    const auto finish=queued;image[finish-1]=0x5d;image[finish]=0xe9;put(finish+1,0x1000-finish-5);
    assert(radarfill::resolve(snapshot(),base+0x1100,base+0x1180).callback==base+0x1700);
    image[finish]=0xc3;std::memset(image+finish+1,0xcc,4);
    // The queue may use callee cleanup; its visual callback may not.
    image[finish]=0xc2;image[finish+1]=4;image[finish+2]=0;
    assert(radarfill::resolve(snapshot(),base+0x1100,base+0x1180).callback==base+0x1700);
    image[finish]=0xc3;image[finish+1]=image[finish+2]=0xcc;
    // Visual callbacks can tail-call another no-args rendering routine too.
    std::memcpy(image+0x1500,counter,sizeof(counter));put(0x1502,address(0x3020));
    image[0x1700]=0xe9;put(0x1701,static_cast<std::uint32_t>(0x1500-0x1700-5));
    assert(radarfill::resolve(snapshot(),base+0x1100,base+0x1180).callback==base+0x1700);
    image[0x1506]=0xc2;image[0x1507]=4;image[0x1508]=0;
    assert(!radarfill::resolve(snapshot(),base+0x1100,base+0x1180).callback);
    std::memset(image+0x1500,0xcc,16);std::memcpy(image+0x1700,counter,sizeof(counter));put(0x1702,address(0x3020));
    image[0x340c]='C';
    // A second independently valid queue is ambiguous and must not be hooked.
    std::memcpy(image+0x1800,image+0x1600,queued+1-0x1600);
    for(unsigned at=0x1800;at<0x1800+queued+1-0x1600;++at)if(image[at]==0xe8){std::uint32_t relative=0;std::memcpy(&relative,image+at+1,4);put(at+1,relative-0x200);at+=4;}
    assert(!radarfill::resolve(snapshot(),base+0x1100,base+0x1180).callback);
    std::memset(image+0x1800,0xcc,queued+1-0x1600);
    // A matching-looking visual function used by another owner is not isolated.
    image[0x1400]=0x68;put(0x1401,address(0x1700));assert(!radarfill::resolve(snapshot(),base+0x1100,base+0x1180).callback);std::memset(image+0x1400,0xcc,5);
    using State=void(__thiscall*)(void*);using Draw=void(__cdecl*)();
    const auto state=reinterpret_cast<State>(image+0x1000);
    const auto meter=reinterpret_cast<Draw>(image+0x1080),border=reinterpret_cast<Draw>(image+0x10c0);
    const auto front=reinterpret_cast<Draw>(image+0x1100),back=reinterpret_cast<Draw>(image+0x1180);
    auto& meters=*reinterpret_cast<unsigned*>(image+0x3014);auto& borders=*reinterpret_cast<unsigned*>(image+0x3018);
    MinimapRing mask;mask.enable(base+0x3300,nullptr,base);assert(mask.active());
    front();back();assert(meters==0&&borders==0&&mask.hidden()==2);
    const auto nativeFill=reinterpret_cast<Draw>(image+0x1700),restoreState=reinterpret_cast<Draw>(image+0x1ec0);
    auto& fills=*reinterpret_cast<unsigned*>(image+0x3020);auto& restores=*reinterpret_cast<unsigned*>(image+0x3024);
    nativeFill();assert(fills==0&&mask.fillHidden()==1&&mask.fillCalls()==1);
    restoreState();assert(restores==1); // Preserve shared state and all CPU GPS work.
    state(image+0x3200);meter();border();assert(meters==1&&borders==1); // Last selection was ring; unrelated HUD still draws.
    state(image+0x3208);meter();assert(meters==2); // Gun/other sprite cannot change compass meter visibility.
    state(image+0x3204);meter();assert(meters==3);
    image[0x3300]=1;front();back();nativeFill();assert(fills==1);assert(meters==5&&borders==3); // Live pause menu, without game ticks.
    image[0x3300]=0;front();nativeFill();assert(fills==1&&mask.fillHidden()==2);assert(meters==5&&mask.hidden()==5);
    std::thread worker([&](){meter();front();});worker.join();assert(meters==6&&mask.hidden()==6);
    mask.release();front();back();nativeFill();assert(fills==2);assert(meters==8&&borders==5&&!mask.active());
    // A second queued function using the same sprite is ambiguous: don't hook.
    makeFrame(0x1380,0x3200);image[0x1400]=0x68;put(0x1401,address(0x1380));
    MinimapRing ambiguous;ambiguous.enable(base+0x3300,nullptr,base);assert(!ambiguous.active());front();assert(meters==9);
    // No queued reference: a nearby, decodable ring-use function is insufficient.
    std::memset(image+0x1400,0xcc,5);std::memset(image+0x1200,0xcc,10);std::memset(image+0x1600,0xcc,0x100);
    MinimapRing unqueued;unqueued.enable(base+0x3300,nullptr,base);assert(!unqueued.active());
    VirtualFree(image,0,MEM_RELEASE);MH_Uninitialize();
    std::puts("Radar visuals: no-RTTI and tail-call queues resolved; callee-cleanup visual and ambiguous/shared owners rejected; native border/fill hooks, HUD preservation, frontend, gun changes, disable and threads passed");
}
