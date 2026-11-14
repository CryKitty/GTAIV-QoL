#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <cassert>
#include "../src/ingame_updates.hpp"
int main(){
    wchar_t path[MAX_PATH]{},receipt[MAX_PATH]{};
    assert(ingameupdates::PathFor(path,L"GTAIV-QoL-update-request.txt"));
    assert(ingameupdates::PathFor(receipt,L"GTAIV-QoL-update-info.ini"));DeleteFileW(receipt);
    const auto status=[&](const char* code,bool staged=false){
        FILE* file=nullptr;assert(_wfopen_s(&file,receipt,L"wb")==0);
        std::fprintf(file,"[Update]\nState=%s\nStaged=%d\n",code,staged?1:0);std::fclose(file);
    };
    const auto queued=[&]{return GetFileAttributesW(path)!=INVALID_FILE_ATTRIBUTES;};
    const auto sentinel=[&]{FILE* file=nullptr;assert(_wfopen_s(&file,path,L"wb")==0);std::fputs("sentinel",file);std::fclose(file);};
    const auto unchanged=[&]{FILE* file=nullptr;assert(_wfopen_s(&file,path,L"rb")==0);char data[128]{};assert(std::fread(data,1,sizeof(data)-1,file)>0);std::fclose(file);assert(std::strcmp(data,"sentinel")==0);};
    sentinel();ingameupdates::Tick(true,600000);unchanged();assert(!ingameupdates::schedule.started);
    ingameupdates::Session(false);assert(!queued()); // unloaded world cancels without a timer tick
    ingameupdates::Session(true); // exact same scheduler tests, but only after world readiness
    sentinel();ingameupdates::Tick(false,0);assert(!queued()); // previous-session cleanup
    ingameupdates::Tick(true,100);assert(queued());
    assert(qolupdatemenu::label(true).find(L"Checking")!=std::wstring::npos);
    sentinel();ingameupdates::Tick(true,600099);unchanged(); // no early check
    ingameupdates::Tick(true,600100);unchanged(); // pending request blocks duplicates
    status("up_to_date");DeleteFileW(path);
    ingameupdates::Tick(true,601100);assert(queued()); // due once idle; next interval from attempt
    status("checking");DeleteFileW(path);
    ingameupdates::Tick(true,1201100);assert(!queued()); // no queue in flight
    status("downloading");ingameupdates::Tick(true,1202100);assert(!queued());
    status("failed");ingameupdates::Tick(true,1203100);assert(queued());
    status("failed");DeleteFileW(path);
    ingameupdates::Tick(true,1803099);assert(!queued());
    ingameupdates::Tick(true,1803100);assert(queued()); // failure retries at ten minutes
    status("staged",true);DeleteFileW(path);
    ingameupdates::Tick(true,2403100);assert(!queued());
    status("failed",true);ingameupdates::Tick(true,2404100);assert(!queued()); // staged failed-apply receipt
    ingameupdates::Tick(false,2404200);assert(!queued()&&!ingameupdates::schedule.enabled);
    status("cancelled");ingameupdates::Tick(true,2404300);assert(queued()); // Off -> On unchanged
    ingameupdates::Session(false);assert(!queued());
    ingameupdates::Tick(true,9000000);assert(!queued()&&!ingameupdates::schedule.enabled); // script unload
    ingameupdates::Session(true);ingameupdates::Tick(true,9000100);assert(queued()); // new session startup
    HANDLE old=CreateFileW(path,FILE_WRITE_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    assert(old!=INVALID_HANDLE_VALUE);FILETIME expired{};assert(SetFileTime(old,nullptr,nullptr,&expired));CloseHandle(old);
    ingameupdates::Tick(true,9600100);assert(queued()&&qolupdatemenu::fileAge(path)<120); // unclaimed failed request retries once
    qolupdatemenu::requestFailed.store(true);assert(qolupdatemenu::label(true).find(L"Update failed")!=std::wstring::npos);
    qolupdatemenu::requestFailed.store(false);ingameupdates::Session(false);
    DeleteFileW(receipt);assert(!queued());std::puts("In-game ten-minute checks, busy/staged suppression, retry and session cleanup passed");
}
