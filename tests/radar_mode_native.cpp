#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cassert>
#include <cstdio>
#include <string>
#include "../vendor/roadnav/src/radar_mode_link.hpp"
static std::string trace;
static void Log(const char* text){trace+=text;trace+='\n';}
int main(){
    auto* image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x5000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));assert(image);
    const auto base=reinterpret_cast<std::uintptr_t>(image);
    auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(image);dos->e_magic=IMAGE_DOS_SIGNATURE;dos->e_lfanew=0x80;
    auto* nt=reinterpret_cast<IMAGE_NT_HEADERS32*>(image+0x80);nt->Signature=IMAGE_NT_SIGNATURE;nt->FileHeader.NumberOfSections=1;nt->FileHeader.SizeOfOptionalHeader=sizeof(IMAGE_OPTIONAL_HEADER32);nt->OptionalHeader.SizeOfImage=0x5000;
    auto* section=IMAGE_FIRST_SECTION(nt);section->VirtualAddress=0x1000;section->Misc.VirtualSize=0x1000;section->Characteristics=IMAGE_SCN_MEM_EXECUTE|IMAGE_SCN_MEM_READ;
    std::memset(image+0x1000,0xcc,0x1000);
    auto& native=*reinterpret_cast<unsigned*>(image+0x3020);
    unsigned at=0x1100;
    const auto cmp=[&](unsigned address,unsigned value){image[at++]=0x83;image[at++]=0x3d;const auto p=static_cast<unsigned>(base+address);std::memcpy(image+at,&p,4);at+=4;image[at++]=static_cast<unsigned char>(value);};
    const auto jump=[&](bool nearJump){if(nearJump){image[at++]=0x0f;image[at++]=0x85;std::memset(image+at,0,4);at+=4;}else{image[at++]=0x75;image[at++]=0;}};
    // Captured CE uses the short form; also exercise the near encoding.
    for(unsigned i=0;i<2;++i){cmp(0x3010,0);jump(i!=0);cmp(0x3020,0);}
    cmp(0x3020,0);cmp(0x3020,2);cmp(0x3020,0);image[at]=0xc3;
    const auto sections=[&](){return std::vector<radarfill::Section>{{base+0x1000,std::vector<unsigned char>(image+0x1000,image+0x2000),true}};};
    assert(radarmode::resolve(sections())==base+0x3020);assert(radarmode::discover(base)==base+0x3020);
    std::memcpy(image+0x1800,image+0x1100,at+1-0x1100);assert(radarmode::resolve(sections())==base+0x3020); // same field, same identity
    for(unsigned i=0x1800;i<0x1800+at+1-0x1100;++i)if(image[i]==0x83&&image[i+1]==0x3d){unsigned p=0;std::memcpy(&p,image+i+2,4);p+=0x20;std::memcpy(image+i+2,&p,4);}
    assert(!radarmode::resolve(sections()));std::memset(image+0x1800,0xcc,at+1-0x1100);
    char folder[MAX_PATH]{},file[MAX_PATH]{};assert(GetTempPathA(MAX_PATH,folder));assert(GetTempFileNameA(folder,"qrl",0,file));
    for(unsigned baseline=0;baseline<=2;++baseline){
        assert(WritePrivateProfileStringA("Compass","RadarRestoreMode","-1",file));native=baseline;
        RadarModeLink link;link.start(file,Log,base);link.session(true);assert(native==0);
        assert(GetPrivateProfileIntA("Compass","RadarRestoreMode",9,file)==baseline);
        native=2;for(unsigned frame=0;frame<100;++frame)link.service();assert(native==2);
        link.stop();assert(native==2); // unload doesn't fight manual changes
        RadarModeLink relaunched;relaunched.start(file,Log,base);relaunched.session(true);assert(native==0);
        relaunched.preference(false,1234);assert(native==baseline);
        assert(static_cast<int>(GetPrivateProfileIntA("Compass","RadarRestoreMode",9,file))==-1);
        native=1;relaunched.preference(true,1235);assert(native==0);
        relaunched.preference(false,1236);assert(native==1);
    }
    assert(WritePrivateProfileStringA("Compass","RadarRestoreMode","broken",file));native=1;
    RadarModeLink invalid;invalid.start(file,Log,base);invalid.session(true);assert(native==1);
    assert(trace.find("event=1234 enabled=0 source=pause-setting action=restored-and-cleared")!=std::string::npos);
    assert(trace.find("invalid-recovery-record")!=std::string::npos);
    DeleteFileA(file);VirtualFree(image,0,MEM_RELEASE);
    std::puts("Native radar link: unique CE preference resolution, persisted mode 0/1/2, manual override, relaunch and exact transition restoration passed");
}
