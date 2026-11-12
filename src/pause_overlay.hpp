#pragma once
#include "pause_blur_shader.hpp"
#include "pause_label.hpp"
namespace pauseoverlay {
volatile LONG active=0,generation=0,callbacks=0;
bool enabled=true;
void Set(bool value){const LONG requested=value&&enabled?1:0;if(InterlockedExchange(&active,requested)!=requested)InterlockedIncrement(&generation);}
template<class T> void Release(T*& resource){if(resource){resource->Release();resource=nullptr;}}
class Renderer final:public ID3DDeviceHook {
    SRWLOCK resources=SRWLOCK_INIT;
    IDirect3DTexture9* snapshot=nullptr;IDirect3DTexture9* blur[2]{};IDirect3DTexture9* label=nullptr;
    IDirect3DPixelShader9* shader=nullptr;
    UINT width=0,height=0;D3DFORMAT format=D3DFMT_UNKNOWN;LONG captured=-1;
    bool rendered=false,failed=false;
    void Clear(){Release(snapshot);Release(blur[0]);Release(blur[1]);Release(label);Release(shader);width=height=0;captured=-1;}
    bool MakeLabel(IDirect3DDevice9* device){
        if(FAILED(device->CreateTexture(pausedLabelWidth,pausedLabelHeight,1,D3DUSAGE_DYNAMIC,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&label,nullptr)))return false;
        D3DLOCKED_RECT lock{};if(FAILED(label->LockRect(0,&lock,nullptr,D3DLOCK_DISCARD)))return false;
        for(unsigned y=0;y<pausedLabelHeight;++y){auto dest=reinterpret_cast<DWORD*>(static_cast<unsigned char*>(lock.pBits)+y*lock.Pitch);for(unsigned x=0;x<pausedLabelWidth;++x)dest[x]=(static_cast<DWORD>(pausedLabelAlpha[y*pausedLabelWidth+x])<<24)|0x00ffffffu;}
        label->UnlockRect(0);return true;
    }
    bool Allocate(IDirect3DDevice9* device,const D3DSURFACE_DESC& desc){
        if(snapshot&&blur[0]&&blur[1]&&label&&shader&&width==desc.Width&&height==desc.Height&&format==desc.Format)return true;
        Clear();width=desc.Width;height=desc.Height;format=desc.Format;
        if(FAILED(device->CreateTexture(width,height,1,D3DUSAGE_RENDERTARGET,format,D3DPOOL_DEFAULT,&snapshot,nullptr)))return false;
        const UINT bw=width/4?width/4:1,bh=height/4?height/4:1;
        for(auto& texture:blur)if(FAILED(device->CreateTexture(bw,bh,1,D3DUSAGE_RENDERTARGET,format,D3DPOOL_DEFAULT,&texture,nullptr)))return false;
        return SUCCEEDED(device->CreatePixelShader(pauseBlurShader,&shader))&&MakeLabel(device);
    }
    struct Vertex {float x,y,z,rhw;DWORD color;float u,v;};
    static HRESULT Quad(IDirect3DDevice9* device,IDirect3DTexture9* texture,float x,float y,float w,float h,DWORD color=0xffffffffu){
        const Vertex vertices[]={{x-0.5f,y-0.5f,0,1,color,0,0},{x+w-0.5f,y-0.5f,0,1,color,1,0},{x-0.5f,y+h-0.5f,0,1,color,0,1},{x+w-0.5f,y+h-0.5f,0,1,color,1,1}};
        device->SetTexture(0,texture);return device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,vertices,sizeof(Vertex));
    }
    static void State(IDirect3DDevice9* device,UINT w,UINT h){
        const D3DVIEWPORT9 viewport{0,0,w,h,0,1};device->SetViewport(&viewport);device->SetVertexShader(nullptr);device->SetPixelShader(nullptr);device->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX1);
        device->SetStreamSourceFreq(0,1);device->SetRenderState(D3DRS_CLIPPLANEENABLE,0);device->SetRenderState(D3DRS_VERTEXBLEND,D3DVBF_DISABLE);device->SetRenderState(D3DRS_INDEXEDVERTEXBLENDENABLE,FALSE);device->SetRenderState(D3DRS_ZENABLE,FALSE);device->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);device->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);device->SetRenderState(D3DRS_STENCILENABLE,FALSE);device->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);device->SetRenderState(D3DRS_LIGHTING,FALSE);device->SetRenderState(D3DRS_FOGENABLE,FALSE);device->SetRenderState(D3DRS_CLIPPING,TRUE);device->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID);
        device->SetRenderState(D3DRS_COLORWRITEENABLE,0xf);device->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);device->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,FALSE);device->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD);device->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);device->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
        device->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_MODULATE);device->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE);device->SetTextureStageState(0,D3DTSS_COLORARG2,D3DTA_DIFFUSE);device->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_MODULATE);device->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE);device->SetTextureStageState(0,D3DTSS_ALPHAARG2,D3DTA_DIFFUSE);device->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,0);device->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE);device->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE);device->SetTextureStageState(1,D3DTSS_ALPHAOP,D3DTOP_DISABLE);
        device->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);device->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);device->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);device->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);device->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);device->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE);
    }
    bool Pass(IDirect3DDevice9* device,IDirect3DTexture9* source,IDirect3DTexture9* target,float dx,float dy){
        IDirect3DSurface9* surface=nullptr;D3DSURFACE_DESC desc{};if(FAILED(target->GetSurfaceLevel(0,&surface)))return false;surface->GetDesc(&desc);
        device->SetTexture(0,nullptr);const HRESULT set=device->SetRenderTarget(0,surface);surface->Release();if(FAILED(set))return false;
        State(device,desc.Width,desc.Height);
        if(dx!=0||dy!=0){device->SetPixelShader(shader);const float step[]={dx,dy,0,0};device->SetPixelShaderConstantF(0,step,1);}
        return SUCCEEDED(Quad(device,source,0,0,static_cast<float>(desc.Width),static_cast<float>(desc.Height)));
    }
