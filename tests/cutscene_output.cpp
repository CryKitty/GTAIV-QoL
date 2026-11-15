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
static float queued=0;static bool reject=false;
static HRESULT WINAPI Set(void*,float value,UINT32 operation){if(reject)return E_FAIL;if(operation)queued=value;else level=value;return S_OK;}
static void WINAPI Get(void*,float* value){*value=level;}
static void WINAPI Destroy(void*){destroyed=true;}
int main(){
    void* table[19]{};table[12]=reinterpret_cast<void*>(&Set);table[13]=reinterpret_cast<void*>(&Get);table[18]=reinterpret_cast<void*>(&Destroy);
    struct Fake {void** table;} voice{table};
    cutsceneoutput::Capture(&voice,false);
    // An unproven mastering property must not be amplified. PCM sources consume
    // the request; a successful property getter is not signal verification.
    for(float gain:{.5f,2.0f,15.8f,1.0f}){auto proof=cutsceneoutput::Apply(gain);assert(proof.verified==0&&std::fabs(level-.8f)<.001f);}
    cutsceneoutput::SetLevel(&voice,.6f,0);assert(std::fabs(level-.6f)<.001f);
    cutsceneoutput::Apply(2);assert(level==.6f&&cutsceneoutput::gain.load()==2);
    cutsceneoutput::SetLevel(&voice,.7f,1);assert(queued==.7f);cutsceneoutput::Apply(1);level=queued;cutsceneoutput::Apply(1);assert(level==.7f);
    cutsceneoutput::DestroyVoice(&voice);assert(destroyed);cutsceneoutput::Apply(2);assert(level==.7f);
}
