#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winver.h>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <cstring>
#include <atomic>
#include "../vendor/minhook/include/MinHook.h"
static_assert(sizeof(void*) == 4, "GTA IV requires x86");
namespace {
struct Vec2 { float x, y; };
using Getter = Vec2* (__cdecl*)(Vec2*, int);
Getter original = nullptr;
float multiplier = 1.5f;
int passThroughOnly = 0;
char logPath[MAX_PATH]{};
bool logging = false;
std::atomic<unsigned> reads{0};
INIT_ONCE once = INIT_ONCE_STATIC_INIT;
void Log(const char* message) {
    if (!logging) return;
    FILE* f = nullptr;
    if (fopen_s(&f, logPath, "a") == 0 && f) { std::fprintf(f, "%s\n", message); std::fclose(f); }
}
Vec2* __cdecl GetWidget(Vec2* out, int id) {
    if (id == 63 && reads.load() < 32) Log("Widget 63: entering original getter.");
    Vec2* result = original(out, id);
    // Return the engine's normal result for every other design value. The original
    // getter refreshes this value on every read, so this cannot compound per visit.
    if (id == 63 && result && out && result == out) {
        const float before = result->x;
        const bool expected = before == 800.0f && result->y == 0.0f;
        if (expected) result->x = before / multiplier;
        if (reads.fetch_add(1) < 32) {
            char line[180];
            sprintf_s(line, "MAP_map_border_init read: %.3f -> %.3f; %s", before, result->x,
                expected ? "experimental range applied" : "unexpected value; unchanged");
            Log(line);
        }
    }
    return result;
}
// The original C++ replacement routed EVERY widget through an additional C++
// call. A conventional C ABI does not promise to preserve volatile registers,
// even if this particular game function happens to preserve them. Keep all
// non-map traffic completely transparent, including EFLAGS and XMM registers.
__declspec(naked) void GetWidgetEntry() {
    __asm {
        pushfd
        cmp dword ptr [passThroughOnly], 0
        jne originalWidget
        cmp dword ptr [esp + 12], 63
        je mapWidget
    originalWidget:
        popfd
        jmp dword ptr [original]
    mapWidget:
        popfd
        jmp GetWidget
    }
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
BOOL CALLBACK Initialize(PINIT_ONCE, PVOID, PVOID*) {
    char exe[MAX_PATH]{}, ini[MAX_PATH]{};
    DWORD n = GetModuleFileNameA(nullptr, exe, MAX_PATH);
    if (!n || n >= MAX_PATH) return TRUE;
    char* slash = strrchr(exe, '\\');
    if (!slash) return TRUE;
    const std::size_t prefix = static_cast<std::size_t>(slash - exe + 1);
    if (prefix + 20 >= MAX_PATH) return TRUE;
    memcpy(ini, exe, prefix);
    strcpy_s(ini + prefix, MAX_PATH-prefix, "GTAIV-QoL.ini");
    memcpy(logPath, exe, prefix);
    strcpy_s(logPath + prefix, MAX_PATH-prefix, "GTAIV-QoL.log");
    logging = GetPrivateProfileIntA("Diagnostics", "LogToFile", 0, ini) == 1;
    Log("GTAIV-QoL 0.2.2 startup isolation; no exception observer (x86)");
    if (GetPrivateProfileIntA("DefaultMapZoom", "Enabled", 0, ini) != 1) { Log("Zoom disabled: ASI initialization completed without scanning or installing a hook."); return TRUE; }
    int percent = GetPrivateProfileIntA("DefaultMapZoom", "ZoomPercent", 150, ini);
    if (percent < 100 || percent > 400) { Log("Invalid ZoomPercent; no hook installed."); return TRUE; }
    multiplier = percent / 100.0f;
    if (!SupportedVersion(exe)) { Log("Unsupported executable version; requires 1.2.0.59. No hook installed."); return TRUE; }
    auto target = FindGetter(reinterpret_cast<std::uint8_t*>(GetModuleHandleA(nullptr)));
    if (!target) { Log("Getter signature missing or ambiguous; no hook installed."); return TRUE; }
    char location[240];
    sprintf_s(location, "Getter target=%p RVA=%08lX; QoL entry=%p; mode=%s", target,
        static_cast<DWORD>(target-reinterpret_cast<std::uint8_t*>(GetModuleHandleA(nullptr))),
        reinterpret_cast<void*>(&GetWidgetEntry),
        GetPrivateProfileIntA("Diagnostics", "PassThroughOnly", 0, ini) ? "pass-through only" : "map zoom");
    Log(location);
    if (MH_Initialize() != MH_OK) { Log("MinHook initialization failed; no hook installed."); return TRUE; }
    if (MH_CreateHook(target, reinterpret_cast<void*>(&GetWidgetEntry), reinterpret_cast<void**>(&original)) != MH_OK) {
        Log("Hook creation failed; no hook installed."); return TRUE;
    }
    passThroughOnly = GetPrivateProfileIntA("Diagnostics", "PassThroughOnly", 0, ini) ? 1 : 0;
    sprintf_s(location, "Trampoline=%p", reinterpret_cast<void*>(original));
    Log(location);
    if (MH_EnableHook(target) != MH_OK) { MH_RemoveHook(target); Log("Hook activation failed."); return TRUE; }
    Log("Initialization-range hook active. Awaiting widget 63 reads; gameplay effect requires in-game verification.");
    return TRUE;
}
}
// Ultimate ASI Loader invokes InitializeASI outside DllMain/loader lock.
// No worker-thread memory writes or game calls from DllMain.
extern "C" __declspec(dllexport) void InitializeASI() { InitOnceExecuteOnce(&once, Initialize, nullptr, nullptr); }
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(instance);
    return TRUE;
}
