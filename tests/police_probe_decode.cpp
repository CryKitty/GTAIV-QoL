#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cassert>
#include <cstdio>
#include <cstddef>
#include "../vendor/roadnav/src/police_search_probe.hpp"
unsigned hashCalls=0,nameCalls=0;bool hashAvailable=false;
void* __cdecl ByHash(unsigned hash){assert(hash==0x746edf);++hashCalls;return hashAvailable?reinterpret_cast<void*>(0x10000):nullptr;}
void* __cdecl ByName(const char* name){assert(std::strcmp(name,"WANTED_STARS_ARE_FLASHING")==0);++nameCalls;return reinterpret_cast<void*>(0x20000);}
int main(){
    assert(policeprobe::resolve(&ByHash,&ByName,0x746edf,"WANTED_STARS_ARE_FLASHING")==reinterpret_cast<void*>(0x20000));
    assert(hashCalls==1&&nameCalls==1);hashAvailable=true;
    assert(policeprobe::resolve(&ByHash,&ByName,0x746edf,"WANTED_STARS_ARE_FLASHING")==reinterpret_cast<void*>(0x10000));
    assert(hashCalls==2&&nameCalls==1);

    // Captured player lookup reached through STORE_WANTED_LEVEL.
    std::array<unsigned char,32> entry{{0x8b,0x44,0x24,0x04,0x83,0xf8,0x1f,0x77,0x08,0x8b,0x04,0x85,0x08,0x88,0x1a,0x01,0xc3}};
    std::uint32_t table=0;assert(policeprobe::playerPedTable(entry,table));assert(table==0x11a8808);
    entry[9]=0;assert(!policeprobe::playerPedTable(entry,table));
    std::array<unsigned char,64> code{};
    // MOV's immediate contains E8 but must not be mistaken for a CALL.
    const unsigned char bytes[]={0xb8,0xe8,0,0,0,0xe8,0x16,0,0,0,0xe8,0xe1,0xff,0xff,0xff,0xc3,0xe8,0,0,0,0};
    std::memcpy(code.data(),bytes,sizeof(bytes));auto refs=policeprobe::decode(code.data(),code.size(),0x1000);
    assert(refs.calls.size()==2&&refs.calls[0]==0x1020&&refs.calls[1]==0xff0);
    assert(policeprobe::decode(code.data(),14,0x1000).calls.empty());
    std::puts("Police capture decodes real CALL boundaries, signed targets and the captured CE table contract");
}
