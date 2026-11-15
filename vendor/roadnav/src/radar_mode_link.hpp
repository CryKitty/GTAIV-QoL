#pragma once
#include "../../../src/radar_mode_link.hpp"
#include "radar_fill.hpp"
#include <string>
#include <mutex>

namespace radarmode {
// CE radar queue: two paired HUD/Radar preference gates, plus the radar-only
// zero and blips-only (2) gates. IV-SDK independently documents RadarMode as a
// uint32 exactly 16 bytes after HudOn. No captured VA is used.
inline std::uintptr_t resolve(const std::vector<radarfill::Section>& sections){
    std::vector<std::uintptr_t> modes,visited;
    for(const auto& s:sections)if(s.executable)for(std::size_t at=2;at+24<s.bytes.size();++at){
        const auto* p=s.bytes.data()+at;
        if(p[0]!=0x83||p[1]!=0x3d||p[6]!=0)continue;
        // Captured CE uses short JNE; accept an equivalent near JNE too.
        const unsigned next=p[7]==0x75?9u:p[7]==0x0f&&p[8]==0x85?13u:0u;
        if(!next||p[next]!=0x83||p[next+1]!=0x3d)continue;
        std::uint32_t first=0,second=0;std::memcpy(&first,p+2,4);std::memcpy(&second,p+next+2,4);
        if(first>0xffffffe0u||second!=first+16)continue;
        const auto lower=at>4096?at-4096:2;
        for(auto start=at;start>=lower;--start){
            if(((s.base+start)&15)||s.bytes[start-1]!=0xcc||s.bytes[start-2]!=0xcc)continue;
            const auto entry=s.base+start;if(std::find(visited.begin(),visited.end(),entry)!=visited.end())break;visited.push_back(entry);
            const auto body=radarfill::decode(entry,sections,radarfill::Body::Queue);
            std::uint32_t hud=0;std::memcpy(&hud,p+2,4);
            if(hud>0xffffffe0u)break;
            const auto radar=hud+16;
            unsigned hudZero=0,radarZero=0,radarTwo=0;
            for(const auto& i:body){const auto& d=i.decoded;
                if(d.opcode!=0x83||d.modrm!=0x3d)continue;
                if(d.disp.disp32==hud&&d.imm.imm8==0)++hudZero;
                if(d.disp.disp32==radar&&d.imm.imm8==0)++radarZero;
                if(d.disp.disp32==radar&&d.imm.imm8==2)++radarTwo;
            }
            if(hudZero==2&&radarZero==4&&radarTwo==1&&std::find(modes.begin(),modes.end(),radar)==modes.end())modes.push_back(radar);
            break;
        }
    }
    return modes.size()==1?modes.front():0;
}
inline std::uintptr_t discover(std::uintptr_t image=0){
    const auto base=image?image:reinterpret_cast<std::uintptr_t>(GetModuleHandleA(nullptr));
    IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS32 nt{};
    if(!radarfill::read(base,dos)||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<=0||dos.e_lfanew>4096||!radarfill::read(base+dos.e_lfanew,nt)||nt.Signature!=IMAGE_NT_SIGNATURE||nt.FileHeader.NumberOfSections>32)return 0;
    const auto headers=base+dos.e_lfanew+offsetof(IMAGE_NT_HEADERS32,OptionalHeader)+nt.FileHeader.SizeOfOptionalHeader;
    std::vector<radarfill::Section> sections;
    for(unsigned n=0;n<nt.FileHeader.NumberOfSections;++n){IMAGE_SECTION_HEADER s{};
        if(!radarfill::read(headers+n*sizeof(s),s)||!(s.Characteristics&IMAGE_SCN_MEM_EXECUTE)||s.VirtualAddress>=nt.OptionalHeader.SizeOfImage||s.Misc.VirtualSize>nt.OptionalHeader.SizeOfImage-s.VirtualAddress||s.Misc.VirtualSize>32*1024*1024)continue;
        radarfill::Section text{base+s.VirtualAddress,std::vector<unsigned char>(s.Misc.VirtualSize),true};SIZE_T count=0;
        if(ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(text.base),text.bytes.data(),text.bytes.size(),&count)&&count==text.bytes.size())sections.push_back(std::move(text));
    }
    const auto address=resolve(sections);std::uint32_t value=~0u;
    if((address&3)||address<base||address-base>=nt.OptionalHeader.SizeOfImage||sizeof(value)>nt.OptionalHeader.SizeOfImage-(address-base)||!radarfill::read(address,value)||value>2)return 0;
    // Require an actual committed writable data location, not an instruction.
    MEMORY_BASIC_INFORMATION info{};
    if(!VirtualQuery(reinterpret_cast<void*>(address),&info,sizeof(info))||info.State!=MEM_COMMIT||(info.Protect&(PAGE_GUARD|PAGE_NOACCESS))||!(info.Protect&(PAGE_READWRITE|PAGE_WRITECOPY)))return 0;
    return address;
}
}
class RadarModeLink {
    std::recursive_mutex mutex_;
    radarmodelink::State state_;
    std::uintptr_t address_=0,image_=0;
    std::string path_;
    void(*log_)(const char*)=nullptr;
    bool ready_=false,haveDesired_=false,desired_=false;
    unsigned event_=0;DWORD retry_=0;
    int read()const{std::uint32_t value=~0u;return radarfill::read(address_,value)&&value<=2?static_cast<int>(value):-1;}
    int load()const{char text[24]{};GetPrivateProfileStringA("Compass","RadarRestoreMode","-1",text,sizeof(text),path_.c_str());
        if(!std::strcmp(text,"-1"))return -1;if(!std::strcmp(text,"0"))return 0;if(!std::strcmp(text,"1"))return 1;if(!std::strcmp(text,"2"))return 2;return -2;}
    bool save(int value){char text[16];std::snprintf(text,sizeof(text),"%d",value);
        return WritePrivateProfileStringA("Compass","RadarRestoreMode",text,path_.c_str())&&WritePrivateProfileStringA(nullptr,nullptr,nullptr,path_.c_str());}
    bool write(int value){if(read()==value)return true;const auto mode=static_cast<std::uint32_t>(value);SIZE_T count=0;
        return radarmodelink::valid(value)&&WriteProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address_),&mode,sizeof(mode),&count)&&count==sizeof(mode);}
    void apply(const char* source){
        if(!ready_||!haveDesired_)return;
        const auto now=GetTickCount();if(!address_){if(retry_&&now-retry_<1000)return;retry_=now;address_=radarmode::discover(image_);}
        const auto r=state_.transition(desired_,*this);
        if(r.acted&&log_){char text[360];std::snprintf(text,sizeof(text),"Compass radar link: event=%u enabled=%u source=%s action=%s radar-mode-before=%d radar-mode-after=%d saved=%d verified=%u geometry-writes=0",event_,desired_?1u:0u,source,r.action,r.before,r.after,r.saved,r.ok?1u:0u);log_(text);}
    }
    friend class radarmodelink::State;
public:
    void start(const char* path,void(*log)(const char*),std::uintptr_t image=0){std::lock_guard<std::recursive_mutex> lock(mutex_);path_=path;log_=log;ready_=false;address_=0;image_=image;retry_=0;state_.reset();}
    void preference(bool enabled,unsigned event){std::lock_guard<std::recursive_mutex> lock(mutex_);haveDesired_=true;desired_=enabled;event_=event;apply("pause-setting");}
    void session(bool enabled){std::lock_guard<std::recursive_mutex> lock(mutex_);ready_=true;haveDesired_=true;desired_=enabled;event_=GetTickCount();apply("session-start");}
    void service(){std::lock_guard<std::recursive_mutex> lock(mutex_);if(ready_&&haveDesired_&&!address_)apply("discovery-retry");}
    void stop(){std::lock_guard<std::recursive_mutex> lock(mutex_);ready_=false; /* Preserve the journal and any manual change; next session handles recovery. */}
};
