#pragma once
#include "../../../src/driving_camera_matrix.hpp"
#include "scene_camera.hpp"
#include "d3d_hook_service.hpp"
#include "../../minhook/include/MinHook.h"
#include <intrin.h>
#include <cstring>
#include <vector>

namespace drivingrender {
using Matrix=qolcamera::Matrix;
using Copy=void*(__fastcall*)(Matrix*,void*,const Matrix*);
inline SRWLOCK lock=SRWLOCK_INIT;
inline qolcamera::RenderRequest request{};
inline qolcamera::MatrixSample sample{};
inline void(*logger)(const char*)=nullptr;
inline Copy originals[2]{};
inline void* targets[2]{};inline std::uintptr_t returns[2]{};
inline bool attempted=false,installed=false;
inline unsigned copies=0,writes=0;
template<class T> bool read(std::uintptr_t address,T& result){SIZE_T n=0;return address>=0x10000&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),&result,sizeof(result),&n)&&n==sizeof(result);}
inline bool executable(std::uintptr_t address){MEMORY_BASIC_INFORMATION m{};if(!VirtualQuery(reinterpret_cast<void*>(address),&m,sizeof(m))||m.State!=MEM_COMMIT)return false;const auto p=m.Protect&255;return p==PAGE_EXECUTE||p==PAGE_EXECUTE_READ||p==PAGE_EXECUTE_READWRITE||p==PAGE_EXECUTE_WRITECOPY;}
inline void publish(qolcamera::RenderRequest next){AcquireSRWLockExclusive(&lock);request=next;ReleaseSRWLockExclusive(&lock);}
inline void disable(){publish({});}
inline void updated(Matrix* destination,const Matrix* source,std::uintptr_t caller){
    unsigned side=2;for(unsigned i=0;i<2;++i)if(caller==returns[i])side=i;if(side==2)return;
    Matrix before{};if(!read(reinterpret_cast<std::uintptr_t>(destination),before)||!qolcamera::usableMatrix(before))return;
    AcquireSRWLockShared(&lock);auto policy=request;const auto previous=sample;ReleaseSRWLockShared(&lock);
    const auto now=GetTickCount();if(!policy.driving||now-policy.at>200)return;
    const auto nativeCopy=before;if(now-previous.at<=200&&previous.request.vehicle==policy.vehicle)before=qolcamera::copyBaseline(before,previous,reinterpret_cast<std::uintptr_t>(source));
    auto after=before;qolcamera::prepareHeight(after,policy,now);const bool change=after.v[14]!=nativeCopy.v[14];bool wrote=false;
    if(change){SIZE_T n=0;wrote=WriteProcessMemory(GetCurrentProcess(),&destination->v[14],&after.v[14],sizeof(float),&n)&&n==sizeof(float);}
    if(!read(reinterpret_cast<std::uintptr_t>(destination),after))return;
    AcquireSRWLockExclusive(&lock);sample={before,after,policy,now,side,wrote,reinterpret_cast<std::uintptr_t>(destination)};++copies;if(wrote)++writes;ReleaseSRWLockExclusive(&lock);
}
__declspec(noinline) inline void* __fastcall front(Matrix* dest,void* edx,const Matrix* source){
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    auto result=originals[0](dest,edx,source);updated(dest,source,caller);return result;
}
__declspec(noinline) inline void* __fastcall rear(Matrix* dest,void* edx,const Matrix* source){
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    auto result=originals[1](dest,edx,source);updated(dest,source,caller);return result;
}
inline std::uintptr_t unique(const std::vector<int>& pattern){
    const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleA(nullptr));IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS32 nt{};
    if(!read(base,dos)||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<=0||dos.e_lfanew>4096||!read(base+dos.e_lfanew,nt)||nt.Signature!=IMAGE_NT_SIGNATURE||nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_I386||nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC||nt.FileHeader.NumberOfSections>32)return 0;
    const auto sections=base+dos.e_lfanew+offsetof(IMAGE_NT_HEADERS32,OptionalHeader)+nt.FileHeader.SizeOfOptionalHeader;
    unsigned found=0;std::uintptr_t site=0;
    for(unsigned i=0;i<nt.FileHeader.NumberOfSections;++i){IMAGE_SECTION_HEADER h{};if(!read(sections+i*sizeof(h),h)||!(h.Characteristics&IMAGE_SCN_MEM_EXECUTE)||h.VirtualAddress>=nt.OptionalHeader.SizeOfImage||h.Misc.VirtualSize>nt.OptionalHeader.SizeOfImage-h.VirtualAddress||h.Misc.VirtualSize>32*1024*1024)continue;
        std::vector<unsigned char> bytes(h.Misc.VirtualSize);SIZE_T n=0;if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(base+h.VirtualAddress),bytes.data(),bytes.size(),&n))continue;
        for(std::size_t k=0;k+pattern.size()<=n;++k){bool match=true;for(std::size_t p=0;p<pattern.size();++p)if(pattern[p]>=0&&bytes[k+p]!=pattern[p]){match=false;break;}
            if(match){site=base+h.VirtualAddress+k;++found;}
        }
    }
    return found==1?site:0;
}
// Separate injectable attachment boundary for the executable ABI test.
inline bool attach(const std::uintptr_t (&sites)[2]){
    for(unsigned i=0;i<2;++i){unsigned char opcode=0;std::int32_t delta=0;if(!read(sites[i],opcode)||opcode!=0xe8||!read(sites[i]+1,delta))return false;
        targets[i]=reinterpret_cast<void*>(static_cast<std::intptr_t>(sites[i]+5)+delta);returns[i]=sites[i]+5;if(!executable(reinterpret_cast<std::uintptr_t>(targets[i])))return false;
    }
    const auto init=MH_Initialize();if(init!=MH_OK&&init!=MH_ERROR_ALREADY_INITIALIZED)return false;
    const bool shared=targets[0]==targets[1];
    if(MH_CreateHook(targets[0],reinterpret_cast<void*>(&front),reinterpret_cast<void**>(&originals[0]))!=MH_OK)return false;
    if(!shared&&MH_CreateHook(targets[1],reinterpret_cast<void*>(&rear),reinterpret_cast<void**>(&originals[1]))!=MH_OK){MH_RemoveHook(targets[0]);return false;}
    const bool queued=MH_QueueEnableHook(targets[0])==MH_OK&&(shared||MH_QueueEnableHook(targets[1])==MH_OK);
    if(!queued||MH_ApplyQueued()!=MH_OK){MH_DisableHook(targets[0]);MH_RemoveHook(targets[0]);if(!shared){MH_DisableHook(targets[1]);MH_RemoveHook(targets[1]);}return false;}
    installed=true;return true;
}
inline bool install(void(*log)(const char*)){
    logger=log;if(attempted)return installed;attempted=true;
    // CE vehicle-follow front/rear matrix-copy call contexts. Preserve any
    // existing target (including FusionFix); touch only these two callers.
    const std::uintptr_t sites[]={
        unique({0xe8,-1,-1,-1,-1,0x80,0xa7,-1,-1,-1,-1,-1,0x80,0xa7,-1,-1,-1,-1,-1,0x80,0x7c,0x24}),
        unique({0xe8,-1,-1,-1,-1,0x5f,0xb0,1,0x5e,0x8b,0xe5,0x5d,0xc2,0x14,0})};
    if(!sites[0]||!sites[1]||sites[0]==sites[1]||!attach(sites)){if(log)log("Driving camera render: unavailable; verified CE follow-copy sites/targets missing, no height applied");return false;}
    if(log)log("Driving camera render: CE vehicle-follow copy interception ready; original target chained, world-Z height after each fresh copy");return true;
}
class Verifier final:public ID3DDeviceHook {
    RoadSceneCamera camera_;DWORD loggedAt_=0,interval_=1000;unsigned calibratedVehicle_=0;
public:
    explicit Verifier(std::uintptr_t testViewport=0,DWORD interval=1000):camera_(nullptr,testViewport),interval_(interval){}
    void OnCreateDevice(ID3DDevice*,PresentParameters*)override{}
    void OnResetDevice(ID3DDevice*,PresentParameters*)override{camera_.invalidate();calibratedVehicle_=0;}
    void OnLostDevice(ID3DDevice*)override{camera_.invalidate();calibratedVehicle_=0;}
    void OnDestroyDevice(ID3DDevice*)override{camera_.invalidate();calibratedVehicle_=0;}
    void OnRender(ID3DDevice*)override{
        const auto now=GetTickCount();if(now-loggedAt_<interval_)return;
        AcquireSRWLockShared(&lock);const auto s=sample;const auto n=copies,w=writes;ReleaseSRWLockShared(&lock);
        if(!s.request.driving||now-s.at>100)return;loggedAt_=now;
        roadnav::CameraFrame reference{};reference.position={s.after.v[12],s.after.v[13],s.after.v[14]};
        reference.rotation={std::asin(std::clamp(s.after.v[6],-1.0f,1.0f))*57.2957795f,0,std::atan2(-s.after.v[4],s.after.v[5])*57.2957795f};
        const auto projection=camera_.acquire(reference);const auto eye=projection.eye();const float xyz[]={eye.x,eye.y,eye.z};
        const auto proof=qolcamera::renderedEvidence(s,xyz,now,projection.valid);
        if(s.request.cm==0&&proof.verified)calibratedVehicle_=s.request.vehicle;
        const bool baseline=calibratedVehicle_==s.request.vehicle&&calibratedVehicle_!=0;
        if(logger){char text[440];std::snprintf(text,sizeof(text),"Driving camera rendered readback: cm=%u driver=%u allowed=%u child=%u copy-before-z=%.4f copy-after-z=%.4f copy-dz=%.4f rendered-z=%.4f rendered-dz=%.4f expected-dz=%.4f pair=%u low-baseline=%u verified=%u viewport=%u side=%u copies=%u writes=%u age=%lu",
            s.request.cm,s.request.driving?1u:0u,s.request.allowed?1u:0u,s.request.child,s.before.v[14],s.after.v[14],proof.copyDelta,eye.z,proof.renderDelta,proof.requested,proof.paired?1u:0u,baseline?1u:0u,proof.verified&&(s.request.cm==0||baseline)?1u:0u,projection.valid?1u:0u,s.side,n,w,now-s.at);logger(text);}
    }
};
}
