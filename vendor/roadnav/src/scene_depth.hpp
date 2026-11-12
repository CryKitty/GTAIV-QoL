#pragma once
#include <windows.h>
#include <d3d9.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>
#include "scene_discovery.hpp"

// Read-only CViewport3DScene::pGBufferRTs discovery, from FusionFix's documented
// CE signatures and grcRenderTargetPC layout. No rendering or shared DLL hooks.
class RoadSceneDepth {
    std::uintptr_t buffers_=0;SceneDiscoveryRetry retry_{};void(*log_)(const char*)=nullptr;
    std::uintptr_t(*locate_)()=nullptr;DWORD(WINAPI* now_)()=&GetTickCount;
    template<class T>static bool read(std::uintptr_t address,T& out){SIZE_T bytes=0;return address>=0x10000&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),&out,sizeof(out),&bytes)&&bytes==sizeof(out);}
    static bool matches(const unsigned char* code,const std::vector<int>& pattern){for(std::size_t i=0;i<pattern.size();++i)if(pattern[i]>=0&&code[i]!=pattern[i])return false;return true;}
    void discover(){
        const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleA(nullptr));IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS nt{};
        if(!read(base,dos)||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<=0||dos.e_lfanew>0x1000||!read(base+dos.e_lfanew,nt)||nt.Signature!=IMAGE_NT_SIGNATURE||nt.FileHeader.NumberOfSections>32)return;
        const std::array<std::vector<int>,2> patterns{{
            {0xa3,-1,-1,-1,-1,0xff,0x35,-1,-1,-1,-1,0x8b,0x01,0xff,0x35,-1,-1,-1,-1,0x6a,0x03,0x68,-1,-1,-1,-1,0xff,0x50,0x38,0x8b,0x0d},
            {0xa3,-1,-1,-1,-1,0x8b,0x01,0x8b,0x40,0x38,0x6a,0x03,0x68,-1,-1,-1,-1,0xff,0xd0,0x8b,0x0d,-1,-1,-1,-1,0xa3,-1,-1,-1,-1,0x8b,0x11,0x8b,0x52,0x38}
        }};
        const auto sections=base+dos.e_lfanew+offsetof(IMAGE_NT_HEADERS,OptionalHeader)+nt.FileHeader.SizeOfOptionalHeader;
        unsigned count=0;std::uintptr_t candidate=0;
        for(unsigned i=0;i<nt.FileHeader.NumberOfSections;++i){IMAGE_SECTION_HEADER h{};
            if(!read(sections+i*sizeof(h),h)||!(h.Characteristics&IMAGE_SCN_MEM_EXECUTE)||h.VirtualAddress>=nt.OptionalHeader.SizeOfImage||h.Misc.VirtualSize>nt.OptionalHeader.SizeOfImage-h.VirtualAddress||h.Misc.VirtualSize>32*1024*1024)continue;
            std::vector<unsigned char> code(h.Misc.VirtualSize);SIZE_T bytes=0;if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(base+h.VirtualAddress),code.data(),code.size(),&bytes))continue;
            for(const auto& pattern:patterns)for(std::size_t at=0;at+pattern.size()<=bytes;++at)if(matches(code.data()+at,pattern)){
                std::uint32_t address=0;std::memcpy(&address,code.data()+at+1,4);
                if(address>=base&&address+4*4<=base+nt.OptionalHeader.SizeOfImage){candidate=address;++count;}
            }
        }
        if(count==1)buffers_=candidate;
        if(log_)log_(buffers_?"Road GPS Line: scene G-buffer table located":"Road GPS Line: scene depth signature unavailable; no see-through fallback");
    }
public:
    // Optional discovery/clock sources allow deterministic testing of the real
    // cache and texture validation, without scanning a game executable.
    explicit RoadSceneDepth(void(*log)(const char*),std::uintptr_t(*locate)()=nullptr,DWORD(WINAPI* now)()=&GetTickCount):log_(log),locate_(locate),now_(now){}
    void invalidate(){buffers_=0;retry_.reset();}
    IDirect3DTexture9* acquire(){
        if(!buffers_&&retry_.due(now_())){if(locate_)buffers_=locate_();else discover();}if(!buffers_)return nullptr;
        std::uint32_t target=0,name=0,texture=0;
        if(!read(buffers_+3*4,target)||target<0x10000||!read(target+0x30,name)||!read(target+0x34,texture)||texture<0x10000)return nullptr;
        constexpr char expected[]="_DEFERRED_GBUFFER_3_";
        for(std::size_t i=0;i<sizeof(expected);++i){char c=0;if(!read(static_cast<std::uintptr_t>(name)+i,c)||c!=expected[i])return nullptr;}
        std::uint32_t table=0,method=0;if(!read(texture,table)||!read(table,method)||method<0x10000)return nullptr;
        auto result=reinterpret_cast<IDirect3DTexture9*>(texture);D3DSURFACE_DESC desc{};
        if(FAILED(result->GetLevelDesc(0,&desc))||desc.Width<160||desc.Height<100||desc.Width>16384||desc.Height>16384)return nullptr;
        result->AddRef();return result;
    }
};
