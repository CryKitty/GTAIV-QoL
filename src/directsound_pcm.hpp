#pragma once
// Included inside directaudio after its buffer/interface registry declarations.
using LockPcm=HRESULT(WINAPI*)(void*,DWORD,DWORD,void**,DWORD*,void**,DWORD*,DWORD);
using UnlockPcm=HRESULT(WINAPI*)(void*,void*,DWORD,void*,DWORD);
using FormatPcm=HRESULT(WINAPI*)(void*,WAVEFORMATEX*,DWORD,DWORD*);
using CursorPcm=HRESULT(WINAPI*)(void*,DWORD*,DWORD*);
struct PcmState {
    pcmgain::Format format{};std::vector<unsigned char> original,scratch,observed;
    std::uintptr_t storage=0;void* first=nullptr;void* second=nullptr;DWORD firstBytes=0,secondBytes=0,offset=0,lastCursor=0;
    bool ready=false,locked=false,readback=false,advanced=false;float applied=1,requested=1;
    unsigned writes=0;HRESULT formatHr=E_PENDING,lastLock=E_PENDING,lastUnlock=E_PENDING;pcmgain::Levels levels{};DWORD logged=0,retried=0;
};
PcmState pcm[128]{};unsigned pcmBytes=0;
inline bool pcmCopy(void* destination,const void* source,DWORD bytes){SIZE_T done=0;return !bytes||(WriteProcessMemory(GetCurrentProcess(),destination,source,bytes,&done)&&done==bytes);}
inline bool pcmRead(PcmState& p,void* first,DWORD one,void* second,DWORD two,DWORD offset,bool capture){
    const auto size=static_cast<DWORD>(p.original.size());if(offset>=size||one>size-offset||two>offset||one+two>size)return false;
    const auto block=[&](void* address,DWORD bytes,DWORD at){
        if(!bytes)return true;
        if(capture&&!Read(address,p.original.data()+at,bytes))return false;
        const auto levels=pcmgain::transform(p.original.data()+at,p.scratch.data()+at,bytes,p.format,cutsceneoutput::gain.load());
        if(!levels.valid||!pcmCopy(address,p.scratch.data()+at,bytes)||!Read(address,p.observed.data()+at,bytes))return false;
        return std::memcmp(p.scratch.data()+at,p.observed.data()+at,bytes)==0;
    };
    const bool ok=block(first,one,offset)&&block(second,two,0);
    p.readback=ok;if(ok&&one+two==size)p.applied=cutsceneoutput::gain.load();
    if(ok){p.levels=pcmgain::transform(p.original.data(),nullptr,size,p.format,1);
        auto after=pcmgain::transform(p.observed.data(),nullptr,size,p.format,1);
        p.levels.outputPeak=after.inputPeak;p.levels.outputRms=after.inputRms;
        const auto expected=pcmgain::transform(p.original.data(),p.scratch.data(),size,p.format,p.applied);p.levels.clipped=expected.clipped;}
    return ok;
}
inline bool initPcm(Buffer& buffer,PcmState& p,DWORD bytes){
    if(!bytes||bytes>1024*1024||pcmBytes+bytes>16*1024*1024)return false;
    auto& v=buffer.views[0];WAVEFORMATEXTENSIBLE extended{};auto& format=extended.Format;DWORD needed=0;
    p.formatHr=reinterpret_cast<FormatPcm>(v.getFormat)(v.object,&format,sizeof(extended),&needed);if(FAILED(p.formatHr))return false;
    bool floating=format.wFormatTag==WAVE_FORMAT_IEEE_FLOAT;
    bool supported=format.wFormatTag==WAVE_FORMAT_PCM||floating;
    if(format.wFormatTag==WAVE_FORMAT_EXTENSIBLE&&format.cbSize>=22&&extended.Samples.wValidBitsPerSample==format.wBitsPerSample){
        GUID type={1,0,0x10,{0x80,0,0,0xaa,0,0x38,0x9b,0x71}};
        supported=std::memcmp(&type,&extended.SubFormat,sizeof(type))==0;type.Data1=3;
        floating=std::memcmp(&type,&extended.SubFormat,sizeof(type))==0;supported=supported||floating;
    }
    p.format={format.wBitsPerSample,format.nChannels,floating};
    if(!supported||!p.format.valid()||format.nBlockAlign!=p.format.width()*p.format.channels||bytes%format.nBlockAlign)return false;
    try {p.original.resize(bytes);p.scratch.resize(bytes);p.observed.resize(bytes);}
    catch(const std::bad_alloc&){std::vector<unsigned char>().swap(p.original);std::vector<unsigned char>().swap(p.scratch);std::vector<unsigned char>().swap(p.observed);return false;}
    void* first=nullptr;void* second=nullptr;DWORD one=0,two=0;
    p.lastLock=reinterpret_cast<LockPcm>(v.lockPcm)(v.object,0,bytes,&first,&one,&second,&two,DSBLOCK_ENTIREBUFFER);
    if(FAILED(p.lastLock))return false;
    const bool copied=one+two==bytes&&Read(first,p.original.data(),one)&&(!two||Read(second,p.original.data()+one,two));
    p.lastUnlock=reinterpret_cast<UnlockPcm>(v.unlockPcm)(v.object,first,one,second,two);
    if(!copied||FAILED(p.lastUnlock))return false;
    p.storage=one==bytes?reinterpret_cast<std::uintptr_t>(first):0;
    p.observed=p.original;p.scratch=p.original;p.ready=true;pcmBytes+=bytes;
    return true;
}
HRESULT WINAPI GuardLockPcm(void* object,DWORD offset,DWORD bytes,void** first,DWORD* one,void** second,DWORD* two,DWORD flags){
    AcquireSRWLockExclusive(&operation);Interface* v=nullptr;auto* b=Find(object,&v);HRESULT result=E_FAIL;
    if(b){auto& p=pcm[b-buffers];DWORD play=0,write=0;
        if(flags&DSBLOCK_FROMWRITECURSOR)reinterpret_cast<CursorPcm>(v->getCursor)(object,&play,&write);
        result=reinterpret_cast<LockPcm>(v->lockPcm)(object,offset,bytes,first,one,second,two,flags);p.lastLock=result;
        if(SUCCEEDED(result)&&p.ready&&first&&one){
            p.offset=(flags&DSBLOCK_ENTIREBUFFER)?0:offset;
            if(flags&DSBLOCK_FROMWRITECURSOR){const auto address=reinterpret_cast<std::uintptr_t>(*first);
                p.offset=p.storage&&address>=p.storage&&address-p.storage<p.original.size()?static_cast<DWORD>(address-p.storage):~0u;
            }
            p.first=*first;p.second=second?*second:nullptr;p.firstBytes=*one;p.secondBytes=two?*two:0;
            const auto size=static_cast<DWORD>(p.original.size());
            p.locked=p.offset<size&&p.firstBytes<=size-p.offset&&p.secondBytes<=p.offset;
            if(p.locked){pcmCopy(p.first,p.original.data()+p.offset,p.firstBytes);pcmCopy(p.second,p.original.data(),p.secondBytes);}
        }
    }
    ReleaseSRWLockExclusive(&operation);return result;
}
HRESULT WINAPI GuardUnlockPcm(void* object,void* first,DWORD one,void* second,DWORD two){
    AcquireSRWLockExclusive(&operation);Interface* v=nullptr;auto* b=Find(object,&v);HRESULT result=E_FAIL;
    if(b){auto& p=pcm[b-buffers];
        if(p.ready&&p.locked&&first==p.first&&second==p.second&&one==p.firstBytes&&two==p.secondBytes){
            pcmRead(p,first,one,second,two,p.offset,true);++p.writes;
        }
        result=reinterpret_cast<UnlockPcm>(v->unlockPcm)(object,first,one,second,two);p.lastUnlock=result;p.locked=false;
        if(FAILED(result))p.readback=false;
    }
    ReleaseSRWLockExclusive(&operation);return result;
}
inline bool reconcilePcm(Buffer& b,PcmState& p,float gain){
    if(!p.ready||p.locked)return false;
    auto& v=b.views[0];
    const auto now=GetTickCount();const bool changed=p.requested!=gain;if(changed)p.logged=0;p.requested=gain;
    if((p.applied!=gain||!p.readback)&&(changed||now-p.retried>=500)){p.retried=now;void* first=nullptr;void* second=nullptr;DWORD one=0,two=0;
        p.lastLock=reinterpret_cast<LockPcm>(v.lockPcm)(v.object,0,static_cast<DWORD>(p.original.size()),&first,&one,&second,&two,DSBLOCK_ENTIREBUFFER);
        if(SUCCEEDED(p.lastLock)){
            p.readback=pcmRead(p,first,one,second,two,0,false);
            p.lastUnlock=reinterpret_cast<UnlockPcm>(v.unlockPcm)(v.object,first,one,second,two);
            if(FAILED(p.lastUnlock))p.readback=false;
        }else p.readback=false;
    }
    DWORD play=0,write=0,status=0;const auto cursor=reinterpret_cast<CursorPcm>(v.getCursor)(v.object,&play,&write);
    const auto playing=v.status(v.object,&status);if(SUCCEEDED(cursor)&&play!=p.lastCursor)p.advanced=true;p.lastCursor=play;
    if(now-p.logged>=5000){p.logged=now;char line[480];sprintf_s(line,"Cutscene PCM source: id=%u multiplier=%.2f percent=%.0f playing=%u cursor-advanced=%u source-writes=%u input-peak=%.6f output-peak=%.6f input-rms=%.6f output-rms=%.6f clipped=%u pcm-readback=%u restored=%u lock-hr=%08lX unlock-hr=%08lX",static_cast<unsigned>(&b-buffers),gain,gain*100,SUCCEEDED(playing)&&(status&DSBSTATUS_PLAYING)?1u:0u,p.advanced?1u:0u,p.writes,p.levels.inputPeak,p.levels.outputPeak,p.levels.inputRms,p.levels.outputRms,p.levels.clipped,p.readback?1u:0u,gain==1&&p.readback?1u:0u,static_cast<unsigned long>(p.lastLock),static_cast<unsigned long>(p.lastUnlock));Log(line);p.advanced=false;}
    return p.readback;
}
