#pragma once
#include <tlhelp32.h>
#include <cmath>
// Read-only discovery. Signatures are sourced from FusionFix comvars/cutscenecam.
// No engine calls, detours, pointer chasing, window messages, or game-memory writes.
namespace diagnostics {
struct Pattern { const unsigned char* bytes; const char* mask; unsigned size; };
inline bool Matches(const unsigned char* p, const Pattern& sig) {
    if(std::strlen(sig.mask)!=sig.size)return false;
    for (unsigned i=0;i<sig.size;++i) if (sig.mask[i]!='?' && p[i]!=sig.bytes[i]) return false;
    return true;
}
struct Target { const char* name; const unsigned char* address; unsigned size; bool raw; };
Target targets[16]{};
unsigned targetCount=0;
std::uintptr_t imageBase=0;
DWORD imageSize=0;
FILE* output=nullptr;
unsigned long outputBytes=0, maxBytes=1048576;
unsigned interval=250, duration=1800;
char outputPath[MAX_PATH]{};
void Emit(const char* line) {
    if (!output || outputBytes>=maxBytes) return;
    const int n=std::fprintf(output,"%lu %s\n",GetTickCount(),line);
    if (n>0) outputBytes+=static_cast<unsigned long>(n);
    std::fflush(output);
    if (outputBytes>=maxBytes) { std::fputs("capture stopped: byte budget reached\n",output); std::fflush(output); }
}
void Hex(const char* name,const unsigned char* p,unsigned count) {
    unsigned char bytes[256]{};
    char line[900]{};
    if (count>sizeof(bytes) || !Read(p,bytes,count)) { sprintf_s(line,"unreadable %s",name); Emit(line); return; }
    int pos=sprintf_s(line,"bytes %s rva=%08lX n=%u ",name,static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(p)-imageBase),count);
    for(unsigned i=0;i<count && pos>0;++i) pos+=sprintf_s(line+pos,sizeof(line)-static_cast<std::size_t>(pos),"%02X",bytes[i]);
    Emit(line);
}
const unsigned char* Unique(const char* name,const Pattern& sig) {
    const auto base=reinterpret_cast<unsigned char*>(imageBase);
    const auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    const auto nt=reinterpret_cast<IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
    const auto sections=IMAGE_FIRST_SECTION(nt);
    const unsigned char* found=nullptr; unsigned matches=0;
    for(unsigned s=0;s<nt->FileHeader.NumberOfSections;++s) {
        const auto& sec=sections[s];
        if (!(sec.Characteristics&IMAGE_SCN_MEM_EXECUTE) || sec.VirtualAddress>=imageSize || sec.Misc.VirtualSize>imageSize-sec.VirtualAddress) continue;
        std::vector<unsigned char> code(sec.Misc.VirtualSize);
        if (!Read(base+sec.VirtualAddress,code.data(),code.size())) continue;
        for(std::size_t i=0;i+sig.size<=code.size();++i) if(Matches(code.data()+i,sig)) { found=base+sec.VirtualAddress+i; ++matches; }
    }
    char line[180]; sprintf_s(line,"signature %s matches=%u rva=%08lX",name,matches,matches==1?static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(found)-imageBase):0ul); Emit(line);
    if(matches!=1) return nullptr;
    const auto remaining=imageSize-static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(found)-imageBase);
    Hex(name,found,remaining<96?remaining:96);
    return found;
}
void Add(const char* name,const unsigned char* code,unsigned operand,unsigned size,bool raw=false) {
    if(!code || targetCount>=16) return;
    std::uint32_t address=0;
    if(!Read(code+operand,&address,4) || address<imageBase || address-imageBase>imageSize || size>imageSize-(address-imageBase)) { Emit("target rejected: operand outside main image"); return; }
    targets[targetCount++]={name,reinterpret_cast<unsigned char*>(address),size,raw};
    char line[180]; sprintf_s(line,"target %s rva=%08lX bytes=%u interpretation=%s",name,static_cast<unsigned long>(address-imageBase),size,raw?"raw-unclassified":"source-derived"); Emit(line);
}
void Discover() {
    const unsigned char state[]={0x83,0x3D,0,0,0,0,0,0x75,0,0x83,0xF8,0,0x75,0,0xF3,0x0F,0x10,0x05,0,0,0,0,0xEB};
    Add("cutscene_state",Unique("cutscene_state",{state,"xx?????x?xx?x?xxxx????x",sizeof(state)}),2,4);
    const unsigned char pauses[]={0x0F,0xB6,0x0D,0,0,0,0,0x0F,0xB6,0x15,0,0,0,0,0x33,0xC0,0x0B,0xC1};
    const auto p=Unique("pause_flags_ce",{pauses,"xxx????xxx????xxxx",sizeof(pauses)});
    Add("user_pause",p,3,1); Add("script_pause",p,10,1);
    const unsigned char legacy[]={0x0F,0xB6,0x0D,0,0,0,0,0x0F,0xB6,0xC0,0x0B,0xC1};
    if(!p) { const auto q=Unique("pause_flags_alt",{legacy,"xxx????xxxxx",sizeof(legacy)}); Add("user_pause",q,3,1);
        // Require the second load opcode before decoding its absolute operand.
        unsigned char op[3]{}; if(q && Read(q+12,op,3) && op[0]==0x0F && op[1]==0xB6 && (op[2]&0xC7)==5) Add("script_pause",q,15,1);
    }
    const unsigned char audio[]={0xB9,0,0,0,0,0xF3,0x0F,0x11,0x44,0x24,0,0xE8,0,0,0,0,0x83,0x3D};
    Add("cutscene_audio_entity",Unique("cutscene_audio_entity",{audio,"x????xxxxx?x????xx",sizeof(audio)}),1,256,true);
    const unsigned char offset[]={0xF3,0x0F,0x5C,0x05,0,0,0,0,0x0F,0x2F,0xD0,0x76,0,0x5F};
    Add("audio_start_offset_float",Unique("audio_start_offset",{offset,"xxxx????xxxx?x",sizeof(offset)}),4,4);
    const unsigned char scale[]={0xF3,0x0F,0x10,0x05,0,0,0,0,0xF3,0x0F,0x11,0x44,0x24,0,0x56,0xE8,0,0,0,0,0x8B,0x0D};
    Add("time_scale_float",Unique("time_scale",{scale,"xxxx????xxxxx?xx????xx",sizeof(scale)}),4,4);
    const unsigned char clock[]={0xF3,0x0F,0x10,0x05,0,0,0,0,0xEB,0,0x66,0x0F,0x6E,0xC0};
    Add("system_time_float",Unique("system_time",{clock,"xxxx????x?xxxx",sizeof(clock)}),4,4);
    const unsigned char getter[]={0x0F,0xB6,0x81,0,0,0,0,0x40};
    Unique("audio_position_getter",{getter,"xxx????x",sizeof(getter)});
    const unsigned char getter2[]={0x0F,0xB6,0x91,0,0,0,0,0x83,0xC2};
    Unique("audio_position_getter_alt",{getter2,"xxx????xx",sizeof(getter2)});
}
bool AudioWord(const char* text) {
    char lower[160]{}; const auto n=std::strlen(text); if(n>=sizeof(lower))return false;
    for(std::size_t i=0;i<n;++i)lower[i]=(text[i]>='A'&&text[i]<='Z')?static_cast<char>(text[i]+32):text[i];
    return std::strstr(lower,"speech")||std::strstr(lower,"voice")||std::strstr(lower,"dialog")||std::strstr(lower,"volume")||std::strstr(lower,"conversation")||std::strstr(lower,"cutscene")||std::strstr(lower,"radio");
}
char candidateLabels[8][40]{};
unsigned candidateCount=0;
void CandidateGlobals(const unsigned char* code,unsigned size) {
    const auto base=reinterpret_cast<unsigned char*>(imageBase);
    const auto nt=reinterpret_cast<IMAGE_NT_HEADERS32*>(base+reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
    const auto sections=IMAGE_FIRST_SECTION(nt);
    for(unsigned i=0;i+8<size && candidateCount<8 && targetCount<16;++i) {
        unsigned operand=0;
        if(code[i]==0xA1||code[i]==0xA3)operand=i+1;
        else if(code[i]==0xD9 && (code[i+1]==0x05||code[i+1]==0x1D))operand=i+2;
        else if(code[i]==0xF3 && code[i+1]==0x0F && (code[i+2]==0x10||code[i+2]==0x11) && (code[i+3]&0xC7)==5)operand=i+4;
        if(!operand)continue;
        std::uint32_t address=0;std::memcpy(&address,code+operand,4);
        if(address<imageBase||address-imageBase>=imageSize)continue;
        bool writable=false,duplicate=false;
        for(unsigned t=0;t<targetCount;++t)if(targets[t].address==reinterpret_cast<unsigned char*>(address))duplicate=true;
        for(unsigned t=0;t<nt->FileHeader.NumberOfSections;++t) {
            const auto& sec=sections[t];const auto rva=address-imageBase;
            if((sec.Characteristics&IMAGE_SCN_MEM_WRITE)&&!(sec.Characteristics&IMAGE_SCN_MEM_EXECUTE)&&rva>=sec.VirtualAddress&&rva-sec.VirtualAddress<sec.Misc.VirtualSize&&sec.Misc.VirtualSize-(rva-sec.VirtualAddress)>=32)writable=true;
        }
        if(!writable||duplicate)continue;
        sprintf_s(candidateLabels[candidateCount],"audio_global_candidate_%u",candidateCount);
        Add(candidateLabels[candidateCount],code,operand,32,true);++candidateCount;
    }
}
void AudioReferences() {
    // Discover names and their direct absolute references for later mixer research.
    // These are candidates, never labelled as verified voice mixer controls.
    const auto base=reinterpret_cast<unsigned char*>(imageBase);
    const auto nt=reinterpret_cast<IMAGE_NT_HEADERS32*>(base+reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
    const auto sections=IMAGE_FIRST_SECTION(nt);
    unsigned names=0,refs=0;
    for(unsigned s=0;s<nt->FileHeader.NumberOfSections && names<64;++s) {
        const auto& sec=sections[s];
        if((sec.Characteristics&IMAGE_SCN_MEM_EXECUTE)||!(sec.Characteristics&IMAGE_SCN_MEM_READ)||sec.VirtualAddress>=imageSize||sec.Misc.VirtualSize>imageSize-sec.VirtualAddress)continue;
        std::vector<unsigned char> data(sec.Misc.VirtualSize); if(!Read(base+sec.VirtualAddress,data.data(),data.size()))continue;
        for(std::size_t i=1;i+5<data.size() && names<64;++i) {
            if(data[i-1]!=0||data[i]<32||data[i]>126)continue;
            std::size_t n=0; while(i+n<data.size()&&n<159&&data[i+n]>=32&&data[i+n]<=126)++n;
            if(n<5||n>=159||i+n>=data.size()||data[i+n]!=0)continue;
            const auto name=reinterpret_cast<const char*>(data.data()+i); if(!AudioWord(name)) {i+=n;continue;}
            char line[260];const auto address=static_cast<std::uint32_t>(imageBase+sec.VirtualAddress+i);
            sprintf_s(line,"audio_name_candidate rva=%08lX name=%s",static_cast<unsigned long>(address-imageBase),name);Emit(line);++names;
            for(unsigned t=0;t<nt->FileHeader.NumberOfSections&&refs<64;++t) {
                const auto& text=sections[t];if(!(text.Characteristics&IMAGE_SCN_MEM_EXECUTE)||text.VirtualAddress>=imageSize||text.Misc.VirtualSize>imageSize-text.VirtualAddress)continue;
                std::vector<unsigned char> code(text.Misc.VirtualSize);if(!Read(base+text.VirtualAddress,code.data(),code.size()))continue;
                for(std::size_t j=16;j+80<=code.size()&&refs<64;++j)if(std::memcmp(code.data()+j,&address,4)==0) {Hex("audio_name_xref_candidate",base+text.VirtualAddress+j-16,96);CandidateGlobals(code.data()+j-16,96);++refs;}
            }
            i+=n;
        }
    }
    char line[120];sprintf_s(line,"audio discovery complete names=%u refs=%u limits=64/64",names,refs);Emit(line);
}
DWORD WINAPI Worker(LPVOID) {
    if(fopen_s(&output,outputPath,"w")!=0 || !output) {Log("Diagnostics output could not be opened.");return 0;}
    Emit("QOL-DIAGNOSTICS v1 read-only; raw audio bytes are unclassified; no native functions invoked");
    const auto base=reinterpret_cast<unsigned char*>(GetModuleHandleA(nullptr));imageBase=reinterpret_cast<std::uintptr_t>(base);
    const auto nt=reinterpret_cast<IMAGE_NT_HEADERS32*>(base+reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);imageSize=nt->OptionalHeader.SizeOfImage;
    char line[1024];sprintf_s(line,"image base=%p size=%lu timestamp=%lu interval=%u duration=%u maxbytes=%lu",base,imageSize,nt->FileHeader.TimeDateStamp,interval,duration,maxBytes);Emit(line);
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,GetCurrentProcessId());MODULEENTRY32 module{};module.dwSize=sizeof(module);
    if(snapshot!=INVALID_HANDLE_VALUE) {if(Module32First(snapshot,&module))do {sprintf_s(line,"module %s base=%p size=%lu",module.szModule,module.modBaseAddr,module.modBaseSize);Emit(line);}while(Module32Next(snapshot,&module));CloseHandle(snapshot);}
    Discover();AudioReferences();
    unsigned char last[16][256]{};bool valid[16]{};const DWORD start=GetTickCount();DWORD heartbeat=start;bool previous=false;
    Emit("sampling starts; F8 optional marker only; Esc/Start and audio settings are observed without interception");
    while(GetTickCount()-start<duration*1000u && outputBytes<maxBytes) {
        const DWORD now=GetTickCount();const bool beat=now-heartbeat>=5000u;
        if(beat) {DWORD pid=0;const HWND window=GetForegroundWindow();if(window)GetWindowThreadProcessId(window,&pid);sprintf_s(line,"heartbeat elapsed=%lu foreground_game=%u minimized=%u",now-start,pid==GetCurrentProcessId()?1u:0u,(pid==GetCurrentProcessId()&&IsIconic(window))?1u:0u);Emit(line);heartbeat=now;}
        const bool marker=(GetAsyncKeyState(VK_F8)&0x8000)!=0;
        if(marker&&!previous)Emit("USER_MARKER F8");previous=marker;
        for(unsigned t=0;t<targetCount;++t) {
            const auto& target=targets[t];unsigned char bytes[256]{};const bool ok=Read(target.address,bytes,target.size);
            if(!ok) {if(valid[t]) {sprintf_s(line,"sample %s unreadable",target.name);Emit(line);}valid[t]=false;continue;}
            if(!valid[t]||std::memcmp(last[t],bytes,target.size)!=0||beat) {
                if(target.raw) { // A bounded, fixed object window; never follow its pointer values.
                    for(unsigned off=0;off<target.size;off+=32)if(!valid[t]||std::memcmp(last[t]+off,bytes+off,32)!=0||beat) {char label[100];sprintf_s(label,"%s+%u",target.name,off);Hex(label,target.address+off,32);}
                }else {std::uint32_t value=0;std::memcpy(&value,bytes,target.size);float f=0;std::memcpy(&f,&value,4);sprintf_s(line,"sample %s raw=%08lX uint=%lu float=%.6g valid_float=%u",target.name,static_cast<unsigned long>(value),static_cast<unsigned long>(value),static_cast<double>(f),std::isfinite(f)?1u:0u);Emit(line);}
                std::memcpy(last[t],bytes,target.size);valid[t]=true;
            }
        }
        Sleep(interval);
    }
    Emit("sampling complete (duration or byte budget); restart game for another session");std::fclose(output);output=nullptr;Log("Broad diagnostic capture finished.");return 0;
}
void Start(const char* ini,const char* exe,std::size_t prefix) {
    if(prefix+sizeof("GTAIV-QoL-diagnostics.log")>=MAX_PATH)return;
    if(!logging||GetPrivateProfileIntA("Diagnostics","BroadCapture",0,ini)!=1)return;
    if(!SupportedVersion(exe)) {Log("Broad diagnostics skipped: unsupported executable version.");return;}
    interval=GetPrivateProfileIntA("Diagnostics","SampleIntervalMs",250,ini);if(interval<100)interval=100;if(interval>5000)interval=5000;
    duration=GetPrivateProfileIntA("Diagnostics","CaptureSeconds",1800,ini);if(duration<30)duration=30;if(duration>3600)duration=3600;
    memcpy(outputPath,exe,prefix);strcpy_s(outputPath+prefix,MAX_PATH-prefix,"GTAIV-QoL-diagnostics.log");
    HANDLE thread=CreateThread(nullptr,0,Worker,nullptr,0,nullptr);if(thread){CloseHandle(thread);Log("Broad read-only cutscene/audio diagnostics worker started.");}else Log("Broad diagnostics thread creation failed.");
}
}
