#include "../src/cutscene_mix.hpp"
#include <cassert>
int main(){
    using cutscenemix::active;
    assert(!active(true,0,true,true));
    for(unsigned state:{8u,9u}){assert(active(true,state,true,false));assert(!active(false,state,true,false));assert(!active(true,state,false,false));assert(!active(true,state,true,true));}
    for(unsigned state:{0u,1u,7u,10u,0xffffffffu})assert(!active(true,state,true,false));
    // Complete / skip / unload / failed state read all restore unity; a new
    // scene starts from unity instead of multiplying the last scene's gain.
    for(bool scene:{true,false,true,false})assert(cutscenemix::gain(scene,true,200)==(scene?2.0f:1.0f));
    assert(cutscenemix::gain(true,true,1600)==16);
    assert(cutscenemix::gain(false,true,800)==1);
    assert(cutscenemix::gain(true,false,0)==1);
    assert(cutscenemix::gain(true,true,50)==.5f);
    assert(cutscenemix::gain(true,true,0)==0);
    assert(cutscenemix::decibels(-1000,1)==-1000);
    assert(cutscenemix::decibels(-1000,0)==-10000);
    assert(cutscenemix::decibels(-1000,.5f)<-1500);
}
