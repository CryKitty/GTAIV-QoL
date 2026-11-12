#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cassert>
static bool Read(const void* p,void* out,SIZE_T size){SIZE_T n=0;return ReadProcessMemory(GetCurrentProcess(),p,out,size,&n)&&n==size;}
static bool Executable(const void*){return true;}
static void Log(const char*){}
#include "../src/cutscene_output.hpp"
static float level=.8f;static bool destroyed=false;
static HRESULT WINAPI Set(void*,float value,UINT32){level=value;return S_OK;}
static void WINAPI Get(void*,float* value){*value=level;}
static void WINAPI Destroy(void*){destroyed=true;}
int main(){
    void* table[19]{};table[12]=reinterpret_cast<void*>(&Set);table[13]=reinterpret_cast<void*>(&Get);table[18]=reinterpret_cast<void*>(&Destroy);
    struct Fake {void** table;} voice{table};
    cutsceneoutput::Capture(&voice,false);cutsceneoutput::Apply(.5f);assert(std::fabs(level-.4f)<.001f);
    cutsceneoutput::SetLevel(&voice,.6f,0);assert(std::fabs(level-.3f)<.001f);
    float requested=0;cutsceneoutput::GetLevel(&voice,&requested);assert(std::fabs(requested-.6f)<.001f);
    cutsceneoutput::Apply(1);assert(std::fabs(level-.6f)<.001f);
    cutsceneoutput::DestroyVoice(&voice);assert(destroyed);cutsceneoutput::Apply(.5f);assert(std::fabs(level-.6f)<.001f);
}
