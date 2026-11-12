#pragma once
#include <cstdint>
namespace cutscenepause {
// ScriptHook SDK ScriptingNatives.h hashes, not the game's alternate native IDs.
// Source: CryKitty/GTAIV-Road-Nav sdk/ScriptingNatives.h (Aru ScriptHook SDK).
struct NativeSpec { const char* name; std::uint32_t sdkHash; };
constexpr NativeSpec natives[]={
    {"PAUSE_GAME",0x6F32A4E2u},
    {"UNPAUSE_GAME",0x0BF2CD82u},
    {"PAUSE_SCRIPTED_CONVERSATION",0xE2C9C6F8u},
    {"HAS_CUTSCENE_LOADED",0xF9998582u},
    {"HAS_CUTSCENE_FINISHED",0x5DED14B4u},
    {"IS_NETWORK_GAME_RUNNING",0x1C1CF453u},
    {"IS_PAUSE_MENU_ACTIVE",0xD3600591u},
    {"GET_CUTSCENE_AUDIO_TIME_MS",0xCA56C61Du},
    {"GET_CUTSCENE_TIME",0x53F5B5ABu}
};
template<class ByHash,class ByName,class Valid>
void* Resolve(const NativeSpec& spec,ByHash byHash,ByName byName,Valid valid) {
    void* address=byHash(spec.sdkHash);
    if(valid(address))return address;
    address=byName(spec.name);
    return valid(address)?address:nullptr;
}
}
