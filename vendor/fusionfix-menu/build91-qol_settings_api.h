#pragma once
#include <stdint.h>

// Versioned C boundary. No STL objects, ownership transfer or exceptions cross DLLs.
enum QolSettingId {
    QOL_ROADNAV = 0, QOL_MAP_AUTOZOOM = 1, QOL_CUTSCENE_PAUSE = 2,
    QOL_VOCAL_SLIDERS = 3, QOL_GAMEPLAY_VOICE = 4, QOL_CUTSCENE_VOICE = 5,
    QOL_COMPASS = 6, QOL_SETTING_COUNT = 7
};
struct QolSettingsApi {
    uint32_t size;
    uint32_t version;
    int32_t (__cdecl* Get)(uint32_t id);
    int32_t (__cdecl* Set)(uint32_t id, int32_t value);
    void (__cdecl* MenuReady)(uint32_t registeredRows);
};
typedef const QolSettingsApi* (__cdecl* QolGetSettingsApi)(uint32_t version);
