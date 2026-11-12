#pragma once
#include "cutscene_clock.hpp"
namespace cinematictime {
using Getter=int(__thiscall*)(void*);
Getter original=nullptr;
void* entity=nullptr;
SRWLOCK lock=SRWLOCK_INIT;
cutscenepause::Timeline timeline;
bool installed=false;
int __fastcall Get(void* object,void*){
    const int raw=original(object);
    if(object!=entity)return raw;
    AcquireSRWLockExclusive(&lock);const int result=timeline.Filter(raw);ReleaseSRWLockExclusive(&lock);return result;
}
bool Begin(){
    if(!installed)return false;
    const int raw=original(entity);AcquireSRWLockExclusive(&lock);const bool ok=timeline.Begin(raw);ReleaseSRWLockExclusive(&lock);
    if(ok){char line[140];sprintf_s(line,"cinematic timeline held: raw_audio=%d",raw);Log(line);}return ok;
}
void End(){
    if(!installed)return;
    const int raw=original(entity);AcquireSRWLockExclusive(&lock);const bool held=timeline.held;timeline.End(raw);const auto offset=timeline.offset;ReleaseSRWLockExclusive(&lock);
    if(held){char line[180];sprintf_s(line,"cinematic timeline released: raw_audio=%d accumulated_pause_offset=%lld",raw,static_cast<long long>(offset));Log(line);}
}
void Reset(){AcquireSRWLockExclusive(&lock);timeline.Reset();ReleaseSRWLockExclusive(&lock);}
bool Install(){
    if(installed)return true;
    auto base=reinterpret_cast<unsigned char*>(GetModuleHandleA(nullptr));auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);auto nt=reinterpret_cast<IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);auto sections=IMAGE_FIRST_SECTION(nt);
    void* getter=nullptr;DWORD object=0;unsigned getterMatches=0,entityMatches=0;
    const unsigned char a[]={0x0F,0xB6,0x81,0,0,0,0,0x40};
    const unsigned char b[]={0x0F,0xB6,0x91,0,0,0,0,0x83,0xC2};
    const unsigned char e[]={0xB9,0,0,0,0,0xF3,0x0F,0x11,0x44,0x24,0,0xE8,0,0,0,0,0x83,0x3D};
    const auto match=[](const unsigned char* code,const unsigned char* pattern,const char* mask,std::size_t n){for(std::size_t k=0;k<n;++k)if(mask[k]!='?'&&code[k]!=pattern[k])return false;return true;};
    for(unsigned s=0;s<nt->FileHeader.NumberOfSections;++s){const auto& sec=sections[s];if(!(sec.Characteristics&IMAGE_SCN_MEM_EXECUTE)||sec.VirtualAddress>=nt->OptionalHeader.SizeOfImage||sec.Misc.VirtualSize>nt->OptionalHeader.SizeOfImage-sec.VirtualAddress)continue;
        std::vector<unsigned char> code(sec.Misc.VirtualSize);if(!Read(base+sec.VirtualAddress,code.data(),code.size()))continue;
        for(std::size_t i=0;i+sizeof(e)<=code.size();++i){
            if(match(code.data()+i,a,"xxx????x",sizeof(a))||match(code.data()+i,b,"xxx????xx",sizeof(b))){getter=base+sec.VirtualAddress+i;++getterMatches;}
            if(match(code.data()+i,e,"x????xxxxx?x????xx",sizeof(e))){std::memcpy(&object,code.data()+i+1,4);++entityMatches;}
        }
    }
    bool data=false;const auto image=reinterpret_cast<std::uintptr_t>(base);
    if(object>=image&&object-image<nt->OptionalHeader.SizeOfImage)for(unsigned s=0;s<nt->FileHeader.NumberOfSections;++s){const auto& sec=sections[s];const auto rva=object-image;if((sec.Characteristics&IMAGE_SCN_MEM_WRITE)&&!(sec.Characteristics&IMAGE_SCN_MEM_EXECUTE)&&rva>=sec.VirtualAddress&&rva-sec.VirtualAddress<sec.Misc.VirtualSize&&256u<=sec.Misc.VirtualSize-(rva-sec.VirtualAddress))data=true;}
    char line[200];sprintf_s(line,"cinematic timeline discovery getter_matches=%u entity_matches=%u getter=%p entity=%08lX",getterMatches,entityMatches,getter,object);Log(line);
    if(getterMatches!=1||entityMatches!=1||!data||!Executable(getter)){Log("cinematic timeline unavailable: signatures missing or ambiguous");return false;}
    const MH_STATUS init=MH_Initialize();if(init!=MH_OK&&init!=MH_ERROR_ALREADY_INITIALIZED)return false;
    void* trampoline=nullptr;if(MH_CreateHook(getter,reinterpret_cast<void*>(&Get),&trampoline)!=MH_OK){Log("cinematic timeline hook creation failed");return false;}
    original=reinterpret_cast<Getter>(trampoline);entity=reinterpret_cast<void*>(object);
    if(MH_EnableHook(getter)!=MH_OK){MH_RemoveHook(getter);original=nullptr;entity=nullptr;Log("cinematic timeline hook activation failed");return false;}
    installed=true;Log("cinematic audio timeline hook ready; pause holds time and compensates resume");return true;
}
}
