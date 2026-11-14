#pragma once
#include <atomic>
#include <cwchar>
#include "qol_settings_api.h"
#include "qol_menu_labels.hpp"
#include "update_menu.hpp"
#include "driving_camera_height.hpp"
namespace qolsettings {
struct Definition {const char* section;const char* key;int initial;int maximum;};
constexpr Definition definitions[QOL_SETTING_COUNT]={
    {"RoadNav","Enabled",1,1},{"DefaultMapZoom","Enabled",1,1},
    {"CutscenePause","Enabled",1,1},{"VoiceVolume","Enabled",1,1},
    {"VoiceVolume","GameplayPercent",100,400},{"VoiceVolume","CutscenePercent",100,1600},
    {"Compass","Enabled",1,1},{"RoadNav","DestinationColours",1,1},{"RoadGPSLine","Enabled",1,1},{"Camera","TaxiRearFOV",70,120},
    {"RoadGPSLine","ViewBendStrengthPercent",100,100},
    {"RoadGPSLine","ShowPoliceEscape",1,1},{"RoadGPSLine","ShowMission",1,1},
    {"RoadGPSLine","ShowFriend",1,1},{"RoadGPSLine","ShowEnemy",1,1},{"RoadGPSLine","ShowCustom",1,1},
    {"Updates","CheckForUpdates",0,1},{"Camera","DrivingHeightCm",0,200}
};
std::atomic<int> values[QOL_SETTING_COUNT];
char path[MAX_PATH]{};
SRWLOCK writeLock=SRWLOCK_INIT;
INIT_ONCE initialized=INIT_ONCE_STATIC_INIT;
std::atomic<unsigned> menuRows{0};
void Load(){
    for(unsigned i=0;i<QOL_SETTING_COUNT;++i){const auto& d=definitions[i];
        const int read=static_cast<int>(GetPrivateProfileIntA(d.section,d.key,d.initial,path));
        values[i].store(read>=0&&read<=d.maximum?read:d.initial);
    }
    values[QOL_DRIVING_CAMERA_HEIGHT].store(qolcamera::presetHeight(qolcamera::heightPreset(values[QOL_DRIVING_CAMERA_HEIGHT].load())));
    // Legacy Flat and Twisted both become On; Off remains Off.
    if(values[QOL_GPS_LINE].load())values[QOL_GPS_VIEW_BEND].store(100);
}
BOOL CALLBACK Initialize(PINIT_ONCE,void*,void**) {
    const DWORD size=GetModuleFileNameA(nullptr,path,MAX_PATH);
    if(!size||size>=MAX_PATH){path[0]=0;return TRUE;}
    char* slash=strrchr(path,'\\');if(!slash){path[0]=0;return TRUE;}
    const auto left=MAX_PATH-static_cast<std::size_t>(slash-path+1);
    if(left<sizeof("GTAIV-QoL.ini")){path[0]=0;return TRUE;}
    strcpy_s(slash+1,left,"GTAIV-QoL.ini");
    Load();
    return TRUE;
}
void Ensure(){InitOnceExecuteOnce(&initialized,Initialize,nullptr,nullptr);}
int32_t __cdecl Get(uint32_t id){Ensure();return id<QOL_SETTING_COUNT?values[id].load():-1;}
int32_t __cdecl Set(uint32_t id,int32_t value){
    Ensure();if(id>=QOL_SETTING_COUNT||value<0||value>definitions[id].maximum||!path[0])return 0;
    char text[16];sprintf_s(text,"%d",value);
    AcquireSRWLockExclusive(&writeLock);
    const bool ok=WritePrivateProfileStringA(definitions[id].section,definitions[id].key,text,path)!=0;
    if(ok)values[id].store(value);
    ReleaseSRWLockExclusive(&writeLock);return ok?1:0;
}
void __cdecl MenuReady(uint32_t rows){menuRows.store(rows);}
const QolSettingsApi api{sizeof(QolSettingsApi),1,Get,Set,MenuReady,nullptr,nullptr};

// The verified menu host has one reserved, never-shown gameplay-voice row.
// Its UI-only adapter maps that row to Road Line On/Off; the normal settings
// ABI and reserved gameplay-voice value stay unchanged for existing clients.
// Appended UI rows 7..11 map to the five new IDs 11..15.
unsigned UiId(unsigned id){return id==13?QOL_DRIVING_CAMERA_HEIGHT:id>=7&&id<13?id+4:id==3?QOL_TAXI_FOV:id==4?QOL_GPS_LINE:id;}
int32_t __cdecl UiGet(uint32_t id){
    if(id==13)return qolcamera::heightPreset(Get(QOL_DRIVING_CAMERA_HEIGHT));
    if(id==QOL_GAMEPLAY_VOICE)return Get(QOL_GPS_LINE)?1:0;
    return Get(UiId(id));
}
int32_t __cdecl UiSet(uint32_t id,int32_t value){
    if(id==13){if(value<0||value>2)return 0;return Set(QOL_DRIVING_CAMERA_HEIGHT,qolcamera::presetHeight(value));}
    if(id>=7&&id<12&&!Get(QOL_GPS_LINE))return 0; // Disabled children retain their own choices.
    if(id==QOL_GAMEPLAY_VOICE){
        if(value<0||value>1)return 0;
        if(value&&!Set(QOL_GPS_VIEW_BEND,100))return 0;
        if(!Set(QOL_GPS_LINE,value?1:0))return 0;
        return WritePrivateProfileStringA("RoadGPSLine","ViewBendMenuVersion","2",path)?1:0;
    }
    if(id==QOL_VOCAL_SLIDERS&&value>=0&&value<40)value=40;
    const auto result=Set(UiId(id),value);
    if(result&&id==QOL_CUTSCENE_VOICE)return Set(QOL_VOCAL_SLIDERS,1);return result;
}
void __cdecl UiReady(uint32_t rows){unsigned actual=0;for(unsigned id=0;id<14;++id)if(rows&(1u<<id))actual|=1u<<UiId(id);MenuReady(actual);}
int32_t __cdecl UiLabel(const char* key,wchar_t* text,uint32_t capacity){
    const auto dynamic=key&&std::strcmp(key,"QOL_UPDATES")==0?qolupdatemenu::label(Get(QOL_CHECK_UPDATES)!=0):std::wstring{};
    const auto label=dynamic.empty()?qolmenu::label(key,Get(QOL_GPS_LINE)!=0):dynamic.c_str();
    if(!label||!text)return 0;
    const auto length=std::wcslen(label);if(length>=capacity)return 0;
    std::memcpy(text,label,(length+1)*sizeof(wchar_t));return 1;
}
using MenuRefresh=void(__cdecl*)();
std::atomic<MenuRefresh> refreshMenu{nullptr};
void __cdecl SetMenuRefresh(MenuRefresh callback){refreshMenu.store(callback);}
void RefreshMenu(bool paused){
    static DWORD checked=0;static std::wstring previous;
    const auto now=GetTickCount();if(checked&&now-checked<250)return;checked=now;
    const auto text=qolupdatemenu::label(Get(QOL_CHECK_UPDATES)!=0);
    if(text!=previous){previous=text;if(paused)if(auto refresh=refreshMenu.load())refresh();}
}
const QolSettingsApi uiApi{sizeof(QolSettingsApi),1,UiGet,UiSet,UiReady,UiLabel,SetMenuRefresh};
}
