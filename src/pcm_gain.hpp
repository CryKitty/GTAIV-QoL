#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
namespace pcmgain {
struct Format {unsigned bits=0,channels=0;bool floating=false;
    unsigned width()const{return bits/8;}
    bool valid()const{return channels>0&&channels<=8&&(floating?bits==32:(bits==8||bits==16||bits==24||bits==32));}
};
struct Levels {double inputPeak=0,outputPeak=0,inputRms=0,outputRms=0;unsigned samples=0,clipped=0;bool valid=false;};
inline double sample(const unsigned char* p,Format f){
    if(f.floating){float value=0;std::memcpy(&value,p,4);return std::isfinite(value)?value:0;}
    if(f.bits==8)return (static_cast<int>(*p)-128)/128.0;
    std::uint32_t raw=0;for(unsigned i=0;i<f.width();++i)raw|=std::uint32_t(p[i])<<(8*i);
    const unsigned shift=32-f.bits;const auto signedValue=static_cast<std::int32_t>(raw<<shift)>>shift;
    return signedValue/std::ldexp(1.0,static_cast<int>(f.bits-1));
}
inline Levels transform(const unsigned char* source,unsigned char* output,unsigned bytes,Format f,float gain){
    Levels level;if(!source||!f.valid()||bytes%(f.width()*f.channels)||!std::isfinite(gain)||gain<0||gain>16)return level;
    level.valid=true;
    if(output&&gain==1)std::memcpy(output,source,bytes); // Restore exact original bits, including clipped samples.
    double before=0,after=0;
    for(unsigned at=0;at<bytes;at+=f.width()){
        const double input=sample(source+at,f);const double requested=input*gain;
        const double result=std::clamp(requested,-1.0,1.0);if(requested!=result)++level.clipped;
        if(output&&gain!=1){
            if(f.floating){const float value=static_cast<float>(result);std::memcpy(output+at,&value,4);}
            else {const double scale=std::ldexp(1.0,static_cast<int>(f.bits-1));
                const auto value=static_cast<std::int64_t>(std::clamp(std::round(result*scale),-scale,scale-1));
                const auto raw=f.bits==8?static_cast<std::uint32_t>(value+128):static_cast<std::uint32_t>(value);
                for(unsigned i=0;i<f.width();++i)output[at+i]=static_cast<unsigned char>(raw>>(8*i));
            }
        }
        const double observed=output?sample(output+at,f):input;
        level.inputPeak=std::max(level.inputPeak,std::fabs(input));level.outputPeak=std::max(level.outputPeak,std::fabs(observed));
        before+=input*input;after+=observed*observed;++level.samples;
    }
    if(level.samples){level.inputRms=std::sqrt(before/level.samples);level.outputRms=std::sqrt(after/level.samples);}return level;
}
}
