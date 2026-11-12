#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winver.h>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <cstring>
#include "map_zoom_policy.hpp"
#include "opening_zoom.hpp"
#include "interactive_zoom.hpp"
#include "qol_settings.hpp"
static_assert(sizeof(void*) == 4, "GTA IV requires x86");
namespace {
char logPath[MAX_PATH]{};
bool logging = false;
INIT_ONCE once = INIT_ONCE_STATIC_INIT;
void Log(const char* text) {
    if (!logging) return;
    FILE* f = nullptr;
    if (fopen_s(&f, logPath, "a") == 0 && f) { std::fprintf(f, "%s\n", text); std::fclose(f); }
}
// Scan only executable PE sections, requiring exactly one known getter prologue.
std::uint8_t* FindGetter(std::uint8_t* base) {
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 4096) return nullptr;
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386) return nullptr;
    const unsigned char a[] = {0x8B,0x44,0x24,0,0x56,0x8B,0x35,0,0,0,0,0x85,0xF6};
    const unsigned char b[] = {0x8B,0x0D,0,0,0,0,0x85,0xC9,0x8B,0x44,0x24,0,0x74};
    const char* masks[] = {"xxx?xxx????xx", "xx????xxxxx?x"};
    const unsigned char* patterns[] = {a,b};
    std::uint8_t* found = nullptr;
    unsigned count = 0;
    auto section = IMAGE_FIRST_SECTION(nt);
    for (unsigned s = 0; s < nt->FileHeader.NumberOfSections; ++s) {
        if (!(section[s].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        const auto start = section[s].VirtualAddress;
        const auto size = section[s].Misc.VirtualSize;
        if (start >= nt->OptionalHeader.SizeOfImage || size > nt->OptionalHeader.SizeOfImage - start) return nullptr;
        for (std::size_t i = 0; i + sizeof(a) <= size; ++i) {
            for (unsigned p = 0; p < 2; ++p) {
                bool match = true;
                for (unsigned j = 0; j < sizeof(a); ++j)
                    if (masks[p][j] != '?' && base[start+i+j] != patterns[p][j]) { match = false; break; }
                if (match) { found = base + start + i; ++count; }
            }
        }
    }
    return count == 1 ? found : nullptr;
}
bool SupportedVersion(const char* exe) {
    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeA(exe, &ignored);
    if (!size) return false;
    std::vector<unsigned char> data(size);
    if (!GetFileVersionInfoA(exe, 0, size, data.data())) return false;
    VS_FIXEDFILEINFO* info = nullptr;
    UINT length = 0;
    if (!VerQueryValueA(data.data(), "\\", reinterpret_cast<void**>(&info), &length) || length < sizeof(*info)) return false;
    return info->dwSignature == 0xFEEF04BD && info->dwFileVersionMS == MAKELONG(2,1) && info->dwFileVersionLS == MAKELONG(59,0);
}


bool Read(const void* address, void* out, SIZE_T length) {
    SIZE_T copied = 0;
    return ReadProcessMemory(GetCurrentProcess(), address, out, length, &copied) && copied == length;
}
struct TraceTargets { float* value; unsigned char* flag; std::uint8_t* caller; } trace{};
bool FindTraceTargets(std::uint8_t* base, std::uint8_t* getter) {
    const auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    const auto nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
    auto section = IMAGE_FIRST_SECTION(nt);
    unsigned matches = 0;
    const unsigned char pattern[] = {
        0xE8,0,0,0,0,0xF3,0x0F,0x10,0x00,0xF3,0x0F,0x58,0x05,0,0,0,0,
        0x8D,0x44,0x24,0x14,0x6A,0x40,0x50,0xF3,0x0F,0x11,0x44,0x24,0x14,
        0xE8,0,0,0,0,0xF3,0x0F,0x10,0x0D,0,0,0,0};
    for (unsigned s = 0; s < nt->FileHeader.NumberOfSections; ++s) {
        if (!(section[s].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        auto start = section[s].VirtualAddress, size = section[s].Misc.VirtualSize;
        if (start >= nt->OptionalHeader.SizeOfImage || size > nt->OptionalHeader.SizeOfImage-start) continue;
        std::vector<unsigned char> code(size);
        if (!Read(base+start, code.data(), size)) continue;
        for (std::size_t i = 48; i+sizeof(pattern) < size; ++i) {
            if (code[i-3] != 0x6A || code[i-2] != 63 || code[i-1] != 0x50) continue;
            bool match = true;
            for (unsigned j = 0; j < sizeof(pattern); ++j) {
                if ((j >= 1 && j <= 4) || (j >= 13 && j <= 16) ||
                    (j >= 31 && j <= 34) || j >= 39) continue;
                if (code[i+j] != pattern[j]) { match = false; break; }
            }
            if (!match || code[i-21] != 0x80 || code[i-20] != 0x3D || code[i-15] != 0 || code[i-14] != 0x53) continue;
            std::int32_t relative = 0, second = 0;
            memcpy(&relative, code.data()+i+1, 4); memcpy(&second, code.data()+i+31, 4);
            auto call = reinterpret_cast<std::uintptr_t>(base)+start+i;
            if (call+5+static_cast<std::uintptr_t>(relative) != reinterpret_cast<std::uintptr_t>(getter) ||
                call+35+static_cast<std::uintptr_t>(second) != reinterpret_cast<std::uintptr_t>(getter)) continue;
            std::uint32_t field = 0, flag = 0;
            memcpy(&field, code.data()+i+39, 4); memcpy(&flag, code.data()+i-19, 4);
            const auto image = reinterpret_cast<std::uintptr_t>(base);
            if (field < image || field-image > nt->OptionalHeader.SizeOfImage-4 ||
                flag < image || flag-image >= nt->OptionalHeader.SizeOfImage) continue;
            trace = {reinterpret_cast<float*>(field), reinterpret_cast<unsigned char*>(flag), base+start+i};
            ++matches;
        }
    }
    return matches == 1;
}

struct Acceptance { unsigned char* first; unsigned char* second; DWORD* page; std::uint8_t* prepare; std::uint8_t* site; } acceptance{};
bool FindAcceptance(std::uint8_t* base) {
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
    auto section = IMAGE_FIRST_SECTION(nt);
    const auto image = reinterpret_cast<std::uintptr_t>(base);
    const auto length = nt->OptionalHeader.SizeOfImage;
    unsigned matches = 0;
    for (unsigned s = 0; s < nt->FileHeader.NumberOfSections; ++s) {
        if (!(section[s].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        auto start = section[s].VirtualAddress, size = section[s].Misc.VirtualSize;
        if (start >= length || size > length-start) continue;
        std::vector<unsigned char> bytes(size);
        if (!Read(base+start, bytes.data(), size)) continue;
        for (std::size_t i = 48; i+80 < size; ++i) {
            if (bytes[i-5] != 0xA2 || bytes[i] != 0xC6 || bytes[i+1] != 0x05 || bytes[i+6] != 1 ||
                bytes[i+7] != 0xA2 || bytes[i+12] != 0xE8 || bytes[i+17] != 0xC7 || bytes[i+18] != 0x05 ||
                bytes[i+23] != 0x32 || bytes[i+24] || bytes[i+25] || bytes[i+26] ||
                bytes[i+27] != 0xC7 || bytes[i+28] != 0x05 ||
                bytes[i+33] != 0xFF || bytes[i+34] != 0xFF || bytes[i+35] != 0xFF || bytes[i+36] != 0xFF ||
                bytes[i+37] != 0x80 || bytes[i+38] != 0x3D) continue;
            DWORD first = 0, second = 0, page = 0; std::int32_t relative = 0;
            memcpy(&first, bytes.data()+i+2, 4); memcpy(&second, bytes.data()+i-4, 4);
            memcpy(&page, bytes.data()+i+19, 4); memcpy(&relative, bytes.data()+i+13, 4);
            const auto target = image+start+i+17+static_cast<std::uintptr_t>(relative);
            if (first < image+8 || first-image > length-32 || second != first+1 ||
                page < image || page-image > length-4 || target < image || target-image > length-512) continue;
            acceptance = {reinterpret_cast<unsigned char*>(first), reinterpret_cast<unsigned char*>(second),
                reinterpret_cast<DWORD*>(page), reinterpret_cast<std::uint8_t*>(target), base+start+i};
            ++matches;
        }
    }
    return matches == 1;
}

struct NativeZoom {
    unsigned char* table = nullptr;
    DWORD* index = nullptr;
    DWORD* focus = nullptr;
    unsigned char* pauseActive = nullptr;
    unsigned selected = 3;
    float target = 2180.0f;
    bool patched = false;
} nativeZoom;
int zoomPercent = 300;
bool FindNativeZoom(std::uint8_t* base) {
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
    auto section = IMAGE_FIRST_SECTION(nt);
    const float stops[] = {6540,5263,3986,2709,1175,650};
    unsigned matches = 0;
    for (unsigned s = 0; s < nt->FileHeader.NumberOfSections; ++s) {
        if (!(section[s].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        auto start = section[s].VirtualAddress, size = section[s].Misc.VirtualSize;
        if (start >= nt->OptionalHeader.SizeOfImage || size > nt->OptionalHeader.SizeOfImage-start) continue;
        std::vector<unsigned char> code(size);
        if (!Read(base+start, code.data(), size)) continue;
        for (std::size_t i = 0; i+180 < size; ++i) {
            bool match = true;
            for (unsigned level = 0; level < 6; ++level) {
                const auto p = code.data()+i+level*8;
                if (p[0] != 0xC7 || p[1] != 0x44 || p[2] != 0x24 || p[3] != 0x1C+level*4 || memcmp(p+4, stops+level, 4)) { match = false; break; }
            }
            if (!match) continue;
            for (std::size_t j = i+48; j+21 < i+160; ++j) {
                // mov eax,[index]; push 1; movss xmm0,[esp+eax*4+20]; movss [target],xmm0
                const unsigned char sequence[] = {0x6A,0x01,0xF3,0x0F,0x10,0x44,0x84,0x20,0xF3,0x0F,0x11,0x05};
                if (code[j] != 0xA1 || memcmp(code.data()+j+5, sequence, sizeof(sequence))) continue;
                DWORD index = 0, target = 0; memcpy(&index, code.data()+j+1, 4); memcpy(&target, code.data()+j+17, 4);
                const auto current = reinterpret_cast<std::uintptr_t>(trace.value);
                if (target != current+4 || index != current+12) continue;
                nativeZoom.table = base+start+i;
                nativeZoom.index = reinterpret_cast<DWORD*>(index);
                nativeZoom.focus = reinterpret_cast<DWORD*>(current+28);
                nativeZoom.pauseActive = acceptance.first-1; // captured 0xC34 flag; observed pause-only
                ++matches;
            }
        }
    }
    return matches == 1;
}
bool PatchZoomStop() {
    if (nativeZoom.patched || nativeZoom.selected == 0) return true;
    const float stops[] = {6540,5263,3986,2709,1175,650};
    auto immediate = nativeZoom.table+nativeZoom.selected*8+4;
    float current = 0;
    if (!Read(immediate, &current, 4) || current != stops[nativeZoom.selected]) { Log("Native zoom-stop constant changed; update skipped."); return false; }
    // Patch only the chosen float immediate, after real interactive acceptance.
    // No jumps/detours. The original 100% and closest/farthest stops remain intact.
    // Require all four bytes to remain within one cache line/page.
    const auto address = reinterpret_cast<std::uintptr_t>(immediate);
    if ((address & 63) > 60 || (address & 4095) > 4092) return false;
    DWORD old = 0;
    if (!VirtualProtect(immediate, 4, PAGE_EXECUTE_READWRITE, &old)) { Log("Could not make zoom constant writable."); return false; }
    SIZE_T written = 0;
    const bool success = WriteProcessMemory(GetCurrentProcess(), immediate, &nativeZoom.target, 4, &written) && written == 4;
    DWORD ignored = 0;
    const bool restored = VirtualProtect(immediate, 4, old, &ignored) != 0;
    FlushInstructionCache(GetCurrentProcess(), immediate, 4);
    float after = 0;
    if (!success || !restored || !Read(immediate, &after, 4) || after != nativeZoom.target) { Log("Zoom-stop patch/readback/protection restore failed; no index selected."); return false; }
    nativeZoom.patched = true;
    char line[180]; sprintf_s(line, "Native stop %u: %.3f -> %.3f; code float only; original 100%% stop retained.", nativeZoom.selected, current, after); Log(line);
    return true;
}
bool SelectOpeningStop() {
    DWORD focus = 0, index = 0; unsigned char pauseActive = 0;
    if (!Read(nativeZoom.focus, &focus, 4) || focus != 5 || !Read(nativeZoom.pauseActive, &pauseActive, 1) || pauseActive != 1 ||
        !Read(nativeZoom.index, &index, 4) || index > 5) return false;
    if (!PatchZoomStop()) return false;
    // Recheck the real focus before selecting native zoom. Never touch center or live ranges.
    if (!Read(nativeZoom.focus, &focus, 4) || focus != 5) return false;
    const DWORD chosen = nativeZoom.selected;
    SIZE_T written = 0;
    if (!WriteProcessMemory(GetCurrentProcess(), nativeZoom.index, &chosen, 4, &written) || written != 4) return false;
    char line[180]; sprintf_s(line, "Interactive map accepted: native stop %lu -> %u, requested %d%%; game handles animation/centering.", index, nativeZoom.selected, zoomPercent); Log(line);
    return true;
}
bool RestoreZoomStop() {
    if(!nativeZoom.patched)return true;
    const float stops[]={6540,5263,3986,2709,1175,650};
    auto immediate=nativeZoom.table+nativeZoom.selected*8+4;float current=0;
    if(!Read(immediate,&current,4)||current!=nativeZoom.target){Log("Autozoom off: stop changed by another writer; left unchanged.");nativeZoom.patched=false;return false;}
    DWORD old=0;if(!VirtualProtect(immediate,4,PAGE_EXECUTE_READWRITE,&old))return false;
    SIZE_T written=0;const bool ok=WriteProcessMemory(GetCurrentProcess(),immediate,&stops[nativeZoom.selected],4,&written)&&written==4;
    DWORD ignored=0;VirtualProtect(immediate,4,old,&ignored);FlushInstructionCache(GetCurrentProcess(),immediate,4);
    if(ok){nativeZoom.patched=false;Log("Map Autozoom off: original zoom stop restored.");}return ok;
}
DWORD WINAPI AutoZoom(LPVOID) {
    interactivezoom::Gate gate;
    unsigned events = 0;
    bool decoded=false,failed=false;
    unsigned priorMenuRows=0;
    for (;;) {
        const unsigned rows=qolsettings::menuRows.load();
        if(rows!=priorMenuRows){char line[100];sprintf_s(line,"QoL native menu registered rows mask=%04X",rows);Log(line);priorMenuRows=rows;}
        if(!qolsettings::Get(QOL_MAP_AUTOZOOM)) {RestoreZoomStop();gate={};Sleep(50);continue;}
        if(failed){Sleep(500);continue;}
        if(!decoded){
            auto base=reinterpret_cast<std::uint8_t*>(GetModuleHandleA(nullptr));auto getter=FindGetter(base);
            if(!getter||!FindTraceTargets(base,getter)||!FindAcceptance(base)||!FindNativeZoom(base)){Log("Runtime range signatures missing or ambiguous; no changes.");failed=true;continue;}
            nativeZoom.target=6540.0f/(zoomPercent/100.0f);nativeZoom.selected=interactivezoom::SelectStop(nativeZoom.target);decoded=true;
        }
        DWORD focus = 0; unsigned char pauseActive = 0;
        if (!Read(nativeZoom.focus, &focus, 4) || !Read(nativeZoom.pauseActive, &pauseActive, 1)) { Log("Map-focus read failed; worker stopped."); return 0; }
        const bool before = gate.active;
        if (gate.Observe(pauseActive == 1, focus, GetTickCount())) {
            if (!SelectOpeningStop()) Log("Interactive-entry checks failed; skipped this opening.");
        }
        if (events < 64 && before != gate.active) {
            char line[140]; sprintf_s(line, "Map-focus transition: pause=%u focus=%lu interactive=%u", static_cast<unsigned>(pauseActive), focus, gate.active ? 1u : 0u); Log(line); ++events;
        }
        Sleep(20);
    }
}
#include "runtime_diagnostics.hpp"
BOOL CALLBACK Initialize(PINIT_ONCE, PVOID, PVOID*) {
    char exe[MAX_PATH]{}, ini[MAX_PATH]{};
    DWORD n = GetModuleFileNameA(nullptr, exe, MAX_PATH);
    if (!n || n >= MAX_PATH) return TRUE;
    char* slash = strrchr(exe, '\\');
    if (!slash) return TRUE;
    const std::size_t prefix = static_cast<std::size_t>(slash-exe+1);
    if (prefix+20 >= MAX_PATH) return TRUE;
    memcpy(ini, exe, prefix); strcpy_s(ini+prefix, MAX_PATH-prefix, "GTAIV-QoL.ini");
    memcpy(logPath, exe, prefix); strcpy_s(logPath+prefix, MAX_PATH-prefix, "GTAIV-QoL.log");
    logging = GetPrivateProfileIntA("Diagnostics", "LogToFile", 1, ini) == 1;
    if (logging) { FILE* f = nullptr; if (fopen_s(&f, logPath, "w") == 0 && f) std::fclose(f); }
    Log("GTAIV-QoL 300% native interactive-map zoom; failed cutscene focus probe removed. No code detours or exception observers.");
    Log("Cutscene focus probe removed: synthetic activation minimized the game without pausing the cutscene.");
    diagnostics::Start(ini, exe, prefix);
    qolsettings::Ensure();
    zoomPercent=GetPrivateProfileIntA("DefaultMapZoom","ZoomPercent",300,ini);
    if(!SupportedVersion(exe)||!mapzoom::ValidPercent(zoomPercent)){
        Log("Unsupported version or ZoomPercent; no changes.");
        Log("ASI initialization completed without scanning or installing a hook.");return TRUE;
    }
    HANDLE thread = CreateThread(nullptr, 0, AutoZoom, nullptr, 0, nullptr);
    if (thread) { CloseHandle(thread); Log("Experimental map-entry zoom worker active."); }
    else Log("Could not start map-entry worker.");
    return TRUE;
}
}
extern "C" __declspec(dllexport) const QolSettingsApi* QoLSettings_GetApi(uint32_t version) {qolsettings::Ensure();return version==1?&qolsettings::api:nullptr;}
extern "C" __declspec(dllexport) void InitializeASI() { InitOnceExecuteOnce(&once, Initialize, nullptr, nullptr); }
extern "C" __declspec(dllexport) const QolSettingsApi* QoLSettings_UiApi(uint32_t version) {qolsettings::Ensure();return version==1?&qolsettings::uiApi:nullptr;}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(instance);
    return TRUE;
}

