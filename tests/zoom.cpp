#include "../src/map_zoom_experimental.cpp"
#include <cassert>
Vec2* __cdecl FakeGetter(Vec2* out, int) { *out = {800.0f, 0.0f}; return out; }
Vec2* __cdecl CustomGetter(Vec2* out, int) { *out = {900.0f, 0.0f}; return out; }
DWORD probeFlags = 0;
float probeXmm = 0.0f;
DWORD probeEcx = 0, probeEdx = 0;
__declspec(naked) Vec2* __cdecl RegisterProbe(Vec2*, int) {
    __asm {
        movss dword ptr [probeXmm], xmm0
        mov dword ptr [probeEcx], ecx
        mov dword ptr [probeEdx], edx
        pushfd
        pop dword ptr [probeFlags]
        mov eax, dword ptr [esp+4]
        ret
    }
}
void TestTransparentEntry() {
    Vec2 data{};
    float sentinel = 42.0f;
    original = RegisterProbe;
    // Exercise the actual assembly entry with volatile registers and flags.
    __asm {
        push 62
        lea eax, data
        push eax
        movss xmm0, sentinel
        mov ecx, 12345678h
        mov edx, 87654321h
        stc
        call GetWidgetEntry
        lea esp, [esp+8]
    }
    assert(probeXmm == 42.0f && (probeFlags & 1));
    assert(probeEcx == 0x12345678 && probeEdx == 0x87654321);
    // Exercise a real MinHook trampoline as well, not only a fake getter.
    assert(MH_Initialize() == MH_OK);
    assert(MH_CreateHook(reinterpret_cast<void*>(&RegisterProbe), reinterpret_cast<void*>(&GetWidgetEntry), reinterpret_cast<void**>(&original)) == MH_OK);
    assert(MH_EnableHook(reinterpret_cast<void*>(&RegisterProbe)) == MH_OK);
    passThroughOnly = 1;
    __asm {
        push 63
        lea eax, data
        push eax
        movss xmm0, sentinel
        mov ecx, 12345678h
        mov edx, 87654321h
        stc
        call RegisterProbe
        lea esp, [esp+8]
    }
    assert(probeXmm == 42.0f && (probeFlags & 1));
    assert(probeEcx == 0x12345678 && probeEdx == 0x87654321);
    assert(MH_DisableHook(reinterpret_cast<void*>(&RegisterProbe)) == MH_OK);
    assert(MH_RemoveHook(reinterpret_cast<void*>(&RegisterProbe)) == MH_OK);
    assert(MH_Uninitialize() == MH_OK);
    passThroughOnly = 0;
}
int main() {
    TestTransparentEntry();
    original = FakeGetter;
    Vec2 v{};
    assert(GetWidget(&v, 62) == &v && v.x == 800.0f);
    GetWidget(&v, 63); assert(v.x > 533.33f && v.x < 533.34f && v.y == 0.0f);
    GetWidget(&v, 63); assert(v.x > 533.33f && v.x < 533.34f); // no compounding
    GetWidget(&v, 64); assert(v.x == 800.0f); // maximum range unchanged
    original = CustomGetter;
    GetWidget(&v, 63); assert(v.x == 900.0f); // preserve custom data
    std::vector<std::uint8_t> image(4096);
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(image.data());
    dos->e_magic = IMAGE_DOS_SIGNATURE; dos->e_lfanew = 128;
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(image.data()+128);
    nt->Signature = IMAGE_NT_SIGNATURE; nt->FileHeader.Machine = IMAGE_FILE_MACHINE_I386;
    nt->FileHeader.NumberOfSections = 1; nt->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER32);
    nt->OptionalHeader.SizeOfImage = 4096;
    auto section = IMAGE_FIRST_SECTION(nt);
    section->VirtualAddress = 1024; section->Misc.VirtualSize = 1024;
    section->Characteristics = IMAGE_SCN_MEM_EXECUTE;
    const unsigned char sig[] = {0x8B,0x44,0x24,4,0x56,0x8B,0x35,1,2,3,4,0x85,0xF6};
    assert(!FindGetter(image.data()));
    memcpy(image.data()+1100, sig, sizeof(sig));
    assert(FindGetter(image.data()) == image.data()+1100);
    memcpy(image.data()+1200, sig, sizeof(sig));
    assert(!FindGetter(image.data())); // ambiguous -> no hook
    section->Characteristics = 0;
    assert(!FindGetter(image.data())); // data sections excluded
    // Loading as an ASI without a supported executable must leave it unmodified.
    logging = false;
    char path[MAX_PATH]; GetModuleFileNameA(nullptr, path, MAX_PATH);
    assert(!SupportedVersion(path));
    return 0;
}
