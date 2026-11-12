#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include <mmsystem.h>
#include <dsound.h>
#include <tlhelp32.h>
#include <xinput.h>
#include <winver.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <atomic>
#include <fstream>
#include <memory>
#include <unordered_map>
#include "voice_metadata.hpp"
#pragma warning(push)
#pragma warning(disable: 4251 4275)
#include "../vendor/scripthook/ScriptThread.h"
#include "../vendor/scripthook/NativeContext.h"
#include "../vendor/scripthook/Services/D3DHook.h"
#pragma warning(pop)
#include "pause_session.hpp"
#include "qol_settings_client.hpp"
extern "C" void InitializeBundledRoadNav(HMODULE);
#include "cutscene_clock.hpp"
#include "pause_natives.hpp"
#include "pause_build.hpp"
#ifndef QOL_BUILD_ID
#define QOL_BUILD_ID "local"
#endif
static_assert(sizeof(void*)==4,"x86 required");
namespace {
char logPath[MAX_PATH]{},iniPath[MAX_PATH]{};
HMODULE ownModule=nullptr;
INIT_ONCE once=INIT_ONCE_STATIC_INIT;
unsigned char* scriptPause=nullptr;
unsigned char* userPause=nullptr;
void Log(const char* text){FILE* f=nullptr;if(fopen_s(&f,logPath,"a")==0&&f){std::fprintf(f,"%lu %s\n",GetTickCount(),text);std::fclose(f);}}
bool Read(const void* p,void* out,SIZE_T size){SIZE_T n=0;return ReadProcessMemory(GetCurrentProcess(),p,out,size,&n)&&n==size;}
bool Version(const char* exe){DWORD ignored=0;const DWORD size=GetFileVersionInfoSizeA(exe,&ignored);if(!size)return false;std::vector<unsigned char> data(size);if(!GetFileVersionInfoA(exe,0,size,data.data()))return false;VS_FIXEDFILEINFO* info=nullptr;UINT n=0;return VerQueryValueA(data.data(),"\\",reinterpret_cast<void**>(&info),&n)&&n>=sizeof(*info)&&info->dwSignature==0xFEEF04BD&&info->dwFileVersionMS==MAKELONG(2,1)&&info->dwFileVersionLS==MAKELONG(59,0);}
using Handler=void(__cdecl*)(GameTypes::scrNativeCallContext*);
using Lookup=void*(__cdecl*)(unsigned);
using NameLookup=void*(__cdecl*)(const char*);
Handler pauseGame=nullptr,resumeGame=nullptr,pauseConversation=nullptr,loaded=nullptr,finished=nullptr,network=nullptr,pauseMenu=nullptr,audioTime=nullptr,sceneTime=nullptr;
DWORD* cutsceneState=nullptr;
bool Executable(const void* p){MEMORY_BASIC_INFORMATION m{};if(!p||!VirtualQuery(p,&m,sizeof(m))||m.State!=MEM_COMMIT||(m.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;return (m.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))!=0;}
void Call(Handler fn){NativeContext ctx;fn(&ctx);}
void Conversation(bool pause){NativeContext ctx;ctx.Push(static_cast<unsigned>(pause?1:0));pauseConversation(&ctx);}
class QueryContext:public NativeContext {public:QueryContext(){*static_cast<unsigned*>(m_pReturn)=0;}};
bool Query(Handler fn){QueryContext result;fn(&result);return result.GetResult<unsigned>()!=0;}
#include "cutscene_output.hpp"
#include "audio_engine_capture.hpp"
#include "directsound_capture.hpp"
namespace allaudio {
bool Available(){return audioengine::Available()||directaudio::Available();}
bool StopAll(){const bool ds=directaudio::StopAll();const bool xa=!audioengine::Available()||audioengine::StopAll();return ds&&xa;}
bool ResumeAll(){const bool ds=directaudio::ResumeAll();const bool xa=audioengine::ResumeAll();return ds&&xa;}
bool Stopped(){return directaudio::Stopped()||audioengine::Stopped();}
}
#include "cutscene_timeline.hpp"
#include "pause_overlay.hpp"
#include "menu_value_text.hpp"

int Clock(Handler fn){QueryContext result;fn(&result);return result.GetResult<int>();}
// Resolve source-derived state/flag reads independently of the native handlers.
void Discover(){
    auto base=reinterpret_cast<unsigned char*>(GetModuleHandleA(nullptr));auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);auto nt=reinterpret_cast<IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);auto sections=IMAGE_FIRST_SECTION(nt);
    unsigned stateCount=0,flagCount=0;DWORD stateAddress=0,scriptAddress=0,userAddress=0;
    const unsigned char state[]={0x83,0x3D,0,0,0,0,0,0x75,0,0x83,0xF8,0,0x75,0,0xF3,0x0F,0x10,0x05,0,0,0,0,0xEB};const char* mask="xx?????x?xx?x?xxxx????x";
    for(unsigned s=0;s<nt->FileHeader.NumberOfSections;++s){const auto& sec=sections[s];if(!(sec.Characteristics&IMAGE_SCN_MEM_EXECUTE)||sec.VirtualAddress>=nt->OptionalHeader.SizeOfImage||sec.Misc.VirtualSize>nt->OptionalHeader.SizeOfImage-sec.VirtualAddress)continue;std::vector<unsigned char> code(sec.Misc.VirtualSize);if(!Read(base+sec.VirtualAddress,code.data(),code.size()))continue;
        for(std::size_t i=0;i+sizeof(state)<=code.size();++i){bool match=true;for(unsigned j=0;j<sizeof(state);++j)if(mask[j]!='?'&&code[i+j]!=state[j]){match=false;break;}if(match){memcpy(&stateAddress,code.data()+i+2,4);++stateCount;}
            if(code[i]==0x0F&&code[i+1]==0xB6&&code[i+2]==0x0D&&code[i+7]==0x0F&&code[i+8]==0xB6&&code[i+9]==0x15&&code[i+14]==0x33&&code[i+15]==0xC0&&code[i+16]==0x0B&&code[i+17]==0xC1){memcpy(&userAddress,code.data()+i+3,4);memcpy(&scriptAddress,code.data()+i+10,4);++flagCount;}else if(code[i]==0x0F&&code[i+1]==0xB6&&code[i+2]==0x0D&&code[i+7]==0x0F&&code[i+8]==0xB6&&code[i+9]==0xC0&&code[i+10]==0x0B&&code[i+11]==0xC1&&code[i+12]==0x0F&&code[i+13]==0xB6&&(code[i+14]&0xC7)==5){memcpy(&userAddress,code.data()+i+3,4);memcpy(&scriptAddress,code.data()+i+15,4);++flagCount;}
        }
    }
    const auto inData=[&](DWORD address,unsigned size){const auto image=reinterpret_cast<std::uintptr_t>(base);if(address<image||address-image>=nt->OptionalHeader.SizeOfImage)return false;for(unsigned s=0;s<nt->FileHeader.NumberOfSections;++s){const auto& sec=sections[s];const auto rva=address-image;if((sec.Characteristics&IMAGE_SCN_MEM_WRITE)&&!(sec.Characteristics&IMAGE_SCN_MEM_EXECUTE)&&rva>=sec.VirtualAddress&&rva-sec.VirtualAddress<sec.Misc.VirtualSize&&size<=sec.Misc.VirtualSize-(rva-sec.VirtualAddress))return true;}return false;};
    if(stateCount==1&&inData(stateAddress,4))cutsceneState=reinterpret_cast<DWORD*>(stateAddress);
    if(flagCount==1&&inData(userAddress,1)&&inData(scriptAddress,1)&&userAddress!=scriptAddress){userPause=reinterpret_cast<unsigned char*>(userAddress);scriptPause=reinterpret_cast<unsigned char*>(scriptAddress);}
    char line[180];sprintf_s(line,"discovery state_matches=%u flags_matches=%u state=%p user=%p script=%p",stateCount,flagCount,cutsceneState,userPause,scriptPause);Log(line);
}
using PadGet=DWORD(WINAPI*)(DWORD,XINPUT_STATE*);
PadGet padGet=nullptr;
void AudioModules(){HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,GetCurrentProcessId());if(snapshot==INVALID_HANDLE_VALUE)return;MODULEENTRY32 module{};module.dwSize=sizeof(module);if(Module32First(snapshot,&module))do{char name[MAX_PATH]{};strcpy_s(name,module.szModule);for(char* p=name;*p;++p)if(*p>='A'&&*p<='Z')*p=static_cast<char>(*p+32);if(strstr(name,"audio")||strstr(name,"dsound")||strstr(name,"xact")||strstr(name,"openal")||strstr(name,"fmod")){char line[340];sprintf_s(line,"runtime audio module %s base=%p",module.szModule,module.modBaseAddr);Log(line);}}while(Module32Next(snapshot,&module));CloseHandle(snapshot);}
void SetupPad(){const char* names[]={"xinput1_3.dll","xinput1_4.dll","xinput9_1_0.dll"};for(const char* name:names){HMODULE mod=GetModuleHandleA(name);if(!mod)mod=LoadLibraryA(name);if(mod){padGet=reinterpret_cast<PadGet>(GetProcAddress(mod,"XInputGetState"));if(padGet)return;}}}
bool Pressed(){if(GetAsyncKeyState(VK_ESCAPE)&0x8000)return true;if(padGet)for(DWORD p=0;p<4;++p){XINPUT_STATE state{};if(padGet(p,&state)==ERROR_SUCCESS&&(state.Gamepad.wButtons&XINPUT_GAMEPAD_START)!=0)return true;}return false;}
class PauseScript final:public ScriptThread {
    cutscenepause::Session session;
    bool ready=false,tickLogged=false,inputPreviously=false;
    DWORD lastHeartbeat=0;
    cutscenepause::CinematicClocks clocks;
    void Resume(){pauseoverlay::Set(false);cinematictime::End();Call(resumeGame);Conversation(false);allaudio::ResumeAll();Log("RESUME: UNPAUSE_GAME and PAUSE_SCRIPTED_CONVERSATION(false) called on game thread");}
protected:
    void OnStart() override{
        // ASI load order can put this companion before the settings owner.
        // Script start runs after all plugins have loaded; retry the deferred install.

        pauseoverlay::Set(false);pauseoverlay::Register();ready=false;session={};tickLogged=false;inputPreviously=false;lastHeartbeat=0;auto mod=GetModuleHandleA("ScriptHook.dll");auto lookup=mod?reinterpret_cast<Lookup>(GetProcAddress(mod,"?GetNativeAddressFromHash@Game@@SAPAXI@Z")):nullptr;
        auto lookupName=mod?reinterpret_cast<NameLookup>(GetProcAddress(mod,"?GetNativeAddress@Game@@SAPAXPBD@Z")):nullptr;
        if(!lookup&&!lookupName){Log("pause disabled: ScriptHook native lookup unavailable");return;}
        Handler resolved[9]{};
        for(unsigned i=0;i<9;++i){
            const auto& spec=cutscenepause::natives[i];
            auto address=cutscenepause::Resolve(spec,[&](unsigned hash)->void*{return lookup?lookup(hash):nullptr;},[&](const char* name)->void*{return lookupName?lookupName(name):nullptr;},Executable);
            resolved[i]=reinterpret_cast<Handler>(address);
            char line[200];sprintf_s(line,"native %s sdk_hash=%08X address=%p status=%s",spec.name,spec.sdkHash,address,address?"ready":"missing");Log(line);
        }
        pauseGame=resolved[0];resumeGame=resolved[1];pauseConversation=resolved[2];loaded=resolved[3];finished=resolved[4];network=resolved[5];pauseMenu=resolved[6];audioTime=resolved[7];sceneTime=resolved[8];
        Discover();SetupPad();AudioModules();cinematictime::Install();ready=pauseGame&&resumeGame&&pauseConversation&&loaded&&finished&&network&&pauseMenu&&audioTime&&sceneTime&&cutsceneState&&scriptPause&&userPause;
        Log(ready?"native handlers ready; actual pause also requires captured game audio; Esc/Start; pause remains active until resumed":"pause disabled: native handlers or unique state/flag signatures missing");
    }
    void OnKill() override{cutsceneoutput::Apply(1);directaudio::ApplyVolume();if(ready&&session.owned){Resume();session.owned=false;}pauseoverlay::Set(false);ready=false;Log("pause script stopped");}
    void RunScript() override{using Dispatch=void(__thiscall*)(ScriptThread*);auto mod=GetModuleHandleA("ScriptHook.dll");auto dispatch=mod?reinterpret_cast<Dispatch>(GetProcAddress(mod,"?RunScript@ScriptThread@@MAEXXZ")):nullptr;if(dispatch)dispatch(this);else Log("pause dispatcher export missing");}
    void RunTick() override{
        DWORD audioState=0;const bool scene=loaded&&finished&&Read(cutsceneState,&audioState,4)&&(audioState==8||audioState==9)&&Query(loaded)&&!Query(finished);
        const float multiplier=cutscenemix::gain(scene,qolclient::Enabled(QOL_VOCAL_SLIDERS),qolclient::Get(QOL_CUTSCENE_VOICE,100));
        const float previous=cutsceneoutput::gain.load();cutsceneoutput::Apply(multiplier);if(previous!=multiplier)directaudio::ApplyVolume();
        if(!ready)return;
        if(!qolclient::Enabled(QOL_CUTSCENE_PAUSE)){
            if(session.owned){session.owned=false;Resume();}
            if(allaudio::Stopped())allaudio::ResumeAll();
            session.Observe(Pressed(),false,false);return;
        }
        if(!tickLogged){Log("pause game-thread tick active");tickLogged=true;}
        DWORD pid=0;HWND window=GetForegroundWindow();if(window)GetWindowThreadProcessId(window,&pid);DWORD state=0;unsigned char sp=1,up=1;
        const bool read=Read(cutsceneState,&state,4)&&Read(scriptPause,&sp,1)&&Read(userPause,&up,1);
        const bool sceneLoaded=Query(loaded),sceneFinished=Query(finished),online=Query(network),menu=Query(pauseMenu);
        const bool active=read&&(state==8||(session.owned&&state==9))&&sceneLoaded&&!sceneFinished;
        const bool allowed=pid==GetCurrentProcessId()&&!online&&!menu&&(session.owned||(sp==0&&up==0));
        const bool pressed=Pressed();const DWORD now=GetTickCount();
        if((pressed&&!inputPreviously)||now-lastHeartbeat>=5000u){char line[260];sprintf_s(line,"pause observation input=%u state=%lu loaded=%u finished=%u foreground=%u network=%u menu=%u script=%u user=%u owned=%u active=%u allowed=%u",pressed?1u:0u,state,sceneLoaded?1u:0u,sceneFinished?1u:0u,pid==GetCurrentProcessId()?1u:0u,online?1u:0u,menu?1u:0u,static_cast<unsigned>(sp),static_cast<unsigned>(up),session.owned?1u:0u,active?1u:0u,allowed?1u:0u);Log(line);lastHeartbeat=now;}inputPreviously=pressed;
        if(!session.owned&&!active)cinematictime::Reset();
        if(!session.owned&&allaudio::Stopped())allaudio::ResumeAll();
        if(session.owned){const int a=Clock(audioTime),c=Clock(sceneTime);if(!clocks.Stable(a,c)){char line[180];sprintf_s(line,"coordinated pause rejected: clocks advanced audio=%d->%d scene=%d->%d",clocks.audio,a,clocks.scene,c);Log(line);session.owned=false;Resume();return;}}
        const auto action=session.Observe(pressed,active,allowed);
        if(action==cutscenepause::Action::Resume)Resume();
        else if(action==cutscenepause::Action::Pause){
            const int beforeAudio=Clock(audioTime);
            if(beforeAudio<0){session.owned=false;Log("pause rejected: cinematic audio clock unavailable");return;}
            if(!allaudio::Available()){session.owned=false;Log("pause rejected: game audio engine not captured; input detected, timer has not started");return;}
            if(!allaudio::StopAll()){allaudio::ResumeAll();session.owned=false;Log("pause rejected: audio processing could not be stopped");return;}
            if(!cinematictime::Begin()){allaudio::ResumeAll();session.owned=false;Log("pause rejected: cinematic timeline hold unavailable; audio restored");return;}
            if(!clocks.Hold(Clock(audioTime),Clock(sceneTime))){cinematictime::End();allaudio::ResumeAll();session.owned=false;Log("pause rejected: cinematic clocks unavailable; audio restored");return;}
            Conversation(true);Call(pauseGame);unsigned char after=0;
            if(!(Read(scriptPause,&after,1)&&sp==0&&after==1)) {session.owned=false;Resume();Log("pause attempt reverted: PAUSE_GAME did not set the decoded script-pause flag");return;}
            pauseoverlay::Set(true);
            Log("PAUSE: captured audio stop requested plus game/conversation pause; script-pause=1; cinematic clocks monitored");
        }
    }
};
BOOL CALLBACK Initialize(PINIT_ONCE,PVOID,PVOID*){
    char exe[MAX_PATH]{};DWORD n=GetModuleFileNameA(nullptr,exe,MAX_PATH);if(!n||n>=MAX_PATH)return TRUE;char* slash=strrchr(exe,'\\');if(!slash)return TRUE;const auto prefix=static_cast<std::size_t>(slash-exe+1);if(prefix+32>=MAX_PATH)return TRUE;
    memcpy(iniPath,exe,prefix);strcpy_s(iniPath+prefix,MAX_PATH-prefix,"GTAIV-QoL.ini");memcpy(logPath,exe,prefix);strcpy_s(logPath+prefix,MAX_PATH-prefix,"GTAIV-QoL-pause.log");FILE* f=nullptr;if(fopen_s(&f,logPath,"w")==0&&f)std::fclose(f);
    Log("QoL native pause attempt build=" QOL_BUILD_ID);if(!Version(exe)){Log("unsupported executable; no pause script registered");return TRUE;}
    pauseoverlay::enabled=GetPrivateProfileIntA("CutscenePause","Overlay",1,iniPath)!=0;
    audioengine::Install();
    audioengine::InstallModernExports();
    directaudio::Install();
    Log("Legacy voice metadata processing removed; cutscene output volume only");
    menuvalue::Install();
    InitializeBundledRoadNav(ownModule);
    auto mod=GetModuleHandleA("ScriptHook.dll");using Register=void(__cdecl*)(ScriptThread*,HMODULE);auto reg=mod?reinterpret_cast<Register>(GetProcAddress(mod,"?RegisterThread@ScriptHookManager@@SAXPAVScriptThread@@PAUHINSTANCE__@@@Z")):nullptr;
    if(!reg){Log("ScriptHook registration unavailable");return TRUE;}
    auto script=new PauseScript;reg(script,ownModule);Log("native pause script registered; awaiting game session");return TRUE;
}
}
extern "C" __declspec(dllexport) void InitializeASI(){InitOnceExecuteOnce(&once,Initialize,nullptr,nullptr);}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID){if(reason==DLL_PROCESS_ATTACH){ownModule=instance;DisableThreadLibraryCalls(instance);}return TRUE;}
