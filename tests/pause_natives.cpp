#include "../src/pause_natives.hpp"
#include <cassert>
#include <cstring>
int main(){
    using namespace cutscenepause;
    const std::uint32_t expected[]={0x6f32a4e2,0x0bf2cd82,0xe2c9c6f8,0xf9998582,0x5ded14b4,0x1c1cf453,0xd3600591,0xca56c61d,0x53f5b5ab};
    int sentinel=0;
    for(unsigned i=0;i<9;++i){
        unsigned calls=0;
        const auto hashLookup=[&](std::uint32_t hash)->void*{++calls;return hash==expected[i]?&sentinel:nullptr;};
        const auto noName=[](const char*)->void*{assert(false);return nullptr;};
        const auto valid=[&](const void* p){return p==&sentinel;};
        assert(Resolve(natives[i],hashLookup,noName,valid)==&sentinel&&calls==1);
        const auto missing=[](std::uint32_t)->void*{return nullptr;};
        const auto nameLookup=[&](const char* name)->void*{return std::strcmp(name,natives[i].name)==0?&sentinel:nullptr;};
        assert(Resolve(natives[i],missing,nameLookup,valid)==&sentinel);
        assert(Resolve(natives[i],missing,[](const char*)->void*{return nullptr;},valid)==nullptr);
    }
    return 0;
}
