#pragma once
#include "qol_settings_api.h"
namespace qolclient {
inline const QolSettingsApi* Api() {
    auto module=GetModuleHandleA("GTAIV-QoL.asi");
    auto get=module?reinterpret_cast<QolGetSettingsApi>(GetProcAddress(module,"QoLSettings_GetApi")):nullptr;
    const auto api=get?get(1):nullptr;
    return api && api->version==1 && api->size>=offsetof(QolSettingsApi,MenuLabel) && api->Get && api->Set ? api:nullptr;
}
// Called only from the ScriptHook game thread, never the background worker.
inline void RefreshMenu(bool paused){
    using Refresh=void(__cdecl*)(unsigned);
    const auto module=GetModuleHandleA("GTAIV-QoL.asi");
    const auto refresh=module?reinterpret_cast<Refresh>(GetProcAddress(module,"QoLSettings_RefreshMenu")):nullptr;
    if(refresh)refresh(paused?1u:0u);
}
inline void UpdateSession(bool active){
    using Session=void(__cdecl*)(unsigned);
    const auto module=GetModuleHandleA("GTAIV-QoL.asi");
    const auto session=module?reinterpret_cast<Session>(GetProcAddress(module,"QoLUpdates_Session")):nullptr;
    if(session)session(active?1u:0u);
}
inline int Get(unsigned id,int fallback) {const auto api=Api();const int value=api?api->Get(id):-1;return value>=0?value:fallback;}
inline bool Enabled(unsigned id,bool fallback=true) {return Get(id,fallback?1:0)!=0;}
}
