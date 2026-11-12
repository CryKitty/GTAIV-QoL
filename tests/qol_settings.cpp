#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cassert>
#include "../src/qol_settings.hpp"
int main(){
    qolsettings::Ensure();char temp[MAX_PATH]{},file[MAX_PATH]{};assert(GetTempPathA(MAX_PATH,temp));assert(GetTempFileNameA(temp,"qol",0,file));strcpy_s(qolsettings::path,file);
    for(unsigned id=0;id<4;++id){assert(qolsettings::api.Set(id,0));assert(qolsettings::api.Get(id)==0);assert(qolsettings::api.Set(id,1));assert(qolsettings::api.Get(id)==1);}
    assert(qolsettings::api.Set(QOL_COMPASS,0));assert(qolsettings::api.Get(QOL_COMPASS)==0);
    assert(qolsettings::api.Set(QOL_COMPASS,1));assert(GetPrivateProfileIntA("Compass","Enabled",0,file)==1);
    assert(!qolsettings::api.Set(QOL_COMPASS,2));
    assert(qolsettings::api.Set(QOL_ROUTE_COLOURS,0));assert(qolsettings::api.Get(QOL_ROUTE_COLOURS)==0);
    assert(qolsettings::api.Set(QOL_ROUTE_COLOURS,1));assert(GetPrivateProfileIntA("RoadNav","DestinationColours",0,file)==1);
    assert(!qolsettings::api.Set(QOL_ROUTE_COLOURS,2));
    assert(qolsettings::api.Get(QOL_GPS_LINE)==1);
    assert(qolsettings::api.Get(QOL_GPS_VIEW_BEND)==100);
    assert(qolsettings::uiApi.Set(QOL_GAMEPLAY_VOICE,1));
    assert(qolsettings::api.Get(QOL_GPS_LINE)==1&&qolsettings::uiApi.Get(QOL_GAMEPLAY_VOICE)==1);
    assert(GetPrivateProfileIntA("RoadGPSLine","Enabled",0,file)==1);
    assert(qolsettings::api.Get(QOL_GPS_VIEW_BEND)==100); // On always twists.
    assert(!qolsettings::uiApi.Set(QOL_GAMEPLAY_VOICE,2));
    assert(GetPrivateProfileIntA("RoadGPSLine","ViewBendMenuVersion",0,file)==2);
    assert(qolsettings::uiApi.Set(QOL_GAMEPLAY_VOICE,0));
    assert(qolsettings::uiApi.Get(QOL_GAMEPLAY_VOICE)==0);
    assert(qolsettings::uiApi.Set(QOL_GAMEPLAY_VOICE,1));
    assert(qolsettings::uiApi.Get(QOL_GAMEPLAY_VOICE)==1&&qolsettings::api.Get(QOL_GPS_VIEW_BEND)==100);
    for(int legacyStrength=0;legacyStrength<=100;legacyStrength+=100){
        assert(WritePrivateProfileStringA("RoadGPSLine","Enabled","1",file));
        assert(WritePrivateProfileStringA("RoadGPSLine","ViewBendStrengthPercent",legacyStrength?"100":"0",file));
        qolsettings::Load();assert(qolsettings::uiApi.Get(QOL_GAMEPLAY_VOICE)==1&&qolsettings::api.Get(QOL_GPS_VIEW_BEND)==100);
    }
    assert(qolsettings::uiApi.Set(QOL_GAMEPLAY_VOICE,0));qolsettings::Load();assert(qolsettings::uiApi.Get(QOL_GAMEPLAY_VOICE)==0);
    assert(qolsettings::uiApi.Set(QOL_GAMEPLAY_VOICE,1));
    assert(qolsettings::api.Get(QOL_ROADNAV)==1); // Independent comparison toggles.
    assert(!qolsettings::api.Set(QOL_GPS_LINE,2));
    assert(qolsettings::uiApi.Set(QOL_VOCAL_SLIDERS,85));assert(qolsettings::api.Get(QOL_TAXI_FOV)==85&&qolsettings::api.Get(QOL_VOCAL_SLIDERS)==1);
    assert(qolsettings::uiApi.Set(QOL_VOCAL_SLIDERS,120));assert(qolsettings::api.Get(QOL_TAXI_FOV)==120);
    assert(!qolsettings::uiApi.Set(QOL_VOCAL_SLIDERS,121));
    assert(qolsettings::uiApi.Set(QOL_VOCAL_SLIDERS,39));assert(qolsettings::api.Get(QOL_TAXI_FOV)==40);
    assert(qolsettings::api.Set(QOL_CUTSCENE_VOICE,1600));
    qolsettings::uiApi.MenuReady(0x7f);assert((qolsettings::menuRows.load()&(1u<<QOL_GPS_LINE))!=0);
    assert(qolsettings::api.Set(QOL_GAMEPLAY_VOICE,100));assert(qolsettings::api.Get(QOL_GAMEPLAY_VOICE)==100&&qolsettings::api.Get(QOL_GPS_LINE)==1);assert(qolsettings::api.Set(QOL_CUTSCENE_VOICE,200));
    assert(GetPrivateProfileIntA("VoiceVolume","CutscenePercent",0,file)==200);
    assert(!qolsettings::api.Set(QOL_CUTSCENE_VOICE,1601));assert(!qolsettings::api.Set(QOL_ROADNAV,2));assert(!qolsettings::api.Set(QOL_MAP_AUTOZOOM,-1));assert(!qolsettings::api.Set(99,1));assert(qolsettings::api.Get(99)==-1);
    assert(qolsettings::api.Get(QOL_CUTSCENE_VOICE)==200);
    strcpy_s(qolsettings::path,"Z:\\qol-test-nonexistent-folder\\settings.ini");assert(!qolsettings::api.Set(QOL_ROADNAV,0));assert(qolsettings::api.Get(QOL_ROADNAV)==1);
    qolsettings::api.MenuReady(63);assert(qolsettings::menuRows.load()==63);DeleteFileA(file);return 0;
}
