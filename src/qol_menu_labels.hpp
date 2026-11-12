#pragma once
#include <cstring>
namespace qolmenu {
inline const wchar_t* label(const char* key,bool master){
    if(!key)return nullptr;
    const char* keys[]={"QOL_ESCAPE","QOL_MISSION","QOL_FRIEND","QOL_ENEMY","QOL_CUSTOM"};
    const wchar_t* enabled[]={L"    Police Escape Line",L"    Mission Line",L"    Friend Line",L"    Enemy Line",L"    Custom Line"};
    // Native GTA text colour controls: grey while disabled, then reset so the
    // next option/value keeps its own palette. Leading spaces express nesting.
    const wchar_t* disabled[]={L"    ~c~Police Escape Line~s~",L"    ~c~Mission Line~s~",L"    ~c~Friend Line~s~",L"    ~c~Enemy Line~s~",L"    ~c~Custom Line~s~"};
    for(unsigned i=0;i<5;++i)if(std::strcmp(key,keys[i])==0)return master?enabled[i]:disabled[i];
    return nullptr;
}
}
