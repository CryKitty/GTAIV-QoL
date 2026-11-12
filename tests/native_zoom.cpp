#include "../src/main.cpp"
#include <cassert>
int main() {
    assert(interactivezoom::SelectStop(4360) == 2);
    assert(interactivezoom::SelectStop(6540) == 0);
    interactivezoom::Gate gate;
    assert(!gate.Observe(false,5,0));
    assert(!gate.Observe(true,3,100)); // pause preview
    assert(!gate.Observe(true,5,200)); // interactive acceptance, let centering settle
    assert(!gate.Observe(true,5,250));
    assert(gate.Observe(true,5,300));
    assert(!gate.Observe(true,5,400)); // no forcing after manual zoom
    assert(!gate.Observe(true,3,500));
    assert(!gate.Observe(true,5,600));
    assert(gate.Observe(true,5,700)); // reopened
    std::vector<unsigned char> image(16384);
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(image.data()); dos->e_magic = IMAGE_DOS_SIGNATURE; dos->e_lfanew = 128;
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(image.data()+128);
    nt->FileHeader.NumberOfSections = 1; nt->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER32); nt->OptionalHeader.SizeOfImage = 16384;
    auto section = IMAGE_FIRST_SECTION(nt); section->VirtualAddress = 1024; section->Misc.VirtualSize = 8192; section->Characteristics = IMAGE_SCN_MEM_EXECUTE;
    const unsigned char fixture[] = {0xC7,0x44,0x24,0x1C,0x00,0x60,0xCC,0x45,0xC7,0x44,0x24,0x20,0x00,0x78,0xA4,0x45,0xC7,0x44,0x24,0x24,0x00,0x20,0x79,0x45,0xC7,0x44,0x24,0x28,0x00,0x50,0x29,0x45,0xC7,0x44,0x24,0x2C,0x00,0xE0,0x92,0x44,0xC7,0x44,0x24,0x30,0x00,0x80,0x22,0x44,0xE8,0x9D,0xEC,0x01,0x00,0x88,0x44,0x24,0x0B,0x84,0xC0,0x74,0x05,0xE8,0xE0,0xE8,0x01,0x00,0xE8,0x3B,0xDC,0x07,0x00,0x6A,0x01,0xB9,0x80,0x7A,0x17,0x01,0xE8,0x7F,0x5A,0x03,0x00,0xA1,0xD8,0x09,0x16,0x01,0x6A,0x01,0xF3,0x0F,0x10,0x44,0x84,0x20,0xF3,0x0F,0x11,0x05,0xD0,0x09,0x16,0x01,0xF3,0x0F,0x10,0x05,0xE8,0x88,0xFE,0x00,0xF3,0x0F,0x11,0x44,0x24,0x1C,0xE8,0xC7,0xD0,0x03,0x00,0x83,0xC4,0x04,0x80,0xB8,0x8D,0x32,0x00,0x00,0x00,0x74,0x0E,0xF3,0x0F,0x10,0x05,0x60,0x8A,0xFE,0x00,0xF3,0x0F,0x11,0x44,0x24,0x18,0xF3,0x0F,0x10,0x05,0xD0,0x09,0x16,0x01,0xF3,0x0F,0x10,0x0D,0xCC,0x09,0x16,0x01,0x0F,0x2F,0xC1,0x0F,0x86,0xA7,0x00,0x00,0x00,0x8D,0x44,0x24,0x0C,0x6A,0x41,0x50,0xE8,0xD8,0xF6,0xFF,0xFF,0xD9,0x7C,0x24,0x1E,0xF3,0x0F};
    auto install = [&](unsigned offset) {
        auto code = image.data()+offset; memcpy(code,fixture,sizeof(fixture));
        const auto current = static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(image.data()+14000));
        const DWORD index=current+12, target=current+4;
        memcpy(code+84,&index,4); memcpy(code+100,&target,4);
        trace.value = reinterpret_cast<float*>(image.data()+14000);
        acceptance.first = image.data()+14501;
    };
    install(1300); assert(FindNativeZoom(image.data()));
    assert(nativeZoom.table == image.data()+1300);
    assert(reinterpret_cast<unsigned char*>(nativeZoom.index) == image.data()+14012);
    assert(reinterpret_cast<unsigned char*>(nativeZoom.focus) == image.data()+14028);
    assert(nativeZoom.pauseActive == image.data()+14500);
    install(2000); assert(!FindNativeZoom(image.data()));
    auto page = static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    assert(page); memcpy(page+128,fixture,sizeof(fixture));
    DWORD protection = 0; assert(VirtualProtect(page,4096,PAGE_EXECUTE_READ,&protection));
    nativeZoom = {}; nativeZoom.table = page+128; nativeZoom.selected=2; nativeZoom.target=4360;
    assert(PatchZoomStop());
    float stops[6]{}; for(unsigned i=0;i<6;++i) memcpy(stops+i,page+128+i*8+4,4);
    assert(stops[0]==6540 && stops[1]==5263 && stops[2]==4360 && stops[3]==2709 && stops[4]==1175 && stops[5]==650);
    MEMORY_BASIC_INFORMATION info{}; VirtualQuery(page,&info,sizeof(info)); assert(info.Protect==PAGE_EXECUTE_READ);
    struct IndexState { DWORD canaryA,index,canaryB; } state{123,0,456};
    DWORD focus=3; unsigned char pause=1;
    nativeZoom.index=&state.index; nativeZoom.focus=&focus; nativeZoom.pauseActive=&pause;
    assert(!SelectOpeningStop() && state.index==0);
    focus=5; assert(SelectOpeningStop() && state.index==2);
    assert(state.canaryA==123 && state.canaryB==456);
    VirtualFree(page,0,MEM_RELEASE);
    return 0;
}
