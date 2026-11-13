#pragma once
#include <stdint.h>
#include <stddef.h>

// Versioned C boundary. No STL objects, ownership transfer or exceptions cross DLLs.
enum QolSettingId {
    QOL_ROADNAV = 0, QOL_MAP_AUTOZOOM = 1, QOL_CUTSCENE_PAUSE = 2,
    QOL_VOCAL_SLIDERS = 3, QOL_GAMEPLAY_VOICE = 4, QOL_CUTSCENE_VOICE = 5,
    QOL_COMPASS = 6, QOL_ROUTE_COLOURS = 7, QOL_GPS_LINE = 8, QOL_TAXI_FOV = 9,
    QOL_GPS_VIEW_BEND = 10, QOL_LINE_ESCAPE = 11, QOL_LINE_MISSION = 12,
    QOL_LINE_FRIEND = 13, QOL_LINE_ENEMY = 14, QOL_LINE_CUSTOM = 15,
    QOL_CHECK_UPDATES = 16, QOL_DRIVING_CAMERA_HEIGHT = 17, QOL_SETTING_COUNT = 18
};
struct QolSettingsApi {
    uint32_t size;
    uint32_t version;
    int32_t (__cdecl* Get)(uint32_t id);
    int32_t (__cdecl* Set)(uint32_t id, int32_t value);
    void (__cdecl* MenuReady)(uint32_t registeredRows);
    // Optional additive tail; old version-1 hosts still use the first 20 bytes.
    int32_t (__cdecl* MenuLabel)(const char* key,wchar_t* text,uint32_t capacity);
    void (__cdecl* SetMenuRefresh)(void (__cdecl* callback)());
};
typedef const QolSettingsApi* (__cdecl* QolGetSettingsApi)(uint32_t version);
