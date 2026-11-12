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

float* companion = nullptr;
int zoomPercent = 150;
bool FindCompanion(std::uint8_t* base) {
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
    auto section = IMAGE_FIRST_SECTION(nt);
    unsigned matches = 0;
    const auto current = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(trace.value));
    for (unsigned s = 0; s < nt->FileHeader.NumberOfSections; ++s) {
        if (!(section[s].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        auto start = section[s].VirtualAddress, size = section[s].Misc.VirtualSize;
        if (start >= nt->OptionalHeader.SizeOfImage || size > nt->OptionalHeader.SizeOfImage-start) continue;
        std::vector<unsigned char> code(size);
        if (!Read(base+start, code.data(), size)) continue;
        for (std::size_t i = 0; i+20 <= size; ++i) {
            if (code[i] != 0xC7 || code[i+1] != 0x05 || code[i+10] != 0xC7 || code[i+11] != 0x05) continue;
            std::uint32_t first = 0, second = 0, firstValue = 0, secondValue = 0;
            memcpy(&first, code.data()+i+2, 4); memcpy(&firstValue, code.data()+i+6, 4);
            memcpy(&second, code.data()+i+12, 4); memcpy(&secondValue, code.data()+i+16, 4);
            // Captured game initialization sets the two adjacent floats to 6540.
            if (second == current && first == current+4 && firstValue == 0x45CC6000 && secondValue == firstValue) {
                companion = reinterpret_cast<float*>(first); ++matches;
            }
        }
    }
    return matches == 1;
}
bool WritableData(const void* address, SIZE_T length) {
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(address, &info, sizeof(info)) || info.State != MEM_COMMIT ||
        (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    const DWORD protection = info.Protect & 0xFF;
    if (protection != PAGE_READWRITE && protection != PAGE_WRITECOPY) return false;
    const auto start = reinterpret_cast<std::uintptr_t>(address);
    const auto end = reinterpret_cast<std::uintptr_t>(info.BaseAddress)+info.RegionSize;
    return start <= end && length <= end-start;
}
bool ApplyOpening(const openingzoom::Ranges& before) {
    unsigned char flag = 0;
    openingzoom::Ranges latest{};
    if (!Read(trace.flag, &flag, 1) || flag != 1 || !Read(trace.value, &latest, sizeof(latest)) ||
        !openingzoom::IsDefault(latest) || !openingzoom::IsDefault(before) || !WritableData(trace.value, sizeof(latest))) return false;
    const float target = 6540.0f/(zoomPercent/100.0f);
    const openingzoom::Ranges after{target, target};
    SIZE_T written = 0;
    if (!WriteProcessMemory(GetCurrentProcess(), trace.value, &after, sizeof(after), &written) || written != sizeof(after)) return false;
    openingzoom::Ranges verified{};
    if (!Read(trace.value, &verified, sizeof(verified))) return false;
    char line[200];
    sprintf_s(line, "Map opening: range %.3f -> %.3f; companion %.3f -> %.3f; readback=%s",
        before.current, verified.current, before.companion, verified.companion,
        verified.current == target && verified.companion == target ? "verified" : "engine changed during readback");
    Log(line);
    return true; // consume this opening even if engine immediately adjusted the result
}
DWORD WINAPI AutoZoom(LPVOID) {
    openingzoom::EntryGate gate;
    unsigned events = 0;
    for (;;) {
        unsigned char flag = 0;
        openingzoom::Ranges ranges{};
        if (!Read(trace.flag, &flag, 1) || !Read(trace.value, &ranges, sizeof(ranges))) {
            Log("Map-state read failed; zoom worker stopped."); return 0;
        }
        const bool wasActive = gate.active;
        if (gate.Observe(flag, ranges, GetTickCount())) {
            if (ApplyOpening(ranges)) gate.Applied();
            else { gate.Applied(); Log("Map opening changed or data was not writable; skipped this opening."); }
        }
        if (events < 64 && wasActive != gate.active) {
            char line[180]; sprintf_s(line, "Map-state transition: active=%u current=%.3f companion=%.3f", flag, ranges.current, ranges.companion); Log(line); ++events;
        }
        Sleep(20);
    }
}
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
    Log("GTAIV-QoL 0.4.0 experimental map-entry runtime zoom. No code detours or exception observers.");
    if (!GetPrivateProfileIntA("DefaultMapZoom", "Enabled", 0, ini)) {
        Log("ASI initialization completed without scanning or installing a hook."); return TRUE;
    }
    zoomPercent = GetPrivateProfileIntA("DefaultMapZoom", "ZoomPercent", 150, ini);
    if (!SupportedVersion(exe) || !mapzoom::ValidPercent(zoomPercent)) { Log("Unsupported version or ZoomPercent; no changes."); return TRUE; }
    auto base = reinterpret_cast<std::uint8_t*>(GetModuleHandleA(nullptr));
    auto getter = FindGetter(base);
    if (!getter || !FindTraceTargets(base, getter) || !FindCompanion(base)) { Log("Runtime range signatures missing or ambiguous; no changes."); return TRUE; }
    char line[180]; sprintf_s(line, "Decoded runtime range=%p companion=%p map-state=%p; requested=%d%%", trace.value, companion, trace.flag, zoomPercent); Log(line);
    HANDLE thread = CreateThread(nullptr, 0, AutoZoom, nullptr, 0, nullptr);
    if (thread) { CloseHandle(thread); Log("Experimental map-entry zoom worker active."); }
    else Log("Could not start map-entry worker.");
    return TRUE;
}
}
extern "C" __declspec(dllexport) void InitializeASI() { InitOnceExecuteOnce(&once, Initialize, nullptr, nullptr); }
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(instance);
    return TRUE;
}
