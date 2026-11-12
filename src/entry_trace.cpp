#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winver.h>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <cstring>
#include "map_zoom_policy.hpp"
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
void Dump(const void* address, unsigned length) {
    unsigned char bytes[640]{};
    if (length > sizeof(bytes) || !Read(address, bytes, length)) { Log("Code read unavailable."); return; }
    auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleA(nullptr));
    for (unsigned i = 0; i < length; i += 16) {
        char line[130]; int used = sprintf_s(line, "tracecode RVA=%08lX:", static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(address)+i-base));
        for (unsigned j = i; j < length && j < i+16; ++j)
            used += sprintf_s(line+used, sizeof(line)-static_cast<std::size_t>(used), " %02X", bytes[j]);
        Log(line);
    }
}
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

void InspectReferences() {
    auto base = reinterpret_cast<std::uint8_t*>(GetModuleHandleA(nullptr));
    const auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    const auto nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
    auto section = IMAGE_FIRST_SECTION(nt);
    const auto operand = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(trace.flag));
    unsigned stores = 0;
    for (unsigned s = 0; s < nt->FileHeader.NumberOfSections && stores < 6; ++s) {
        if (!(section[s].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        auto start = section[s].VirtualAddress, size = section[s].Misc.VirtualSize;
        if (start >= nt->OptionalHeader.SizeOfImage || size > nt->OptionalHeader.SizeOfImage-start) continue;
        std::vector<unsigned char> code(size);
        if (!Read(base+start, code.data(), size)) continue;
        for (std::size_t i = 64; i+192 < size && stores < 6; ++i) {
            if (memcmp(code.data()+i, &operand, 4) != 0) continue;
            bool store = (code[i-2] == 0xC6 && code[i-1] == 0x05) ||
                code[i-1] == 0xA2 || (code[i-2] == 0x88 && (code[i-1] & 0xC7) == 0x05);
            if (!store) continue;
            char line[160]; sprintf_s(line, "Flag-writer candidate %u operand RVA=%08lX (requires disassembly)", ++stores, static_cast<DWORD>(start+i)); Log(line);
            Dump(base+start+i-64, 256);
        }
    }
    char line[100]; sprintf_s(line, "Flag-writer inspection: %u candidate stores.", stores); Log(line);
}

DWORD WINAPI ObserveRange(LPVOID) {
    InspectReferences();
    unsigned char previous[24]{};
    float previousRange[2]{};
    unsigned events = 0, lastEvent = 0;
    for (unsigned tick = 0; tick < 12000; ++tick) {
        float ranges[2]{};
        unsigned char controls[24]{};
        // A bounded frontend control-state neighborhood only. No pointers are
        // followed from this snapshot and no game data/code is ever written.
        if (Read(trace.value, ranges, sizeof(ranges)) && Read(trace.flag-7, controls, sizeof(controls))) {
            const bool changed = memcmp(controls, previous, sizeof(controls)) != 0 || memcmp(ranges, previousRange, sizeof(ranges)) != 0;
            if (events < 64 && (!events || (changed && tick-lastEvent >= 5))) {
                char line[200]; int used = sprintf_s(line, "Entry trace t=%ums range=%.3f other=%.3f controls=", tick*50, ranges[0], ranges[1]);
                for (unsigned i = 0; i < sizeof(controls); ++i)
                    used += sprintf_s(line+used, sizeof(line)-static_cast<std::size_t>(used), "%02X", controls[i]);
                Log(line); ++events; lastEvent = tick;
                memcpy(previous, controls, sizeof(controls)); memcpy(previousRange, ranges, sizeof(ranges));
            }
        }
        Sleep(50);
    }
    Log("Read-only interactive-entry observation finished."); return 0;
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
    Log("GTAIV-QoL 0.4.1 READ-ONLY interactive-entry trace. No game writes, detours, or exception observers.");
    if (!GetPrivateProfileIntA("Diagnostics", "TraceRange", 1, ini) || !SupportedVersion(exe)) {
        Log("ASI initialization completed without scanning or installing a hook."); return TRUE;
    }
    auto base = reinterpret_cast<std::uint8_t*>(GetModuleHandleA(nullptr));
    auto getter = FindGetter(base);
    if (!getter || !FindTraceTargets(base, getter)) { Log("Range reader signature missing or ambiguous; trace skipped."); return TRUE; }
    char line[160]; sprintf_s(line, "Decoded trace targets: range=%p flag=%p caller=%p", trace.value, trace.flag, trace.caller); Log(line);
    HANDLE thread = CreateThread(nullptr, 0, ObserveRange, nullptr, 0, nullptr);
    if (thread) { CloseHandle(thread); Log("Read-only interactive-entry trace active for ten minutes; no zoom changes."); }
    else Log("Could not start read-only trace.");
    return TRUE;
}
}
extern "C" __declspec(dllexport) void InitializeASI() { InitOnceExecuteOnce(&once, Initialize, nullptr, nullptr); }
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(instance);
    return TRUE;
}
