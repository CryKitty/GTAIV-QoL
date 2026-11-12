#include "../src/range_trace.cpp"
#include <cassert>
int main() {
    std::vector<unsigned char> image(4096);
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(image.data());
    dos->e_magic = IMAGE_DOS_SIGNATURE; dos->e_lfanew = 128;
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(image.data()+128);
    nt->Signature = IMAGE_NT_SIGNATURE; nt->FileHeader.NumberOfSections = 1;
    nt->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER32);
    nt->OptionalHeader.SizeOfImage = 4096;
    auto section = IMAGE_FIRST_SECTION(nt); section->VirtualAddress = 1024;
    section->Misc.VirtualSize = 2048; section->Characteristics = IMAGE_SCN_MEM_EXECUTE;
    const unsigned char fixture[] = {0x83,0xEC,0x34,0xA1,0xB4,0x7F,0x05,0x01,0x33,0xC4,0x89,0x44,0x24,0x30,0x83,0x3D,0x7C,0x0B,0x03,0x01,0x01,0x0F,0x85,0x02,0x02,0x00,0x00,0x80,0x3D,0xF7,0x09,0x16,0x01,0x00,0x53,0x0F,0x85,0xD9,0x00,0x00,0x00,0x8D,0x44,0x24,0x04,0x6A,0x3F,0x50,0xE8,0xEB,0xBC,0xFF,0xFF,0xF3,0x0F,0x10,0x00,0xF3,0x0F,0x58,0x05,0x64,0x8C,0xFE,0x00,0x8D,0x44,0x24,0x14,0x6A,0x40,0x50,0xF3,0x0F,0x11,0x44,0x24,0x14,0xE8,0xCD,0xBC,0xFF,0xFF,0xF3,0x0F,0x10,0x0D,0xCC,0x09,0x16,0x01};
    auto getter = image.data()+1100;
    auto install = [&](unsigned offset) {
        auto code = image.data()+offset; memcpy(code, fixture, sizeof(fixture));
        auto rel1 = static_cast<std::int32_t>(getter-(code+53));
        auto rel2 = static_cast<std::int32_t>(getter-(code+83));
        auto field = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(image.data()+3500));
        auto flag = field+4;
        memcpy(code+49, &rel1, 4); memcpy(code+79, &rel2, 4);
        memcpy(code+87, &field, 4); memcpy(code+29, &flag, 4);
    };
    install(1300);
    assert(FindTraceTargets(image.data(), getter));
    assert(reinterpret_cast<unsigned char*>(trace.value) == image.data()+3500);
    assert(trace.flag == image.data()+3504);
    assert(trace.caller == image.data()+1348);
    install(1800);
    assert(!FindTraceTargets(image.data(), getter)); // ambiguity rejected
    memset(image.data()+1800, 0, sizeof(fixture));
    image[1300+83] = 0x90;
    assert(!FindTraceTargets(image.data(), getter)); // instruction signature mismatch
    return 0;
}
