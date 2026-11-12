#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cassert>
#include "../src/passive_crash_capture.hpp"
int main(){
    char root[MAX_PATH]{},folder[MAX_PATH]{};assert(GetTempPathA(MAX_PATH,root));assert(GetTempFileNameA(root,"qcc",0,folder));
    assert(DeleteFileA(folder));assert(CreateDirectoryA(folder,nullptr));strcat_s(folder,"\\");
    passivecrash::Install(folder);assert(passivecrash::path[0]);
    bool caught=false;ULONG_PTR access[2]={0,0x1234};
    __try {RaiseException(EXCEPTION_ACCESS_VIOLATION,0,2,access);}
    __except(EXCEPTION_EXECUTE_HANDLER){caught=true;}
    assert(caught); // The observer must not consume or recover the exception.
    FILE* file=nullptr;assert(fopen_s(&file,passivecrash::path,"rb")==0&&file);
    char capture[4096]{};const auto count=std::fread(capture,1,sizeof(capture)-1,file);std::fclose(file);
    assert(count>0&&std::strstr(capture,"QoL passive fault code=C0000005"));assert(std::strstr(capture,"target=00001234"));
    assert(DeleteFileA(passivecrash::path));folder[std::strlen(folder)-1]=0;assert(RemoveDirectoryA(folder));
    std::puts("Passive fault capture preserves exception delivery and writes uploaded diagnostics");
}
