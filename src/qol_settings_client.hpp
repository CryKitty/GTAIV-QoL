#pragma once
#include "qol_settings_api.h"
namespace qolclient {
inline const QolSettingsApi* Api() {
    auto module=GetModuleHandleA("GTAIV-QoL.asi");
    auto get=module?reinterpret_cast<QolGetSettingsApi>(GetProcAddress(module,"QoLSettings_GetApi")):nullptr;
    const auto api=get?get(1):nullptr;
    return api && api->version==1 && api->size>=sizeof(QolSettingsApi) && api->Get && api->Set ? api:nullptr;
}
inline int Get(unsigned id,int fallback) {const auto api=Api();const int value=api?api->Get(id):-1;return value>=0?value:fallback;}
inline bool Enabled(unsigned id,bool fallback=true) {return Get(id,fallback?1:0)!=0;}
}
