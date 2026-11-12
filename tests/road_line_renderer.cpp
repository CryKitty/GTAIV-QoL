#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define SCRIPTHOOK_EXPORTS
#include <windows.h>
#include <d3d9.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#pragma warning(push)
#pragma warning(disable: 4251 4275)
#include "../vendor/roadnav/src/road_line_renderer.hpp"
#pragma warning(pop)
#include "../vendor/roadnav/src/road_surface.hpp"
#include "../src/qol_settings.hpp"

namespace {
IDirect3DTexture9* depthTexture=nullptr;
bool ready=true,drawn=false;std::uint32_t clockNow=0;
std::array<std::uint32_t,4> sceneBuffers{};
std::array<std::uint32_t,14> sceneTarget{};
constexpr char sceneName[]="_DEFERRED_GBUFFER_3_";
std::uintptr_t LocateDepth(){return ready?reinterpret_cast<std::uintptr_t>(sceneBuffers.data()):0;}
DWORD WINAPI SceneTime(){return clockNow;}
struct TestDepth:RoadSceneDepth {
    explicit TestDepth(void(*log)(const char*)):RoadSceneDepth(log,&LocateDepth,&SceneTime){}
};
struct TestCamera {
    explicit TestCamera(void(*)(const char*)){}
    void invalidate(){}
    roadnav::RoadProjection acquire(const roadnav::CameraFrame&){return {};}
};
void Log(const char* text){if(std::strstr(text,"status=draw submitted"))drawn=true;std::puts(text);}
using Renderer=roadnav::RoadLineRendererWithScene<TestDepth,TestCamera>;
unsigned colouredPixels(IDirect3DDevice9* device){
    IDirect3DSurface9* back=nullptr;assert(SUCCEEDED(device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back)));
    D3DSURFACE_DESC desc{};back->GetDesc(&desc);IDirect3DSurface9* copy=nullptr;
    assert(SUCCEEDED(device->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&copy,nullptr)));
    assert(SUCCEEDED(device->GetRenderTargetData(back,copy)));D3DLOCKED_RECT rect{};
    assert(SUCCEEDED(copy->LockRect(&rect,nullptr,D3DLOCK_READONLY)));unsigned count=0;
    for(UINT y=0;y<desc.Height;++y){const auto* row=reinterpret_cast<const DWORD*>(static_cast<const unsigned char*>(rect.pBits)+y*rect.Pitch);
        for(UINT x=0;x<desc.Width;++x)if(row[x]&0x00ffffffu)++count;}
    copy->UnlockRect();copy->Release();back->Release();return count;
}
void draw(Renderer& renderer,IDirect3DDevice9* device,roadnav::LineFrame frame){
    drawn=false;assert(SUCCEEDED(device->Clear(0,nullptr,D3DCLEAR_TARGET,0xff000000,1,0)));
    const awareness::LineTypes types{qolsettings::api.Get(QOL_LINE_ESCAPE)!=0,qolsettings::api.Get(QOL_LINE_MISSION)!=0,
        qolsettings::api.Get(QOL_LINE_FRIEND)!=0,qolsettings::api.Get(QOL_LINE_ENEMY)!=0,qolsettings::api.Get(QOL_LINE_CUSTOM)!=0};
    if(types.visible(qolsettings::api.Get(QOL_GPS_LINE)!=0,frame.kind))renderer.submit(frame);else renderer.clear();
    assert(SUCCEEDED(device->BeginScene()));renderer.OnRender(device);assert(SUCCEEDED(device->EndScene()));
}
}
int main(){
    // Read the actual shipped defaults; never silently pass with zero routes.
    qolsettings::Ensure();char source[MAX_PATH]{},temp[MAX_PATH]{},file[MAX_PATH]{};
    assert(GetFullPathNameA("GTAIV-QoL.ini.example",MAX_PATH,source,nullptr));
    assert(GetTempPathA(MAX_PATH,temp)&&GetTempFileNameA(temp,"gps",0,file));assert(CopyFileA(source,file,FALSE));
    strcpy_s(qolsettings::path,file);qolsettings::Load();
    assert(qolsettings::api.Get(QOL_GPS_LINE)==1&&qolsettings::uiApi.Get(QOL_GAMEPLAY_VOICE)==1);
    HWND window=CreateWindowW(L"STATIC",L"Road line renderer test",WS_OVERLAPPEDWINDOW,0,0,800,450,nullptr,nullptr,GetModuleHandleA(nullptr),nullptr);assert(window);
    auto api=Direct3DCreate9(D3D_SDK_VERSION);assert(api);D3DPRESENT_PARAMETERS params{};
    params.Windowed=TRUE;params.SwapEffect=D3DSWAPEFFECT_DISCARD;params.hDeviceWindow=window;params.BackBufferWidth=800;params.BackBufferHeight=450;params.BackBufferFormat=D3DFMT_X8R8G8B8;
    IDirect3DDevice9* device=nullptr;D3DDEVTYPE kind=D3DDEVTYPE_HAL;
    HRESULT result=api->CreateDevice(0,kind,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&params,&device);
    if(FAILED(result)){kind=D3DDEVTYPE_REF;result=api->CreateDevice(0,kind,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&params,&device);}
    if(FAILED(result)){kind=D3DDEVTYPE_NULLREF;result=api->CreateDevice(0,kind,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&params,&device);}
    assert(SUCCEEDED(result)&&device);std::printf("Road line D3D9 device=%d; NULLREF skips pixel checks only\n",static_cast<int>(kind));
    assert(SUCCEEDED(device->CreateTexture(800,450,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&depthTexture,nullptr)));
    D3DLOCKED_RECT depth{};assert(SUCCEEDED(depthTexture->LockRect(0,&depth,nullptr,0)));
    for(unsigned y=0;y<450;++y)std::memset(static_cast<unsigned char*>(depth.pBits)+y*depth.Pitch,255,800*4);
    depthTexture->UnlockRect(0); // Far depth, so neither test route is occluded.
    sceneTarget[12]=reinterpret_cast<std::uint32_t>(sceneName);
    sceneTarget[13]=reinterpret_cast<std::uint32_t>(depthTexture);
    sceneBuffers[3]=reinterpret_cast<std::uint32_t>(sceneTarget.data());
    using roadnav::Vec3;const Vec3 player{0,0,1};
    const std::vector<Vec3> manual{{0,0,0},{0,30,0},{30,30,0},{60,30,0}};
    const std::vector<Vec3> escape{{0,0,0},{0,30,0},{-30,30,0},{-60,30,0}};
    auto primary=roadnav::roadLine(manual,player,8),police=roadnav::roadLine(escape,player,8);
    const auto ground=[](float,float,float){return 0.0f;};
    roadnav::fitRoadSurface(primary,player,{},ground);roadnav::fitRoadSurface(police,player,{},ground,&primary);
    assert(primary.visible()&&police.visible());
    for(int legacyMode:{2,1}){
        assert(WritePrivateProfileStringA("RoadGPSLine","Enabled","1",file));
        assert(WritePrivateProfileStringA("RoadGPSLine","ViewBendStrengthPercent",legacyMode==2?"100":"0",file));
        qolsettings::Load();assert(qolsettings::uiApi.Get(QOL_GAMEPLAY_VOICE)==1);
        assert(qolsettings::api.Get(QOL_GPS_VIEW_BEND)==100);
        assert(qolsettings::uiApi.Set(QOL_GAMEPLAY_VOICE,1));
        assert(GetPrivateProfileIntA("RoadGPSLine","ViewBendStrengthPercent",0,file)==100);
        for(auto routeKind:{awareness::RouteKind::Blue,awareness::RouteKind::Escape}){
            roadnav::LineFrame frame;frame.line=routeKind==awareness::RouteKind::Escape?police:primary;
            frame.at=GetTickCount();frame.player=player;frame.kind=routeKind;frame.camera.position={0,-6,6};frame.camera.rotation={-12,0,0};frame.camera.fov=70;
            frame.viewBend={qolsettings::api.Get(QOL_GPS_VIEW_BEND)/100.0f,75,1};
            Renderer renderer(Log);draw(renderer,device,frame);assert(drawn&&renderer.renderedFrames()==1);
            if(kind!=D3DDEVTYPE_NULLREF)assert(colouredPixels(device)>20);
            const awareness::RouteKind types[]={awareness::RouteKind::Escape,awareness::RouteKind::Mission,awareness::RouteKind::Friend,awareness::RouteKind::Enemy,awareness::RouteKind::Custom};
            for(unsigned off=0;off<5;++off){
                assert(qolsettings::api.Set(QOL_LINE_ESCAPE+off,0));
                for(unsigned type=0;type<5;++type){auto typed=frame;typed.kind=types[type];
                    draw(renderer,device,typed);
                    if(kind!=D3DDEVTYPE_NULLREF)assert((colouredPixels(device)>20)==(type!=off));
                }
                assert(qolsettings::api.Set(QOL_LINE_ESCAPE+off,1));
            }
            renderer.OnDestroyDevice(device);
            Renderer quiet(nullptr);draw(quiet,device,frame);assert(quiet.renderedFrames()==1);quiet.OnDestroyDevice(device);
            // Failed first lookup must recover when resources become available,
            // without a restart, and without a see-through fallback meanwhile.
            ready=false;clockNow=0;Renderer delayed(Log);draw(delayed,device,frame);assert(!drawn);
            ready=true;clockNow=100;draw(delayed,device,frame);assert(!drawn);
            clockNow=2000;draw(delayed,device,frame);
            assert(delayed.renderedFrames()==1);
            if(kind!=D3DDEVTYPE_NULLREF)assert(colouredPixels(device)>20);
            // Toggle invalidation must also discard the failed discovery latch.
            delayed.clear();ready=false;draw(delayed,device,frame);
            ready=true;delayed.clear();draw(delayed,device,frame);
            assert(delayed.renderedFrames()==2);
            if(kind!=D3DDEVTYPE_NULLREF)assert(colouredPixels(device)>20);
            assert(qolsettings::uiApi.Set(QOL_GAMEPLAY_VOICE,0));draw(delayed,device,frame);
            assert(delayed.renderedFrames()==2);
            if(kind!=D3DDEVTYPE_NULLREF)assert(colouredPixels(device)==0);
            assert(qolsettings::uiApi.Set(QOL_GAMEPLAY_VOICE,1));draw(delayed,device,frame);
            assert(delayed.renderedFrames()==3);
            if(kind!=D3DDEVTYPE_NULLREF)assert(colouredPixels(device)>20);
            delayed.OnDestroyDevice(device);
        }
    }
    SceneDiscoveryRetry retry;assert(retry.due(0xfffffff0u));assert(!retry.due(0x100u));assert(retry.due(0x800u));retry.reset();assert(retry.due(0x801u));
    depthTexture->Release();device->Release();api->Release();DestroyWindow(window);DeleteFileA(file);
    std::puts("Manual and police escape: shipped defaults, legacy Flat/Twisted to On, pixel draw, scene retry and Off/On recovery passed");
}
IService::~IService(){}
