#pragma once
#include "update_menu.hpp"
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
inline void Tick(bool enabled){
    static bool previous=false;
    if(enabled&&!previous)qolupdatemenu::requestFailed.store(!Request());
    previous=enabled;
}
}
