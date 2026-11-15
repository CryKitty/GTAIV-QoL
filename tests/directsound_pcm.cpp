#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dsound.h>
#include <cassert>
#include <atomic>
#include <cstdio>
#include <cstring>
static bool Read(const void* p,void* out,SIZE_T size){SIZE_T n=0;return ReadProcessMemory(GetCurrentProcess(),p,out,size,&n)&&n==size;}
static bool Executable(const void* p){return p!=nullptr;}
static void Log(const char* text){std::puts(text);}
#include "../src/audio_engine_capture.hpp"
#include "../src/directsound_capture.hpp"
struct Fake {void** table;short data[16];bool playing=true;DWORD cursor=0;};
static HRESULT WINAPI Query(void*,REFIID,void** result){*result=nullptr;return E_NOINTERFACE;}
static ULONG WINAPI Add(void*){return 2;}static ULONG WINAPI Release(void*){return 1;}
static HRESULT WINAPI Caps(void*,DSBCAPS* caps){caps->dwFlags=DSBCAPS_GLOBALFOCUS;caps->dwBufferBytes=32;return DS_OK;}
static HRESULT WINAPI GetVolume(void*,LONG*){return DSERR_CONTROLUNAVAIL;}
static HRESULT WINAPI SetVolume(void*,LONG){return DSERR_CONTROLUNAVAIL;}
static HRESULT WINAPI Format(void*,WAVEFORMATEX* f,DWORD bytes,DWORD* needed){if(needed)*needed=sizeof(*f);if(!f||bytes<sizeof(*f))return E_FAIL;*f={};f->wFormatTag=WAVE_FORMAT_PCM;f->nChannels=1;f->nSamplesPerSec=44100;f->wBitsPerSample=16;f->nBlockAlign=2;return DS_OK;}
static HRESULT WINAPI Cursor(void* object,DWORD* play,DWORD* write){auto* f=static_cast<Fake*>(object);f->cursor=(f->cursor+4)%32;if(play)*play=f->cursor;if(write)*write=f->cursor;return DS_OK;}
static bool rejectLock=false;
static HRESULT WINAPI Lock(void* object,DWORD at,DWORD bytes,void** first,DWORD* one,void** second,DWORD* two,DWORD flags){auto* f=static_cast<Fake*>(object);if(rejectLock)return DSERR_BUFFERLOST;if(flags&DSBLOCK_ENTIREBUFFER){at=0;bytes=32;}if(at>=32||bytes>32)return E_FAIL;*first=reinterpret_cast<unsigned char*>(f->data)+at;*one=std::min(bytes,32-at);*second=*one<bytes?f->data:nullptr;*two=bytes-*one;return DS_OK;}
static HRESULT WINAPI Unlock(void*,void*,DWORD,void*,DWORD){return DS_OK;}
static HRESULT WINAPI Play(void*,DWORD,DWORD,DWORD){return DS_OK;}static HRESULT WINAPI Stop(void*){return DS_OK;}
static HRESULT WINAPI Status(void*,DWORD* state){*state=DSBSTATUS_PLAYING;return DS_OK;}
int main(){
    void* table[21]{};table[0]=reinterpret_cast<void*>(&Query);table[1]=reinterpret_cast<void*>(&Add);table[2]=reinterpret_cast<void*>(&Release);table[3]=reinterpret_cast<void*>(&Caps);table[4]=reinterpret_cast<void*>(&Cursor);table[5]=reinterpret_cast<void*>(&Format);table[6]=reinterpret_cast<void*>(&GetVolume);table[9]=reinterpret_cast<void*>(&Status);table[11]=reinterpret_cast<void*>(&Lock);table[12]=reinterpret_cast<void*>(&Play);table[15]=reinterpret_cast<void*>(&SetVolume);table[18]=reinterpret_cast<void*>(&Stop);table[19]=reinterpret_cast<void*>(&Unlock);
    Fake sound{table,{}};for(auto& sample:sound.data)sample=1000;
    directaudio::CaptureBuffer(&sound);assert(directaudio::pcm[0].ready);
    cutsceneoutput::Apply(2);auto proof=directaudio::ApplyVolume();assert(proof.pcmSources==1&&proof.verified==1&&proof.readHr==DSERR_CONTROLUNAVAIL);
    assert(sound.data[0]==2000);assert(directaudio::pcm[0].levels.outputRms>directaudio::pcm[0].levels.inputRms);
    for(unsigned frame=0;frame<100;++frame){void* first=nullptr;void* second=nullptr;DWORD one=0,two=0;
        assert(SUCCEEDED(directaudio::GuardLockPcm(&sound,0,32,&first,&one,&second,&two,DSBLOCK_ENTIREBUFFER)));
        assert(static_cast<short*>(first)[0]==(frame?3000:1000));for(unsigned i=0;i<16;++i)static_cast<short*>(first)[i]=3000;
        assert(SUCCEEDED(directaudio::GuardUnlockPcm(&sound,first,one,second,two)));assert(sound.data[0]==6000);}
    cutsceneoutput::Apply(1);directaudio::ApplyVolume();assert(sound.data[0]==3000);
    for(unsigned scene=0;scene<3;++scene){cutsceneoutput::Apply(16);directaudio::ApplyVolume();assert(sound.data[0]==32767);cutsceneoutput::Apply(1);directaudio::ApplyVolume();assert(sound.data[0]==3000);}
    cutsceneoutput::Apply(2);directaudio::ApplyVolume();assert(sound.data[0]==6000);
    rejectLock=true;cutsceneoutput::Apply(1);auto failed=directaudio::ApplyVolume();assert(failed.failed==1&&!directaudio::pcm[0].readback&&sound.data[0]==6000);
    rejectLock=false;directaudio::pcm[0].retried=GetTickCount()-500;auto retried=directaudio::ApplyVolume();assert(retried.verified==1&&sound.data[0]==3000);
    LONG volume=123;assert(directaudio::GuardGetVolume(&sound,&volume)==DSERR_CONTROLUNAVAIL&&volume==123);
    std::puts("PCM source: measured 2x without volume controls, streaming without stacking, exact restore after clipping and repeat scenes passed");
}
