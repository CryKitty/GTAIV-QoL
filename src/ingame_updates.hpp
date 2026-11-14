#pragma once
#include "update_menu.hpp"
#include "update_check_schedule.hpp"
namespace ingameupdates {
// The Windows process only queues a request on shared disk. The Deck user
// service owns Linux networking/staging and waits for every GTA process to exit.
inline bool Request(){
    wchar_t path[MAX_PATH]{};const auto n=GetModuleFileNameW(nullptr,path,MAX_PATH);
    if(!n||n>=MAX_PATH)return false;auto slash=wcsrchr(path,L'\\');if(!slash)return false;
    const auto remaining=MAX_PATH-static_cast<std::size_t>(slash+1-path);
    if(remaining<64)return false;
    wcscpy_s(slash+1,remaining,L"GTAIV-QoL-update-request.tmp");
    const auto flags=GetFileAttributesW(path);if(flags!=INVALID_FILE_ATTRIBUTES&&(flags&FILE_ATTRIBUTE_REPARSE_POINT))return false;
    HANDLE file=CreateFileW(path,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    FILETIME time{};GetSystemTimeAsFileTime(&time);char request[96]{};
    const auto length=sprintf_s(request,"%lu-%lu-%lu\n",GetCurrentProcessId(),time.dwHighDateTime,time.dwLowDateTime);
    DWORD written=0;const bool ok=length>0&&WriteFile(file,request,static_cast<DWORD>(length),&written,nullptr)&&written==static_cast<DWORD>(length);
    FlushFileBuffers(file);CloseHandle(file);if(!ok)return false;
    wchar_t target[MAX_PATH]{};wcscpy_s(target,path);auto end=wcsrchr(target,L'\\');wcscpy_s(end+1,MAX_PATH-static_cast<std::size_t>(end+1-target),L"GTAIV-QoL-update-request.txt");
    return MoveFileExW(path,target,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
}
inline Schedule schedule;
inline SRWLOCK timerLock=SRWLOCK_INIT;
inline bool stopped=false,initialized=false;
inline std::uint64_t lastRead=0;
inline bool PathFor(wchar_t (&path)[MAX_PATH],const wchar_t* name){
    const auto n=GetModuleFileNameW(nullptr,path,MAX_PATH);if(!n||n>=MAX_PATH)return false;
    auto end=wcsrchr(path,L'\\');if(!end)return false;
    const auto remaining=MAX_PATH-static_cast<std::size_t>(end+1-path);if(remaining<64)return false;
    wcscpy_s(end+1,remaining,name);return true;
}
inline void Clear(){
    for(const wchar_t* name:{L"GTAIV-QoL-update-request.txt",L"GTAIV-QoL-update-request.tmp"}){
        wchar_t path[MAX_PATH]{};if(PathFor(path,name))DeleteFileW(path);
    }
}
inline void Session(bool active){
    AcquireSRWLockExclusive(&timerLock);
    if(!active){stopped=true;schedule={};lastRead=0;Clear();}
    else if(stopped){stopped=false;schedule={};lastRead=0;}
    ReleaseSRWLockExclusive(&timerLock);
}
inline void Tick(bool enabled,std::uint64_t now=GetTickCount64()){
    AcquireSRWLockExclusive(&timerLock);
    if(!initialized){Clear();initialized=true;}
    if(stopped||!enabled){if(schedule.enabled)Clear();schedule={};lastRead=0;ReleaseSRWLockExclusive(&timerLock);return;}
    if((schedule.started&&now-schedule.attempted<checkIntervalMs)||(lastRead&&now-lastRead<1000)){
        ReleaseSRWLockExclusive(&timerLock);return;
    }
    lastRead=now;
    wchar_t request[MAX_PATH]{},receipt[MAX_PATH]{};
    const bool paths=PathFor(request,L"GTAIV-QoL-update-request.txt")&&PathFor(receipt,L"GTAIV-QoL-update-info.ini");
    // Read pending first: the worker commits Checking before consuming it.
    bool pending=paths&&GetFileAttributesW(request)!=INVALID_FILE_ATTRIBUTES;
    const auto state=paths?qolupdatemenu::readReceipt(receipt):qolupdatemenu::Receipt{};
    const bool busy=state.state=="checking"||state.state=="downloading"||state.state=="applying"||
        (paths&&!state.valid&&GetFileAttributesW(receipt)!=INVALID_FILE_ATTRIBUTES);
    // An unclaimed request already shown as failed is replaceable at the
    // normal interval. Never replace a request owned by an active worker.
    if(pending&&!busy&&qolupdatemenu::fileAge(request)>120){
        // A failed delete can mean the worker just consumed it. Keep blocking
        // this tick; read its busy/terminal receipt again before any retry.
        if(DeleteFileW(request))pending=false;
    }
    if(schedule.due(true,now,pending,busy,state.staged||state.state=="staged"))
        qolupdatemenu::requestFailed.store(!Request());
    ReleaseSRWLockExclusive(&timerLock);
}
}
