#include "../src/map_data_experimental.cpp"
#include <cassert>
struct ReloadScenario { mapzoom::Pair* first; mapzoom::Pair* second; std::uint32_t* slot; };
DWORD WINAPI ReloadTable(LPVOID parameter) {
    auto scenario = static_cast<ReloadScenario*>(parameter);
    Sleep(350);
    scenario->first[63].x = 800.0f; // parser restores data in the same allocation
    Sleep(200);
    *scenario->slot = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(scenario->second));
    return 0;
}
int main() {
    assert(!mapzoom::ValidPercent(99) && mapzoom::ValidPercent(150) && !mapzoom::ValidPercent(401));
    mapzoom::Pair rows[180]{};
    rows[63] = {800.0f, 0.0f}; rows[64] = {1600.0f, 0.0f}; rows[65] = {2.0f, 4.0f};
    assert(mapzoom::ExpectedMapRows(rows+63));
    rows[64].x = 900.0f; assert(!mapzoom::ExpectedMapRows(rows+63)); rows[64].x = 1600.0f;
    std::uint32_t table = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(rows));
    settings.slot = &table; settings.percent = 150; settings.monitor = false;
    assert(ApplyInitialRange(nullptr) == 0);
    assert(rows[63].x > 533.33f && rows[63].x < 533.34f && rows[63].y == 0.0f);
    assert(rows[62].x == 0.0f && rows[64].x == 1600.0f && rows[65].x == 2.0f && rows[65].y == 4.0f);
    assert(!mapzoom::ExpectedMapRows(rows+63)); // already-modified table cannot compound
    assert(mapzoom::InitialRange(100) == 800.0f);
    mapzoom::Pair second[180]{};
    second[63] = {800.0f, 0.0f}; second[64] = {1600.0f, 0.0f}; second[65] = {2.0f, 4.0f};
    rows[63].x = 800.0f;
    ReloadScenario scenario{rows, second, &table};
    settings.monitor = true; settings.attempts = 12;
    HANDLE reload = CreateThread(nullptr, 0, ReloadTable, &scenario, 0, nullptr);
    assert(reload);
    ApplyInitialRange(nullptr);
    WaitForSingleObject(reload, INFINITE); CloseHandle(reload);
    assert(rows[63].x == mapzoom::InitialRange(150));
    assert(second[63].x == mapzoom::InitialRange(150));
    assert(second[64].x == 1600.0f && second[65].y == 4.0f);
    unsigned char scratch[8]{};
    assert(!Read(nullptr, scratch, sizeof(scratch)));
    auto page = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READONLY);
    assert(page && !Writable(page, 4));
    VirtualFree(page, 0, MEM_RELEASE);
    return 0;
}
