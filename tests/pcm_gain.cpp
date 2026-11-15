#include "../src/pcm_gain.hpp"
#include <vector>
#include <cassert>
int main(){
    pcmgain::Format f{16,2,false};std::vector<unsigned char> input(16),output(16);
    const short values[]={1000,-1000,4000,-4000,12000,-12000,30000,-30000};std::memcpy(input.data(),values,16);
    auto gain=pcmgain::transform(input.data(),output.data(),16,f,2);
    assert(gain.valid&&gain.samples==8&&gain.clipped==2&&gain.outputRms>gain.inputRms);
    pcmgain::transform(input.data(),output.data(),16,f,1);assert(output==input); // bit-exact even after clipping
    for(unsigned scene=0;scene<3;++scene){pcmgain::transform(input.data(),output.data(),16,f,2);pcmgain::transform(input.data(),output.data(),16,f,1);assert(input==output);}
    assert(!pcmgain::transform(input.data(),output.data(),15,f,2).valid);
    for(unsigned bits:{8u,16u,24u,32u}){pcmgain::Format format{bits,1,false};std::vector<unsigned char> in(bits/8,0),out(bits/8);if(bits==8)in[0]=140;else in[bits/8-1]=0x10;
        const auto result=pcmgain::transform(in.data(),out.data(),bits/8,format,2);assert(result.valid&&std::fabs(result.outputPeak-result.inputPeak*2)<.00001);}
    float values32[]={.1f,-.1f,.2f,-.2f};unsigned char rendered[sizeof(values32)]{};pcmgain::Format floating{32,2,true};
    const auto result=pcmgain::transform(reinterpret_cast<unsigned char*>(values32),rendered,sizeof(values32),floating,2);assert(result.valid&&std::fabs(result.outputRms/result.inputRms-2)<.00001);
}
