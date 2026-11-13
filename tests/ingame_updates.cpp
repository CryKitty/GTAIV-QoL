#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <cassert>
#include "../src/ingame_updates.hpp"
int main(){
    wchar_t path[MAX_PATH]{};assert(GetModuleFileNameW(nullptr,path,MAX_PATH));auto end=wcsrchr(path,L'\\');assert(end);
    wcscpy_s(end+1,MAX_PATH-static_cast<std::size_t>(end+1-path),L"GTAIV-QoL-update-request.txt");DeleteFileW(path);
    ingameupdates::Tick(false);assert(GetFileAttributesW(path)==INVALID_FILE_ATTRIBUTES);
    ingameupdates::Tick(true);assert(GetFileAttributesW(path)!=INVALID_FILE_ATTRIBUTES);
    FILE* file=nullptr;assert(_wfopen_s(&file,path,L"wb")==0);std::fputs("sentinel",file);std::fclose(file);
    ingameupdates::Tick(true);assert(_wfopen_s(&file,path,L"rb")==0);char data[128]{};assert(std::fread(data,1,sizeof(data)-1,file)>0);std::fclose(file);assert(std::strcmp(data,"sentinel")==0);
    ingameupdates::Tick(false);ingameupdates::Tick(true);assert(_wfopen_s(&file,path,L"rb")==0);std::memset(data,0,sizeof(data));assert(std::fread(data,1,sizeof(data)-1,file)>0);std::fclose(file);assert(std::strcmp(data,"sentinel")!=0);
    // Request-overlay state is visible immediately, before the Linux poll.
    assert(qolupdatemenu::label(true).find(L"Checking")!=std::wstring::npos);
    qolupdatemenu::requestFailed.store(true);assert(qolupdatemenu::label(true).find(L"Update failed")!=std::wstring::npos);
    qolupdatemenu::requestFailed.store(false);
    DeleteFileW(path);std::puts("In-game request defaults Off, startup On and fresh toggle edge passed");
}
