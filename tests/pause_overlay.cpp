#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define SCRIPTHOOK_EXPORTS
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include <cassert>
#pragma warning(push)
#pragma warning(disable: 4251 4275)
#include "../vendor/scripthook/Services/D3DHook.h"
#pragma warning(pop)
bool overlayLogged=false;
void Log(const char* text){if(std::strstr(text,"pause overlay rendered"))overlayLogged=true;std::puts(text);}
#include "../src/pause_overlay.hpp"
#include "../vendor/minhook/include/MinHook.h"
using EndScene=HRESULT(WINAPI*)(IDirect3DDevice9*);
EndScene originalEnd=nullptr;
unsigned nestedCallbacks=0;
HRESULT WINAPI ScriptHookEnd(IDirect3DDevice9* device){
    ++nestedCallbacks;pauseoverlay::renderer.OnRender(device);return originalEnd(device);
}
std::vector<DWORD> Pixels(IDirect3DDevice9* d){
    IDirect3DSurface9* back=nullptr;assert(SUCCEEDED(d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back)));D3DSURFACE_DESC desc{};back->GetDesc(&desc);
    IDirect3DSurface9* copy=nullptr;assert(SUCCEEDED(d->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&copy,nullptr)));assert(SUCCEEDED(d->GetRenderTargetData(back,copy)));
    D3DLOCKED_RECT rect{};assert(SUCCEEDED(copy->LockRect(&rect,nullptr,D3DLOCK_READONLY)));std::vector<DWORD> out(static_cast<std::size_t>(desc.Width)*desc.Height);
    for(UINT y=0;y<desc.Height;++y)std::memcpy(out.data()+y*desc.Width,static_cast<const unsigned char*>(rect.pBits)+y*rect.Pitch,desc.Width*4);
    copy->UnlockRect();copy->Release();back->Release();return out;
}
void Save(const std::vector<DWORD>& pixels){
    BITMAPFILEHEADER file{};BITMAPINFOHEADER info{};info.biSize=sizeof(info);info.biWidth=800;info.biHeight=-450;info.biPlanes=1;info.biBitCount=32;info.biCompression=BI_RGB;info.biSizeImage=800*450*4;file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(info);file.bfSize=file.bfOffBits+info.biSizeImage;
    FILE* output=nullptr;assert(fopen_s(&output,"build/pause-overlay-preview.bmp","wb")==0&&output);std::fwrite(&file,sizeof(file),1,output);std::fwrite(&info,sizeof(info),1,output);std::fwrite(pixels.data(),4,pixels.size(),output);std::fclose(output);
}
int main(){
    using namespace pauseoverlay;
    HWND window=CreateWindowW(L"STATIC",L"QoL overlay test",WS_OVERLAPPEDWINDOW,0,0,800,450,nullptr,nullptr,GetModuleHandleA(nullptr),nullptr);assert(window);
    auto api=Direct3DCreate9(D3D_SDK_VERSION);assert(api);D3DPRESENT_PARAMETERS p{};p.Windowed=TRUE;p.SwapEffect=D3DSWAPEFFECT_DISCARD;p.hDeviceWindow=window;p.BackBufferWidth=800;p.BackBufferHeight=450;p.BackBufferFormat=D3DFMT_X8R8G8B8;
    IDirect3DDevice9* device=nullptr;D3DDEVTYPE kind=D3DDEVTYPE_HAL;
    HRESULT result=api->CreateDevice(D3DADAPTER_DEFAULT,kind,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&p,&device);
    if(FAILED(result)){kind=D3DDEVTYPE_REF;result=api->CreateDevice(D3DADAPTER_DEFAULT,kind,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&p,&device);}
    if(FAILED(result)){kind=D3DDEVTYPE_NULLREF;result=api->CreateDevice(D3DADAPTER_DEFAULT,kind,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&p,&device);}
    assert(SUCCEEDED(result)&&device);std::printf("D3D9 test device type=%d (NULLREF skips pixel assertions)\n",static_cast<int>(kind));
    renderer.OnCreateDevice(device,&p);renderer.OnResetDevice(device,&p);
    for(LONG y=0;y<450;y+=50)for(LONG x=0;x<800;x+=80){D3DRECT rect{x,y,x+80,y+50};assert(SUCCEEDED(device->Clear(1,&rect,D3DCLEAR_TARGET,((x/80+y/50)%2)?0xff215b9c:0xffd4a25c,1,0)));}
    const D3DVIEWPORT9 viewport{5,7,720,400,0.1f,0.9f};device->SetViewport(&viewport);device->SetRenderState(D3DRS_LIGHTING,TRUE);device->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);device->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT);const float constants[]={0.1f,0.2f,0.3f,0.4f};device->SetPixelShaderConstantF(0,constants,1);
    Set(true);assert(SUCCEEDED(device->BeginScene()));renderer.OnRender(device);assert(SUCCEEDED(device->EndScene()));assert(overlayLogged);
    D3DVIEWPORT9 restored{};device->GetViewport(&restored);assert(std::memcmp(&restored,&viewport,sizeof(viewport))==0);DWORD state=0;device->GetRenderState(D3DRS_LIGHTING,&state);assert(state==TRUE);device->GetRenderState(D3DRS_ALPHABLENDENABLE,&state);assert(state==FALSE);device->GetSamplerState(0,D3DSAMP_MINFILTER,&state);assert(state==D3DTEXF_POINT);float restoredConstants[4]{};device->GetPixelShaderConstantF(0,restoredConstants,1);assert(std::memcmp(constants,restoredConstants,sizeof(constants))==0);
    if(kind!=D3DDEVTYPE_NULLREF){
        const auto image=Pixels(device);Save(image);unsigned white=0;for(unsigned y=390;y<440;++y)for(unsigned x=660;x<780;++x){const DWORD pixel=image[y*800+x];const DWORD r=(pixel>>16)&255,g=(pixel>>8)&255,b=pixel&255;if(r>190&&g>190&&b>190)++white;}assert(white>30); // actual Pricedown Paused artwork near bottom right
        unsigned centerWhite=0;for(unsigned y=390;y<440;++y)for(unsigned x=300;x<500;++x){const DWORD px=image[y*800+x];if(((px>>16)&255)>190&&((px>>8)&255)>190&&(px&255)>190)++centerWhite;}assert(centerWhite==0);
        Set(false);const D3DVIEWPORT9 full{0,0,800,450,0,1};device->SetViewport(&full);
        assert(SUCCEEDED(device->Clear(0,nullptr,D3DCLEAR_TARGET,0xff204060,1,0)));renderer.OnRender(device);const auto plain=Pixels(device);assert((plain[420*800+400]&0x00ffffffu)==0x00204060u);
    }
    // Reproduce ScriptHook calling us again from our own EndScene. Without the
    // renderer re-entry guard this blocks forever on the SRW resource lock.
    Set(false);Set(true);assert(MH_Initialize()==MH_OK);void* trampoline=nullptr;
    void** table=*reinterpret_cast<void***>(device);void* endTarget=table[42];
    assert(MH_CreateHook(endTarget,reinterpret_cast<void*>(&ScriptHookEnd),&trampoline)==MH_OK);originalEnd=reinterpret_cast<EndScene>(trampoline);
    assert(MH_EnableHook(endTarget)==MH_OK);
    renderer.OnRender(device); // deliberately outside BeginScene/EndScene
    assert(nestedCallbacks==1);
    assert(MH_DisableHook(endTarget)==MH_OK&&MH_RemoveHook(endTarget)==MH_OK);
    // Input/tick responsiveness relies on render returning; it must still be
    // possible to draw another game frame and dismiss the overlay immediately.
    Set(false);assert(SUCCEEDED(device->BeginScene()));renderer.OnRender(device);assert(SUCCEEDED(device->EndScene()));
    Set(false);renderer.OnLostDevice(device);assert(SUCCEEDED(device->Reset(&p)));renderer.OnResetDevice(device,&p);assert(SUCCEEDED(device->Clear(0,nullptr,D3DCLEAR_TARGET,0xff304050,1,0)));Set(true);assert(SUCCEEDED(device->BeginScene()));renderer.OnRender(device);assert(SUCCEEDED(device->EndScene()));Set(false);renderer.OnDestroyDevice(device);
    device->Release();api->Release();DestroyWindow(window);return 0;
}

IService::~IService(){} // SDK service stub for standalone tests only.
