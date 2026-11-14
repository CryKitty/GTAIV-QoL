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
    cutsceneoutput::Capture(&voice,false);cutsceneoutput::Apply(.5f);assert(std::fabs(level-.4f)<.001f);
    cutsceneoutput::SetLevel(&voice,.6f,0);assert(std::fabs(level-.3f)<.001f);
    float requested=0;cutsceneoutput::GetLevel(&voice,&requested);assert(std::fabs(requested-.6f)<.001f);
    cutsceneoutput::Apply(1);assert(std::fabs(level-.6f)<.001f);
    // Full positive gain, per-frame game overwrites via a cached original
    // setter, failed writes, deferred operations and exact non-stacking restore.
    cutsceneoutput::Apply(2);assert(std::fabs(level-1.2f)<.001f);
    for(unsigned frame=0;frame<100;++frame){level=.7f;const auto proof=cutsceneoutput::Apply(2);assert(proof.verified==1&&proof.rebased==1&&std::fabs(level-1.4f)<.001f);}
    cutsceneoutput::Apply(1);assert(std::fabs(level-.7f)<.001f);
    for(unsigned scene=0;scene<3;++scene){cutsceneoutput::Apply(4);assert(std::fabs(level-2.8f)<.001f);cutsceneoutput::Apply(1);assert(std::fabs(level-.7f)<.001f);}
    cutsceneoutput::Apply(2);cutsceneoutput::SetLevel(&voice,.6f,1);
    assert(std::fabs(queued-.6f)<.001f); // Deferred queue must not retain a boosted level.
    cutsceneoutput::Apply(1);level=queued;cutsceneoutput::Apply(1);assert(std::fabs(level-.6f)<.001f);
    reject=true;auto failed=cutsceneoutput::Apply(2);assert(failed.failed==1&&failed.verified==0&&level==.6f);
    reject=false;cutsceneoutput::Apply(2);assert(std::fabs(level-1.2f)<.001f);
    cutsceneoutput::Apply(1);assert(std::fabs(level-.6f)<.001f);
    cutsceneoutput::DestroyVoice(&voice);assert(destroyed);cutsceneoutput::Apply(.5f);assert(std::fabs(level-.6f)<.001f);
}
