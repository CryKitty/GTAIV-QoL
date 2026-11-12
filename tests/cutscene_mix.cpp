#include "../src/cutscene_mix.hpp"
#include <cassert>
int main(){
    assert(cutscenemix::gain(true,true,1600)==16);
    assert(cutscenemix::gain(false,true,800)==1);
    assert(cutscenemix::gain(true,false,0)==1);
    assert(cutscenemix::gain(true,true,50)==.5f);
    assert(cutscenemix::gain(true,true,0)==0);
    assert(cutscenemix::decibels(-1000,1)==-1000);
    assert(cutscenemix::decibels(-1000,0)==-10000);
    assert(cutscenemix::decibels(-1000,.5f)<-1500);
}
