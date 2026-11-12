#include "../src/voice_metadata.hpp"
#include <cassert>
using Bytes=std::vector<unsigned char>;
void Put(Bytes& bytes,uint32_t value){for(unsigned i=0;i<4;++i)bytes.push_back(static_cast<unsigned char>(value>>(8*i)));}
Bytes Dat(const std::vector<std::pair<std::string,Bytes>>& items){
    Bytes body,index;for(const auto& item:items){index.push_back(static_cast<unsigned char>(item.first.size()));index.insert(index.end(),item.first.begin(),item.first.end());Put(index,static_cast<uint32_t>(body.size()));Put(index,static_cast<uint32_t>(item.second.size()));body.insert(body.end(),item.second.begin(),item.second.end());}
    Bytes result;Put(result,15);Put(result,static_cast<uint32_t>(body.size()));result.insert(result.end(),body.begin(),body.end());Put(result,4);Put(result,0);Put(result,static_cast<uint32_t>(items.size()));Put(result,0);result.insert(result.end(),index.begin(),index.end());return result;
}
int16_t Volume(const Bytes& bytes){int16_t value=0;std::memcpy(&value,bytes.data()+15,2);return value;}
int main(){
    // Real centre-track header shape captured by the v55 read-only inventory.
    const Bytes centre={0x0c,0x76,0xef,1,0,0xaa,0xaa,2,0,0,0,0,0,1,0,0x20,0,0,0,0,0x06,0xe7,0x7b,0x57,0xff,0x38,0xc6,0x30};
    Bytes speech(17);speech[0]=4;speech[11]=1;const int16_t baseline=-700;std::memcpy(speech.data()+15,&baseline,2);
    const auto raw=Dat({{"CUTSCENES_FAU1_B_FAU1_B_CENTRE",centre},{"CUTSCENES_FAU1_B_FAU1_B_LEFT",centre},{"SPEECH_TEMPLATE",speech}});
    std::vector<voicevolume::Entry> entries;assert(voicevolume::ParseDat(raw,entries));assert(entries.size()==2);assert(entries[0].cutscene&&!entries[1].cutscene);assert(entries[0].offset==0&&entries[1].offset==centre.size()*2);assert(voicevolume::HeaderEnd(centre)==16);assert(voicevolume::NameHash("CUTSCENES_TEST_CENTRE")==voicevolume::NameHash("cutscenes_test_centre"));
    const auto boosted=voicevolume::Gain(entries[0],150);assert(boosted.size()==centre.size()+2);assert(Volume(boosted)==352);assert((voicevolume::U32(boosted.data()+11)&1)==1);assert(std::equal(centre.begin()+15,centre.end(),boosted.begin()+17));
    assert(Volume(voicevolume::Gain(entries[1],200))==-98);assert(Volume(voicevolume::Gain(entries[1],400))==504);assert(Volume(voicevolume::Gain(entries[1],800))==1106);assert(Volume(voicevolume::Gain(entries[1],0))==-10000);assert(voicevolume::Gain(entries[0],801).empty());assert(entries[0].bytes==centre);
    for(size_t n=0;n<raw.size();++n){const Bytes truncated(raw.begin(),raw.begin()+n);std::vector<voicevolume::Entry> output;assert(!voicevolume::ParseDat(truncated,output));assert(output.empty());}
    auto invalid=raw;invalid[4]=255;std::vector<voicevolume::Entry> output;assert(!voicevolume::ParseDat(invalid,output));
    return 0;
}
