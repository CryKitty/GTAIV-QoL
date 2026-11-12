#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
// Mapping checks only: FusionFix global constructors require an actual GTA
// image. Do not claim that an ordinary process can validate its game startup.
extern "C" void __cdecl IsUltimateASILoader() {}
#pragma comment(linker,"/EXPORT:IsUltimateASILoader=_IsUltimateASILoader")
int main(int argc,char** argv){
    if(argc<2)return 2;
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    if(!GetProcAddress(GetModuleHandleA(nullptr),"IsUltimateASILoader"))return 3;
    void* reserved=argc>2?VirtualAlloc(reinterpret_cast<void*>(0x10000000),0x800000,MEM_RESERVE,PAGE_NOACCESS):nullptr;
    if(argc>2&&!reserved)return 4;
    auto module=LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
    if(!module){std::printf("FusionFix LoadLibrary failed: %lu\n",GetLastError());return 5;}
    if(reserved&&module==reserved)return 6;
    if(!GetProcAddress(module,"InitializeASI")||!GetProcAddress(module,"QoLCompassRadarRange")||!GetProcAddress(module,"QoLFusionFixBridgeVersion"))return 7;
    std::printf("FusionFix PE mapping passed at %p (relocated=%u)\n",static_cast<void*>(module),reserved?1u:0u);
    if(!FreeLibrary(module))return 8;
    if(reserved)VirtualFree(reserved,0,MEM_RELEASE);
    return 0;
}
