#include "../src/runtime_zoom_experimental.cpp"
#include <cassert>
int main() {
    using openingzoom::Ranges;
    const Ranges stock{6540,6540}, preview{190,6540}, manual{3000,3000};
    openingzoom::EntryGate gate;
    assert(!gate.Observe(0, stock, 0));
    assert(!gate.Observe(1, stock, 100));
    assert(gate.Observe(1, stock, 120)); gate.Applied();
    assert(!gate.Observe(1, manual, 200));
    assert(!gate.Observe(1, stock, 300)); // returning manually to max cannot reapply
    assert(!gate.Observe(0, preview, 400));
    assert(!gate.Observe(1, preview, 500));
    assert(!gate.Observe(1, stock, 600));
    assert(gate.Observe(1, stock, 620)); gate.Applied();
    gate = {};
    assert(!gate.Observe(1, stock, 100));
    assert(!gate.Observe(1, manual, 120)); // user moved zoom first
    assert(!gate.Observe(1, stock, 140));
    gate = {};
    assert(!gate.Observe(1, preview, 100));
    assert(!gate.Observe(1, preview, 1700));
    assert(!gate.Observe(1, stock, 1720)); // entry window expired
    struct FakeState { float canary; Ranges ranges; float centerX,centerY; } state{99,stock,123,456};
    unsigned char flag = 0;
    trace.value = &state.ranges.current; companion = &state.ranges.companion; trace.flag = &flag;
    assert(!ApplyOpening(stock)); // preview must remain untouched
    flag = 1;
    assert(ApplyOpening(stock));
    assert(state.ranges.current == 4360 && state.ranges.companion == 4360);
    assert(state.canary == 99 && state.centerX == 123 && state.centerY == 456);
    assert(!ApplyOpening(stock)); // nonstock current state is not overwritten
    std::vector<unsigned char> image(4096);
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(image.data()); dos->e_magic = IMAGE_DOS_SIGNATURE; dos->e_lfanew = 128;
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(image.data()+128);
    nt->FileHeader.NumberOfSections = 1; nt->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER32); nt->OptionalHeader.SizeOfImage = 4096;
    auto section = IMAGE_FIRST_SECTION(nt); section->VirtualAddress = 1024; section->Misc.VirtualSize = 1024; section->Characteristics = IMAGE_SCN_MEM_EXECUTE;
    const unsigned char initializer[] = {0xC7,0x05,0,0,0,0,0x00,0x60,0xCC,0x45,0xC7,0x05,0,0,0,0,0x00,0x60,0xCC,0x45};
    auto install = [&](unsigned offset) {
        memcpy(image.data()+offset, initializer, sizeof(initializer));
        auto current = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(image.data()+3500));
        auto other = current+4;
        memcpy(image.data()+offset+2, &other, 4); memcpy(image.data()+offset+12, &current, 4);
        trace.value = reinterpret_cast<float*>(image.data()+3500);
    };
    install(1100);
    assert(FindCompanion(image.data()));
    assert(companion == reinterpret_cast<float*>(image.data()+3504));
    install(1200); assert(!FindCompanion(image.data()));
    return 0;
}
