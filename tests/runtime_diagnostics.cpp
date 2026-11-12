#include "../src/main.cpp"
#include <cassert>
int main() {
    const unsigned char bytes[]={0x83,0x3D,1,2,3,4,8};
    const diagnostics::Pattern p{bytes,"xx????x",7};
    unsigned char candidate[]={0x83,0x3D,9,8,7,6,8};
    assert(diagnostics::Matches(candidate,p));candidate[6]=7;
    assert(!diagnostics::Matches(candidate,p));
    assert(diagnostics::AudioWord("SCRIPTED_CONVERSATION"));
    assert(diagnostics::AudioWord("VoiceVolume"));
    assert(!diagnostics::AudioWord("texture"));
    std::vector<unsigned char> image(8192);
    auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(image.data());dos->e_lfanew=128;
    auto nt=reinterpret_cast<IMAGE_NT_HEADERS32*>(image.data()+128);
    nt->FileHeader.NumberOfSections=1;nt->FileHeader.SizeOfOptionalHeader=sizeof(IMAGE_OPTIONAL_HEADER32);
    auto section=IMAGE_FIRST_SECTION(nt);section->VirtualAddress=1024;section->Misc.VirtualSize=1024;section->Characteristics=IMAGE_SCN_MEM_EXECUTE;
    diagnostics::imageBase=reinterpret_cast<std::uintptr_t>(image.data());diagnostics::imageSize=8192;
    assert(!diagnostics::Unique("missing",p));
    memcpy(image.data()+1100,bytes,7);assert(diagnostics::Unique("unique",p)==image.data()+1100);
    memcpy(image.data()+1200,bytes,7);assert(!diagnostics::Unique("ambiguous",p));
    const auto ptr=static_cast<std::uint32_t>(diagnostics::imageBase+8000);memcpy(image.data()+1300,&ptr,4);
    diagnostics::targetCount=0;diagnostics::Add("fits",image.data()+1300,0,4);assert(diagnostics::targetCount==1);
    diagnostics::Add("overruns",image.data()+1300,0,256,true);assert(diagnostics::targetCount==1);
    section->Characteristics=IMAGE_SCN_MEM_READ;assert(!diagnostics::Unique("data excluded",p));
    return 0;
}