public:
    void OnCreateDevice(IDirect3DDevice9*,D3DPRESENT_PARAMETERS*) override{}
    void OnResetDevice(IDirect3DDevice9*,D3DPRESENT_PARAMETERS*) override{AcquireSRWLockExclusive(&resources);Clear();failed=false;ReleaseSRWLockExclusive(&resources);}
    void OnLostDevice(IDirect3DDevice9*) override{AcquireSRWLockExclusive(&resources);Clear();ReleaseSRWLockExclusive(&resources);}
    void OnDestroyDevice(IDirect3DDevice9*) override{AcquireSRWLockExclusive(&resources);Clear();ReleaseSRWLockExclusive(&resources);}
    void OnRender(IDirect3DDevice9* device) override{
        // ScriptHook can dispatch OnRender from EndScene. When the service calls
        // us outside a scene, our own EndScene can synchronously dispatch us
        // again. Reject that re-entry before taking the nonrecursive GPU lock.
        static thread_local bool inside=false;
        if(inside)return;
        inside=true;
        struct RenderScope {bool& flag;~RenderScope(){flag=false;}} scope{inside};
        if(InterlockedIncrement(&callbacks)==1)Log("pause overlay render callback active");
        if(!InterlockedCompareExchange(&active,0,0)){AcquireSRWLockExclusive(&resources);captured=-1;ReleaseSRWLockExclusive(&resources);return;}
        if(!device||device->TestCooperativeLevel()!=D3D_OK)return;
        AcquireSRWLockExclusive(&resources);
        IDirect3DSurface9* back=nullptr;IDirect3DStateBlock9* saved=nullptr;IDirect3DSurface9* targets[4]{};IDirect3DSurface9* depth=nullptr;
        bool ok=false,begun=false;D3DSURFACE_DESC desc{};
        if(SUCCEEDED(device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back))&&SUCCEEDED(back->GetDesc(&desc))&&SUCCEEDED(device->CreateStateBlock(D3DSBT_ALL,&saved))&&SUCCEEDED(saved->Capture())){
            for(DWORD i=0;i<4;++i)device->GetRenderTarget(i,&targets[i]);device->GetDepthStencilSurface(&depth);
            if(Allocate(device,desc)){
                const LONG now=InterlockedCompareExchange(&generation,0,0);
                bool prepared=captured==now;
                if(!prepared){IDirect3DSurface9* destination=nullptr;if(SUCCEEDED(snapshot->GetSurfaceLevel(0,&destination))){prepared=SUCCEEDED(device->StretchRect(back,nullptr,destination,nullptr,D3DTEXF_NONE));destination->Release();}}
                begun=SUCCEEDED(device->BeginScene()); // service may already be inside a scene
                device->SetDepthStencilSurface(nullptr);for(DWORD i=1;i<4;++i)device->SetRenderTarget(i,nullptr);
                if(prepared&&captured!=now){
                    prepared=Pass(device,snapshot,blur[0],0,0);const float dx=4.0f/static_cast<float>(width),dy=4.0f/static_cast<float>(height);
                    for(unsigned i=0;i<2&&prepared;++i)prepared=Pass(device,blur[0],blur[1],dx,0)&&Pass(device,blur[1],blur[0],0,dy);
                    if(prepared)captured=now;
                }
                device->SetTexture(0,nullptr);device->SetRenderTarget(0,back);State(device,width,height);
                if(prepared)ok=SUCCEEDED(Quad(device,blur[0],0,0,static_cast<float>(width),static_cast<float>(height)));
                device->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);
                device->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG2);device->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG2);
                Quad(device,nullptr,0,0,static_cast<float>(width),static_cast<float>(height),0x40000000u);
                device->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_MODULATE);device->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_MODULATE);
                const float scale=static_cast<float>(height)*0.05f/static_cast<float>(pausedLabelHeight),w=static_cast<float>(pausedLabelWidth)*scale,h=static_cast<float>(pausedLabelHeight)*scale,x=static_cast<float>(width)*0.96f-w,y=static_cast<float>(height)*0.95f-h;
                if(label){Quad(device,label,x+2,y+2,w,h,0xb0000000u);Quad(device,label,x,y,w,h,0xf2ffffffu);}
            }
            if(begun)device->EndScene();
            device->SetTexture(0,nullptr);for(DWORD i=0;i<4;++i)if(targets[i]||i!=0)device->SetRenderTarget(i,targets[i]);device->SetDepthStencilSurface(depth);saved->Apply();
        }
        for(auto& target:targets)Release(target);Release(depth);Release(saved);Release(back);
        if(ok&&!rendered){rendered=true;Log("pause overlay rendered: Gaussian blur and bottom-right Pricedown Paused label");}
        if(!ok&&!failed){failed=true;Log("pause overlay could not complete blur; pause/audio behavior remains active");}
        ReleaseSRWLockExclusive(&resources);
    }
};
Renderer renderer;
void Register(){
    if(!enabled)return;
    using Request=void*(__cdecl*)(const char*);auto module=GetModuleHandleA("ScriptHook.dll");auto request=module?reinterpret_cast<Request>(GetProcAddress(module,"?RequestService@ScriptHookManager@@SAPAVIService@@PBD@Z")):nullptr;
    auto service=request?static_cast<ID3DHookService*>(request("D3DHook")):nullptr;
    if(!service){Log("pause overlay unavailable: ScriptHook D3DHook service missing");return;}
    // Loading a save restarts ScriptHook services. Rebind at each script start,
    // removing an existing registration first to avoid duplicate rendering.
    service->RemoveDeviceHook(&renderer);service->AddDeviceHook(&renderer);Log("pause overlay registered with ScriptHook D3DHook service");
}
}
