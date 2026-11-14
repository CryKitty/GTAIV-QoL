#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define SCRIPTHOOK_EXPORTS
#include <windows.h>
#include <cassert>
#include <cstdio>
#include <string>
#pragma warning(push)
#pragma warning(disable:4251 4275)
#include "../vendor/roadnav/src/driving_camera_render.hpp"
#pragma warning(pop)
__declspec(naked) void* __fastcall gameCopy(qolcamera::Matrix*,void*,const qolcamera::Matrix*){
    __asm {
        mov eax,[esp+4]
        movups xmm0,[eax]
        movups xmm1,[eax+16]
        movups xmm2,[eax+32]
        movups xmm3,[eax+48]
        movups [ecx],xmm0
        movups [ecx+16],xmm1
        movups [ecx+32],xmm2
        movups [ecx+48],xmm3
        mov eax,ecx
        ret 4
    }
}
std::string lastEvidence;
void record(const char* text){lastEvidence=text;}
void setViewport(unsigned char* bytes,const qolcamera::Matrix& world){
    float view[4][4]{},projection[4][4]{};
    view[0][0]=view[2][1]=view[1][2]=view[3][3]=1;
    view[3][0]=-world.v[12];view[3][1]=-world.v[14];view[3][2]=-world.v[13];
    projection[0][0]=projection[1][1]=projection[2][2]=projection[2][3]=1;projection[3][2]=-.1f;
    memcpy(bytes+0x180,view,sizeof(view));memcpy(bytes+0x1c0,projection,sizeof(projection));
}
int main(){
    auto* code=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));assert(code);
    // Two real x86 thiscall/fastcall callers of one copy target. Retain its
    // return value, stack cleanup and all matrix fields except the owned Z.
    const std::uintptr_t target=reinterpret_cast<std::uintptr_t>(&gameCopy);
    std::uintptr_t sites[2]{};
    for(unsigned i=0;i<2;++i){auto* p=code+i*32;const unsigned char prefix[]={0xff,0x74,0x24,4,0xe8};memcpy(p,prefix,5);
        const auto delta=static_cast<std::int32_t>(target-reinterpret_cast<std::uintptr_t>(p+9));memcpy(p+5,&delta,4);p[9]=0xc2;p[10]=4;p[11]=0;sites[i]=reinterpret_cast<std::uintptr_t>(p+4);
    }
    assert(drivingrender::attach(sites));
    auto front=reinterpret_cast<drivingrender::Copy>(code),rear=reinterpret_cast<drivingrender::Copy>(code+32);
    qolcamera::Matrix source{},dest{};for(unsigned i=0;i<4;++i)source.v[i*5]=1;source.v[12]=1;source.v[13]=2;source.v[14]=10;
    unsigned char viewport[0x2d0]{};auto currentViewport=reinterpret_cast<std::uintptr_t>(viewport);
    drivingrender::Verifier verifier(reinterpret_cast<std::uintptr_t>(&currentViewport),0);drivingrender::logger=record;
    for(unsigned cm:{0u,60u,120u})for(auto caller:{front,rear}){
        qolcamera::RenderRequest request;request.at=GetTickCount();request.cm=cm;request.vehicle=77;request.child=3842;request.driving=true;request.allowed=cm!=0;request.player[0]=1;request.player[1]=2;request.player[2]=9;
        drivingrender::publish(request);
        for(unsigned frame=0;frame<100;++frame){request.at=GetTickCount();drivingrender::publish(request);assert(caller(&dest,nullptr,&source)==&dest);assert(std::fabs(dest.v[14]-(10+cm*.01f))<.0001f);}
        setViewport(viewport,dest);verifier.OnRender(nullptr);
        assert(lastEvidence.find("verified=1")!=std::string::npos);
        if(cm){setViewport(viewport,source);verifier.OnRender(nullptr);assert(lastEvidence.find("verified=0")!=std::string::npos);}
        std::uintptr_t missing=0;drivingrender::Verifier unavailable(reinterpret_cast<std::uintptr_t>(&missing),0);unavailable.OnRender(nullptr);
        assert(lastEvidence.find("verified=unavailable")!=std::string::npos);
        assert(source.v[14]==10);for(unsigned i=0;i<16;++i)if(i!=14)assert(dest.v[i]==source.v[i]);
        auto measured=drivingrender::sample;float eye[]={dest.v[12],dest.v[13],dest.v[14]};
        assert(qolcamera::renderedEvidence(measured,eye,GetTickCount(),true).verified);
        eye[2]=10;if(cm)assert(!qolcamera::renderedEvidence(measured,eye,GetTickCount(),true).verified); // writes alone never prove effect
    }
    {
        qolcamera::RenderRequest chained;chained.at=GetTickCount();chained.cm=120;chained.driving=chained.allowed=true;chained.vehicle=77;
        drivingrender::publish(chained);front(&dest,nullptr,&source);
        qolcamera::Matrix next{};rear(&next,nullptr,&dest);assert(std::fabs(next.v[14]-11.2f)<.0001f);
        chained.cm=0;chained.allowed=false;chained.at=GetTickCount();drivingrender::publish(chained);
        front(&dest,nullptr,&next);assert(std::fabs(dest.v[14]-10)<.0001f);
    }
    drivingrender::disable();front(&dest,nullptr,&source);assert(dest.v[14]==10);
    // A different caller of the shared function must be a pure pass-through.
    qolcamera::RenderRequest active;active.at=GetTickCount();active.cm=120;active.driving=active.allowed=true;drivingrender::publish(active);
    gameCopy(&dest,nullptr,&source);assert(dest.v[14]==10);
    MH_DisableHook(drivingrender::targets[0]);MH_RemoveHook(drivingrender::targets[0]);VirtualFree(code,0,MEM_RELEASE);
    std::puts("Driving camera: real x86 front/rear copy chaining, per-frame 0/.60/1.2m world Z, no accumulation and readback failure detection passed");
}
IService::~IService(){}
