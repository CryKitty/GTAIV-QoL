#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
namespace voicevolume {
struct Entry {std::string name;std::vector<unsigned char> bytes;bool cutscene=false;uint32_t offset=0;};
inline uint32_t U32(const unsigned char* p){uint32_t value=0;std::memcpy(&value,p,4);return value;}
inline uint32_t NameHash(const std::string& name){uint32_t h=0;for(unsigned char c:name){if(c>='A'&&c<='Z')c=static_cast<unsigned char>(c+32);h+=c;h+=h<<10;h^=h>>6;}h+=h<<3;h^=h>>11;h+=h<<15;return h;}
inline size_t HeaderEnd(const std::vector<unsigned char>& bytes){
    if(bytes.size()<15)return 0;
    constexpr unsigned widths[]={2,2,2,2,2,2,2,2,4,4,2,2,2,4,0,2,1,1,4,4,4,0,0,4};
    const auto flags=U32(bytes.data()+11);if(flags&0xff000000u)return 0;
    size_t end=15;for(unsigned bit=0;bit<24;++bit)if(flags&(1u<<bit)){if(!widths[bit])return 0;end+=widths[bit];}
    return end<=bytes.size()?end:0;
}
inline bool ParseDat(const std::vector<unsigned char>& data,std::vector<Entry>& output){
    if(data.size()<24)return false;
    const size_t size=U32(data.data()+4);
    if(size>data.size()-24)return false;
    const size_t end=8+size;
    const size_t table=U32(data.data()+end),strings=U32(data.data()+end+4);
    if(strings>131072||table<4+strings*4||table>data.size()-end-4)return false;
    size_t pos=end+4+table;
    if(pos>data.size()-8)return false;
    const size_t count=U32(data.data()+pos);pos+=8;
    if(count>131072)return false;
    std::vector<Entry> parsed;
    for(size_t i=0;i<count;++i){
        if(pos>=data.size())return false;
        const size_t length=data[pos];
        if(!length||length>data.size()-pos-1||data.size()-pos-1-length<8)return false;
        std::string name(reinterpret_cast<const char*>(data.data()+pos+1),length);
        const size_t offset=U32(data.data()+pos+1+length),bytes=U32(data.data()+pos+5+length);
        if(offset>size||bytes>size-offset)return false;
        if(bytes>=15&&bytes<=512){
            const auto body=data.data()+8+offset;
            const bool centre=body[0]==12&&name.compare(0,10,"CUTSCENES_")==0&&
                name.size()>7&&name.compare(name.size()-7,7,"_CENTRE")==0;
            if(centre||body[0]==4){
                const auto flags=U32(body+11);
                constexpr unsigned widths[]={2,2,2,2,2,2,2,2,4,4,2,2,2,4};
                size_t required=15;for(unsigned bit=0;bit<14;++bit)if(flags&(1u<<bit))required+=widths[bit];
                if(required<=bytes)parsed.push_back({name,{body,body+bytes},centre,static_cast<uint32_t>(offset)});
            }
        }
        pos+=9+length;
    }
    if(parsed.size()>4096||output.size()+parsed.size()>4096)return false;
    output.insert(output.end(),parsed.begin(),parsed.end());return true;
}
inline std::vector<unsigned char> Gain(const Entry& entry,int percent){
    if(percent<0||percent>800||entry.bytes.size()<15)return {};
    auto result=entry.bytes;
    const auto flags=U32(result.data()+11);
    int16_t base=0;
    if(flags&1){if(result.size()<17)return {};std::memcpy(&base,result.data()+15,2);}
    else{result.insert(result.begin()+15,2,0);const uint32_t changed=flags|1u;std::memcpy(result.data()+11,&changed,4);}
    const int delta=percent?static_cast<int>(std::lround(2000.0*std::log10(percent/100.0))):-10000;
    const int16_t value=static_cast<int16_t>(percent?std::clamp(static_cast<int>(base)+delta,-10000,10000):-10000);
    std::memcpy(result.data()+15,&value,2);return result;
}
}
