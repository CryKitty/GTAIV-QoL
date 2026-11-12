#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winver.h>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <cstring>
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

// ReadProcessMemory safely rejects unavailable pages. This build never executes
// the discovered function, changes page protection, or writes to the game image.
void DumpCode(const void* address, unsigned length, std::uintptr_t imageBase) {
    unsigned char bytes[192]{};
    SIZE_T count = 0;
    if (length > sizeof(bytes) || !ReadProcessMemory(GetCurrentProcess(), address, bytes, length, &count) || count != length) {
        Log("Code read unavailable; skipped."); return;
    }
    for (unsigned i = 0; i < length; i += 16) {
        char line[120]{};
        int used = sprintf_s(line, "code RVA=%08lX:", static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(address)+i-imageBase));
        for (unsigned j = i; j < length && j < i+16; ++j)
            used += sprintf_s(line+used, sizeof(line)-static_cast<std::size_t>(used), " %02X", bytes[j]);
        Log(line);
    }
}
void InspectFrontend(std::uint8_t* base) {
    auto getter = FindGetter(base);
    if (!getter) { Log("Getter signature missing or ambiguous. Inspection stopped."); return; }
    const auto imageBase = reinterpret_cast<std::uintptr_t>(base);
    char line[160];
    sprintf_s(line, "READ ONLY: getter=%p RVA=%08lX; no detour installed.", getter, static_cast<DWORD>(getter-base));
    Log(line);
    DumpCode(getter, 192, imageBase);
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
    auto section = IMAGE_FIRST_SECTION(nt);
    unsigned found = 0;
    for (unsigned s = 0; s < nt->FileHeader.NumberOfSections && found < 16; ++s) {
        if (!(section[s].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        const auto start = section[s].VirtualAddress;
        const auto size = section[s].Misc.VirtualSize;
        if (start >= nt->OptionalHeader.SizeOfImage || size > nt->OptionalHeader.SizeOfImage-start) continue;
        // Take a bounded copy first; don't directly read possibly unavailable pages.
        std::vector<std::uint8_t> code(size);
        SIZE_T copied = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), base+start, code.data(), size, &copied) || copied != size) continue;
        for (std::size_t i = 32; i+16 < size && found < 16; ++i) {
            if (code[i] != 0xE8) continue;
            std::int32_t relative = 0;
            memcpy(&relative, code.data()+i+1, sizeof(relative));
            const auto destination = imageBase+start+i+5+static_cast<std::uintptr_t>(relative);
            if (destination != reinterpret_cast<std::uintptr_t>(getter)) continue;
            ++found;
            sprintf_s(line, "Candidate direct call %u at RVA=%08lX (byte match; instruction boundary unverified):", found, static_cast<DWORD>(start+i));
            Log(line);
            DumpCode(base+start+i-32, 48, imageBase);
        }
    }
    sprintf_s(line, "Read-only inspection completed: %u candidate callers captured; game code unchanged.", found);
    Log(line);
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
    Log("GTAIV-QoL 0.2.3 READ-ONLY frontend probe. No hooks, exception observers or game function calls.");
    // Old Enabled/PassThroughOnly settings cannot activate a hook in this binary.
    if (SupportedVersion(exe)) InspectFrontend(reinterpret_cast<std::uint8_t*>(GetModuleHandleA(nullptr)));
    else Log("Unsupported executable version; read-only inspection skipped.");
    Log("Read-only ASI initialization completed; no hook installed.");
    return TRUE;
}
}
extern "C" __declspec(dllexport) void InitializeASI() { InitOnceExecuteOnce(&once, Initialize, nullptr, nullptr); }
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(instance);
    return TRUE;
}
