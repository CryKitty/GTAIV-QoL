#pragma once
#include "d3d_hook_service.hpp"
#include "../../../src/compass_overlay.hpp"
namespace compass {
struct NoticeFrame {unsigned alpha=0;DWORD at=0;std::uintptr_t frontend=0;};
class NoticeRenderer final:public ID3DDeviceHook {
    SRWLOCK lock_=SRWLOCK_INIT;NoticeFrame frame_{};
    struct Vertex {float x,y,z,rhw;DWORD colour;};
public:
    void submit(NoticeFrame frame){AcquireSRWLockExclusive(&lock_);frame_=frame;ReleaseSRWLockExclusive(&lock_);}
    void clear(){submit({});}
    void OnCreateDevice(ID3DDevice*,PresentParameters*)override{}
    void OnResetDevice(ID3DDevice*,PresentParameters*)override{clear();}
    void OnLostDevice(ID3DDevice*)override{clear();}
    void OnDestroyDevice(ID3DDevice*)override{clear();}
    void OnRender(ID3DDevice* device)override{
        AcquireSRWLockShared(&lock_);const auto frame=frame_;ReleaseSRWLockShared(&lock_);
        if(!frame.alpha||GetTickCount()-frame.at>250||!frame.frontend)return;
        unsigned char frontend=1;SIZE_T bytes=0;
        if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(frame.frontend),&frontend,1,&bytes)||bytes!=1||frontend)return;
        D3DVIEWPORT9 viewport{};if(FAILED(device->GetViewport(&viewport))||!viewport.Width||!viewport.Height)return;
        const auto box=messageEnvelope(static_cast<float>(viewport.Width)/viewport.Height);
        std::vector<Vertex> vertices;vertices.reserve(216);
        for(unsigned layer=0;layer<2;++layer){
            const auto mesh=envelopeMesh(box.x*viewport.Width+viewport.X,box.y*viewport.Height+viewport.Y,box.width*viewport.Width,box.height*viewport.Height,layer?frame.alpha:frame.alpha*3/4,box.height*viewport.Height*(layer?.065f:.20f));
            for(const auto& point:mesh)vertices.push_back({point.x-.5f,point.y-.5f,0,1,layer?D3DCOLOR_ARGB(point.alpha,235,237,225):D3DCOLOR_ARGB(point.alpha,0,0,0)});
        }
        IDirect3DStateBlock9* state=nullptr;if(FAILED(device->CreateStateBlock(D3DSBT_ALL,&state)))return;
        if(FAILED(state->Capture())){state->Release();return;}
        device->SetVertexShader(nullptr);device->SetPixelShader(nullptr);device->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE);device->SetTexture(0,nullptr);
        device->SetStreamSourceFreq(0,1);device->SetRenderState(D3DRS_ZENABLE,FALSE);device->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
        device->SetRenderState(D3DRS_LIGHTING,FALSE);device->SetRenderState(D3DRS_FOGENABLE,FALSE);device->SetRenderState(D3DRS_STENCILENABLE,FALSE);device->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
        device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);device->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID);device->SetRenderState(D3DRS_SHADEMODE,D3DSHADE_GOURAUD);
        device->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);device->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);device->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);device->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
        device->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD);device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,FALSE);device->SetRenderState(D3DRS_COLORWRITEENABLE,0xf);device->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);
        device->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);device->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE);device->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);device->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE);device->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE);
        device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,static_cast<UINT>(vertices.size()/3),vertices.data(),sizeof(Vertex));state->Apply();state->Release();
    }
};
}
