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
bool Writable(const void* address, SIZE_T length) {
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(address, &info, sizeof(info)) || info.State != MEM_COMMIT ||
        (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    const DWORD protection = info.Protect & 0xFF;
    if (protection != PAGE_READWRITE && protection != PAGE_WRITECOPY) return false;
    auto start = reinterpret_cast<std::uintptr_t>(address);
    auto end = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
    return start <= end && length <= end-start;
}
struct Settings {
    std::uint32_t* slot; int percent;
    std::uint8_t* getter = nullptr;
    bool monitor = true;
    unsigned attempts = 6000;
} settings{};
void InspectMapCallers() {
    if (!logging || !settings.getter) return;
    auto base = reinterpret_cast<std::uint8_t*>(GetModuleHandleA(nullptr));
    const auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    const auto nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
    const auto section = IMAGE_FIRST_SECTION(nt);
    unsigned captured = 0;
    for (unsigned s = 0; s < nt->FileHeader.NumberOfSections && captured < 6; ++s) {
        if (!(section[s].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        auto start = section[s].VirtualAddress, size = section[s].Misc.VirtualSize;
        if (start >= nt->OptionalHeader.SizeOfImage || size > nt->OptionalHeader.SizeOfImage-start) continue;
        std::vector<unsigned char> bytes(size);
        if (!Read(base+start, bytes.data(), size)) continue;
        for (std::size_t i = 64; i+192 < size && captured < 6; ++i) {
            if (bytes[i] != 0xE8) continue;
            std::int32_t relative = 0;
            memcpy(&relative, bytes.data()+i+1, 4);
            const auto destination = reinterpret_cast<std::uintptr_t>(base)+start+i+5+static_cast<std::uintptr_t>(relative);
            if (destination != reinterpret_cast<std::uintptr_t>(settings.getter)) continue;
            bool candidate = false;
            // This is only a candidate filter, not an instruction decoder. Full
            // surrounding bytes are captured for offline boundary/data-flow review.
            for (std::size_t j = i-24; j+1 < i; ++j)
                if (bytes[j] == 0x6A && bytes[j+1] == 63) candidate = true;
            if (!candidate) continue;
            ++captured;
            char line[180];
            sprintf_s(line, "Map-init candidate %u call RVA=%08lX (near push 63; requires disassembly):", captured, static_cast<DWORD>(start+i));
            Log(line);
            for (std::size_t j = i-64; j < i+192; j += 16) {
                int used = sprintf_s(line, "mapcode RVA=%08lX:", static_cast<DWORD>(start+j));
                for (unsigned k = 0; k < 16; ++k)
                    used += sprintf_s(line+used, sizeof(line)-static_cast<std::size_t>(used), " %02X", bytes[j+k]);
                Log(line);
            }
        }
    }
    char line[100]; sprintf_s(line, "Targeted map-init inspection finished: %u candidate callers.", captured); Log(line);
}
DWORD WINAPI ApplyInitialRange(LPVOID) {
    // No game calls, executable writes, page-protection changes or exception hooks.
    // This remains design-data-only; no live zoom/center state is touched.
    // Reapply only if a validated design table is restored to the stock 800.
    InspectMapCallers();
    unsigned updates = 0, events = 0;
    std::uint32_t observedTable = 0;
    mapzoom::Pair observedRows[3]{};
    std::uint32_t previous = 0;
    unsigned stable = 0;
    for (unsigned attempt = 0; attempt < settings.attempts; ++attempt) {
        std::uint32_t table = 0;
        if (!Read(settings.slot, &table, sizeof(table))) break;
        if (table && table == previous) ++stable; else stable = 0;
        previous = table;
        if (table && stable >= 2 && table <= UINT32_MAX-66*sizeof(mapzoom::Pair)) {
            auto entry = reinterpret_cast<mapzoom::Pair*>(table + 63*sizeof(mapzoom::Pair));
            mapzoom::Pair rows[3]{};
            if (!Read(entry, rows, sizeof(rows))) { Sleep(100); continue; }
            if (events < 24 && (table != observedTable || memcmp(rows, observedRows, sizeof(rows)) != 0)) {
                char event[240];
                sprintf_s(event, "Table observation t=%ums pointer=%08lX init=%.3f,%.3f maximum=%.3f,%.3f scroll=%.3f,%.3f",
                    attempt*100, static_cast<DWORD>(table), rows[0].x, rows[0].y, rows[1].x, rows[1].y, rows[2].x, rows[2].y);
                Log(event); ++events; observedTable = table; memcpy(observedRows, rows, sizeof(rows));
            }
            if (mapzoom::ExpectedMapRows(rows) && updates < 16 &&
                (updates == 0 || settings.percent != 100)) {
                std::uint32_t current = 0;
                const float target = mapzoom::InitialRange(settings.percent);
                char line[240];
                sprintf_s(line, "Validated active map table: pointer=%08lX init=%.3f,%.3f maximum=%.3f,%.3f scroll=%.3f,%.3f",
                    static_cast<DWORD>(table), rows[0].x, rows[0].y, rows[1].x, rows[1].y, rows[2].x, rows[2].y);
                Log(line);
                if (!Read(settings.slot, &current, sizeof(current)) || current != table || !Writable(entry, sizeof(float))) {
                    Log("Table changed or entry is not writable data; no change made."); return 0;
                }
                SIZE_T written = 0;
                if (!WriteProcessMemory(GetCurrentProcess(), entry, &target, sizeof(target), &written) || written != sizeof(target)) {
                    Log("Data update failed; no executable code was modified."); return 0;
                }
                mapzoom::Pair after[3]{};
                if (!Read(entry, after, sizeof(after))) { Log("Data update could not be read back."); return 0; }
                sprintf_s(line, "Initial map range data: %.3f -> %.3f (requested %d%% zoom). Readback=%s; monitoring table reloads.",
                    rows[0].x, after[0].x, settings.percent,
                    after[0].x == target && after[0].y == rows[0].y &&
                    memcmp(after+1, rows+1, 2*sizeof(mapzoom::Pair)) == 0 ? "verified" : "unexpected");
                Log(line);
                ++updates;
                if (!settings.monitor) return 0;
                if (updates == 16) Log("Data update limit reached; further writes disabled for this session.");
            }
        }
        if (attempt == 300 || attempt == 1200) {
            char line[160]; sprintf_s(line, "Monitoring t=%ums table=%08lX updates=%u", attempt*100, static_cast<DWORD>(table), updates); Log(line);
        }
        Sleep(100);
    }
    char summary[120]; sprintf_s(summary, "Design-table monitoring finished; verified-write attempts=%u.", updates); Log(summary);
    return 0;
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
    Log("GTAIV-QoL 0.3.1 monitored initial-range DATA update. No code detours or exception observers.");
    if (!GetPrivateProfileIntA("DefaultMapZoom", "Enabled", 0, ini)) {
        Log("Zoom disabled: ASI initialization completed without scanning or installing a hook."); return TRUE;
    }
    settings.percent = GetPrivateProfileIntA("DefaultMapZoom", "ZoomPercent", 150, ini);
    if (!mapzoom::ValidPercent(settings.percent) || !SupportedVersion(exe)) {
        Log("Unsupported version or ZoomPercent; no data update."); return TRUE;
    }
    auto base = reinterpret_cast<std::uint8_t*>(GetModuleHandleA(nullptr));
    auto getter = FindGetter(base);
    if (!getter) { Log("Getter signature missing or ambiguous; no data update."); return TRUE; }
    // Full observed 1.2.0.59 getter: mov esi,[table]; copy two words at id*8.
    // Only the absolute pointer-slot operand (bytes 7..10) may vary.
    const unsigned char expected[] = {
        0x8B,0x44,0x24,0x04,0x56,0x8B,0x35,0,0,0,0,0x85,0xF6,0x74,0x12,0x8B,
        0x54,0x24,0x0C,0x8B,0x0C,0xD6,0x89,0x08,0x8B,0x4C,0xD6,0x04,0x89,0x48,0x04,0x5E,
        0xC3,0xC7,0x00,0,0,0,0,0xC7,0x40,0x04,0,0,0,0,0x5E,0xC3};
    unsigned char actual[sizeof(expected)]{};
    if (!Read(getter, actual, sizeof(actual))) return TRUE;
    for (unsigned i = 0; i < sizeof(expected); ++i)
        if ((i < 7 || i > 10) && actual[i] != expected[i]) {
            Log("Getter body differs from observed CE layout; no data update."); return TRUE;
        }
    std::uint32_t slot = 0;
    memcpy(&slot, actual+7, sizeof(slot));
    const auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    const auto nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
    const auto start = reinterpret_cast<std::uintptr_t>(base);
    if (slot < start || slot-start > nt->OptionalHeader.SizeOfImage-sizeof(slot)) {
        Log("Table pointer slot is outside executable image; no data update."); return TRUE;
    }
    settings.slot = reinterpret_cast<std::uint32_t*>(slot);
    settings.getter = getter;
    HANDLE thread = CreateThread(nullptr, 0, ApplyInitialRange, nullptr, 0, nullptr);
    if (thread) { CloseHandle(thread); Log("Waiting for active frontend design data; executable code unchanged."); }
    else Log("Could not start data worker; no update.");
    return TRUE;
}
}
extern "C" __declspec(dllexport) void InitializeASI() { InitOnceExecuteOnce(&once, Initialize, nullptr, nullptr); }
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(instance);
    return TRUE;
}
