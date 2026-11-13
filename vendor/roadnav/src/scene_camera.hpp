#pragma once
#include "scene_depth.hpp"
#include "road_projection.hpp"
// Read-only rage::pCurrentViewport signatures/layout from FusionFix comvars.ixx.
class RoadSceneCamera {
    std::uintptr_t viewport_=0;std::uint32_t gameplay_=0;SceneDiscoveryRetry retry_{};void(*log_)(const char*)=nullptr;
    template<class T>static bool read(std::uintptr_t address,T& out){SIZE_T bytes=0;return address>=0x10000&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),&out,sizeof(out),&bytes)&&bytes==sizeof(out);}
    static bool matches(const unsigned char* code,const std::vector<int>& pattern){for(std::size_t i=0;i<pattern.size();++i)if(pattern[i]>=0&&code[i]!=pattern[i])return false;return true;}
    void discover(){
        const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleA(nullptr));IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS nt{};
        if(!read(base,dos)||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<=0||dos.e_lfanew>0x1000||!read(base+dos.e_lfanew,nt)||nt.Signature!=IMAGE_NT_SIGNATURE||nt.FileHeader.NumberOfSections>32)return;
        const std::array<std::vector<int>,2> patterns{{
            {0x8b,0x35,-1,-1,-1,-1,0x75,0x14},
            {0x8b,0x3d,-1,-1,-1,-1,0x75,0x14,0x6a,0x00}
        }};
        const auto sections=base+dos.e_lfanew+offsetof(IMAGE_NT_HEADERS,OptionalHeader)+nt.FileHeader.SizeOfOptionalHeader;
        unsigned count=0;std::uintptr_t candidate=0;
        for(unsigned i=0;i<nt.FileHeader.NumberOfSections;++i){IMAGE_SECTION_HEADER h{};
            if(!read(sections+i*sizeof(h),h)||!(h.Characteristics&IMAGE_SCN_MEM_EXECUTE)||h.VirtualAddress>=nt.OptionalHeader.SizeOfImage||h.Misc.VirtualSize>nt.OptionalHeader.SizeOfImage-h.VirtualAddress||h.Misc.VirtualSize>32*1024*1024)continue;
            std::vector<unsigned char> code(h.Misc.VirtualSize);SIZE_T bytes=0;if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(base+h.VirtualAddress),code.data(),code.size(),&bytes))continue;
            for(const auto& pattern:patterns)for(std::size_t at=0;at+pattern.size()<=bytes;++at)if(matches(code.data()+at,pattern)){
                std::uint32_t address=0;std::memcpy(&address,code.data()+at+2,4);
                if(address>=base&&address+4<=base+nt.OptionalHeader.SizeOfImage){candidate=address;++count;}
            }
        }
        if(count==1)viewport_=candidate;
        if(log_)log_(viewport_?"Road GPS Line: rendered camera located":"Road GPS Line: rendered camera unavailable; authored camera fallback");
    }
public:
    explicit RoadSceneCamera(void(*log)(const char*)):log_(log){}
    void invalidate(){viewport_=0;gameplay_=0;retry_.reset();}
    roadnav::RoadProjection acquire(const roadnav::CameraFrame& camera){
        if(!viewport_&&retry_.due(GetTickCount()))discover();
        const auto sample=[&](std::uint32_t address){
            roadnav::RoadProjection result;
            if(address<0x10000||!read(address+0x180,result.view)||!read(address+0x1c0,result.projection))return roadnav::RoadProjection{};
            result.valid=result.usable()&&result.matchesCamera(camera);
            // Some viewport passes leave a non-gameplay/stale view matrix.
            // The documented inverse-view slot is an independent candidate;
            // retain every original perspective/position/direction gate.
            if(!result.valid){float inverse[4][4]{};
                if(read(address+0x140,inverse)&&result.fromInverseView(inverse))result.valid=result.matchesCamera(camera);
            }
            if(result.valid){read(address+0x2c0,result.nearClip);read(address+0x2c4,result.farClip);}
            return result;
        };
        // The current viewport can be a HUD/phone/mirror view at EndScene.
        // Keep the validated gameplay VIEWPORT OBJECT and read its live matrices
        // every draw, rather than alternating authored and rendered cameras.
        if(gameplay_){const auto result=sample(gameplay_);if(result.valid)return result;}
        std::uint32_t current=0;
        if(viewport_&&read(viewport_,current)){const auto result=sample(current);if(result.valid){gameplay_=current;return result;}}
        return {};
    }
};
