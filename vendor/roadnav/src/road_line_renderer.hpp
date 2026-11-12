#pragma once
#include "d3d_hook_service.hpp"
#include "road_line.hpp"
#include "scene_depth.hpp"
#include "scene_camera.hpp"
#include <cstdio>
#include <cstring>
#include <atomic>
#include "road_line_depth_shader.hpp"
#include "../../../src/navigation_awareness.hpp"
namespace roadnav {
struct LineFrame {RoadLine line{};CameraFrame camera{};DWORD at=0;Vec3 player{};awareness::RouteKind kind=awareness::RouteKind::Blue;bool colours=true;float nearClip=.15f,farClip=1500;float widthScale=1;RoadLineViewBend viewBend{};};
// Inject only the read-only scene providers in tests; geometry, projection,
// depth shader and draw submission remain the production implementation.
template<class Depth=RoadSceneDepth,class Camera=RoadSceneCamera>
class RoadLineRendererWithScene final:public ID3DDeviceHook {
    SRWLOCK lock_=SRWLOCK_INIT;LineFrame frame_{};void(*log_)(const char*)=nullptr;bool logged_=false;
    Depth sceneDepth_;Camera sceneCamera_;IDirect3DPixelShader9* occlusion_=nullptr;bool missingLogged_=false;
    std::atomic<bool> invalidateScene_{false};
    DWORD reportAt_=0;unsigned drawFrames_=0,emptyFrames_=0,outsideFrames_=0,depthMissingFrames_=0,authoredFrames_=0;
    void report(const char* status,const LineFrame& frame,unsigned vertices=0){
        if(!std::strcmp(status,"draw submitted"))++drawFrames_;
        else if(!std::strcmp(status,"no route geometry"))++emptyFrames_;
        else if(!std::strcmp(status,"geometry outside camera"))++outsideFrames_;
        else if(!std::strcmp(status,"scene depth unavailable"))++depthMissingFrames_;
        const auto now=GetTickCount();if(reportAt_&&now-reportAt_<5000)return;reportAt_=now;
        if(log_&&frame.kind==awareness::RouteKind::Escape&&std::strcmp(status,"draw submitted")){
            char reason[180];std::snprintf(reason,sizeof(reason),"Police escape suppressed (renderer): reason=%s points=%u age=%lums",status,static_cast<unsigned>(frame.line.points.size()),now-frame.at);log_(reason);
        }
        if(log_){char text[340];std::snprintf(text,sizeof(text),"Road GPS Line: status=%s kind=%u points=%u age=%lums submitted-vertices=%u frames(draw/empty/outside/depth/authored)=%u/%u/%u/%u/%u",status,static_cast<unsigned>(frame.kind),static_cast<unsigned>(frame.line.points.size()),now-frame.at,vertices,drawFrames_,emptyFrames_,outsideFrames_,depthMissingFrames_,authoredFrames_);log_(text);}
    }
    struct Vertex {float x,y,z,rhw;DWORD colour;float depth,padding;};
    // D3D callbacks own this scratch buffer; submit() never touches it.
    std::vector<Vertex> vertices_;
    std::vector<std::array<Vec3,2>> sections_;
    void reset(){clear();if(occlusion_){occlusion_->Release();occlusion_=nullptr;}missingLogged_=false;}
public:
    explicit RoadLineRendererWithScene(void(*log)(const char*)):log_(log),sceneDepth_(log),sceneCamera_(log){}
    unsigned renderedFrames()const{return drawFrames_;} // Render-thread diagnostic counter.
    void submit(LineFrame frame){AcquireSRWLockExclusive(&lock_);frame_=std::move(frame);ReleaseSRWLockExclusive(&lock_);}
    void clear(){submit({});invalidateScene_.store(true);}
    void OnCreateDevice(ID3DDevice*,PresentParameters*)override{reset();}
    void OnResetDevice(ID3DDevice*,PresentParameters*)override{reset();}
    void OnLostDevice(ID3DDevice*)override{reset();}
    void OnDestroyDevice(ID3DDevice*)override{reset();}
    void OnRender(ID3DDevice* device)override{
        // Scene providers are render-thread-owned. Script/toggle callbacks only
        // request invalidation; never race discovery or cached viewport reads.
        if(invalidateScene_.exchange(false)){sceneDepth_.invalidate();sceneCamera_.invalidate();missingLogged_=false;}
        AcquireSRWLockShared(&lock_);const auto f=frame_;ReleaseSRWLockShared(&lock_);
        if(!f.line.visible()){report("no route geometry",f);return;}
        if(GetTickCount()-f.at>1200){report("expired script snapshot",f);return;}
        if(device->TestCooperativeLevel()!=D3D_OK){report("device unavailable",f);return;}
        D3DVIEWPORT9 viewport{};if(FAILED(device->GetViewport(&viewport))){report("viewport unavailable",f);return;}
        auto rendered=sceneCamera_.acquire(f.camera);
        if(!rendered.valid)++authoredFrames_;
        const auto projectScene=[&](Vec3 p,ScreenPoint& point,float& depth){
            if(rendered.valid)return rendered.projectPoint(p,static_cast<float>(viewport.Width),static_cast<float>(viewport.Height),point,depth);
            depth=roadDepth(p,f.camera);return project(p,f.camera,static_cast<float>(viewport.Width),static_cast<float>(viewport.Height),point);
        };
        auto texture=sceneDepth_.acquire();
        if(!texture){if(!missingLogged_&&log_)log_("Road GPS Line: waiting for scene depth; see-through drawing disabled");missingLogged_=true;report("scene depth unavailable",f);return;}
        if(!occlusion_&&FAILED(device->CreatePixelShader(roadLineDepthShader,&occlusion_))){report("depth shader unavailable",f);texture->Release();return;}
        IDirect3DStateBlock9* state=nullptr;if(FAILED(device->CreateStateBlock(D3DSBT_ALL,&state))){report("render state unavailable",f);texture->Release();return;}
        if(FAILED(state->Capture())){report("render state capture failed",f);state->Release();texture->Release();return;}
        float nearClip=f.nearClip,farClip=f.farClip;
        // Match the depth texture's gameplay viewport. Constant register c209
        // can belong to a later HUD/post-process shader and must not set depth.
        rendered.clipPlanes(nearClip,farClip);
        const float params[]{nearClip,farClip,1.0f/viewport.Width,1.0f/viewport.Height};
        device->SetVertexShader(nullptr);device->SetPixelShader(occlusion_);device->SetPixelShaderConstantF(0,params,1);device->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX1);device->SetTexture(0,texture);
        device->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT);device->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT);device->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);device->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);device->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);device->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE);
        device->SetStreamSourceFreq(0,1);device->SetRenderState(D3DRS_ZENABLE,D3DZB_FALSE);device->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
        device->SetRenderState(D3DRS_DEPTHBIAS,0);device->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS,0);
        device->SetRenderState(D3DRS_LIGHTING,FALSE);device->SetRenderState(D3DRS_FOGENABLE,FALSE);device->SetRenderState(D3DRS_STENCILENABLE,FALSE);device->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
        device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);device->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID);device->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);device->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);
        device->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);device->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);device->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD);device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,FALSE);device->SetRenderState(D3DRS_COLORWRITEENABLE,0xf);
        device->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);device->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE);device->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);device->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE);device->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE);
        const auto lift=roadLineLift(f.line.points,f.line.bank,f.player);
        const float yaw=f.camera.rotation.z*.01745329252f;const Vec3 forward{-std::sin(yaw),std::cos(yaw),0};
        const bool joined=f.line.edges.empty()||f.line.edges.size()>=f.line.points.size();
        const auto rgb=awareness::routeColour(f.kind,f.colours,GetTickCount());bool drew=false;unsigned submitted=0;
        for(unsigned layer=0;layer<2;++layer){float along=0;auto& vertices=vertices_;vertices.clear();vertices.reserve(f.line.points.size()*6+12);
            const float width=(layer?.5292f:.8568f)*f.widthScale;
            if(joined)roadLineRenderSections(f.line,lift,width,forward,f.camera.position,f.viewBend,sections_);
            for(std::size_t i=1;i<f.line.points.size();++i){
                const auto a=f.line.points[i-1],b=f.line.points[i];if(distance2D(a,b)<.02f)continue;
                const float distance=distance2D(f.player,a);const float fade=std::clamp((distance-3)/4,0.0f,1.0f)*std::clamp((f.line.ahead-along)/12,0.0f,1.0f);along+=distance2D(a,b);
                const DWORD alpha=static_cast<DWORD>((layer?76:44)*fade);if(!alpha)continue;
                const auto sectionA=joined?sections_[i-1]:roadLineRenderSection(f.line,lift,i-1,width,forward,f.camera.position,f.viewBend,i<f.line.edges.size());
                const auto sectionB=joined?sections_[i]:roadLineRenderSection(f.line,lift,i,width,forward,f.camera.position,f.viewBend,i<f.line.edges.size());
                const std::array<Vec3,4> quad{{sectionA[0],sectionA[1],sectionB[1],sectionB[0]}};std::array<Vertex,4> v{};bool valid=true;
                for(unsigned n=0;n<4;++n){ScreenPoint point{};float d=0;const bool projected=projectScene(quad[n],point,d);
                    if(!projected||d<=std::max(.6f,nearClip)||std::fabs(point.x)>viewport.Width*4.0f||std::fabs(point.y)>viewport.Height*4.0f){valid=false;break;}
                    const DWORD colour=layer?D3DCOLOR_ARGB(alpha,rgb[0],rgb[1],rgb[2]):D3DCOLOR_ARGB(alpha,12,16,19);
                    v[n]={point.x+viewport.X,point.y+viewport.Y,0,1/d,colour,d,0};
                }
                if(valid)for(unsigned n:{0u,1u,2u,0u,2u,3u})vertices.push_back(v[n]);
            }
            if(f.line.endsAtDestination&&!f.line.points.empty()){
                constexpr float shape[6][2]={{-.60f,.65f},{-.05f,.65f},{.60f,0},{-.05f,-.65f},{-.60f,-.65f},{.05f,0}};
                constexpr unsigned triangles[12]={0,1,5,1,2,5,5,2,3,5,3,4};
                const auto end=f.line.points.back();
                const Vec3 right{std::cos(yaw),std::sin(yaw),0};const float scale=layer?.72f:.88f;
                const float fade=std::clamp((distance2D(f.player,end)-2)/3,0.0f,1.0f);
                const DWORD alpha=static_cast<DWORD>((layer?210:145)*fade);
                std::array<Vertex,6> projected{};bool valid=alpha!=0;
                for(unsigned n=0;n<6;++n){
                    const Vec3 point{end.x+right.x*shape[n][1]*scale,end.y+right.y*shape[n][1]*scale,end.z+.9f-shape[n][0]*scale};
                    ScreenPoint screen{};float depth=0;const bool visible=projectScene(point,screen,depth);
                    if(!visible||depth<=std::max(.6f,nearClip)){valid=false;break;}
                    const DWORD colour=layer?D3DCOLOR_ARGB(alpha,rgb[0],rgb[1],rgb[2]):D3DCOLOR_ARGB(alpha,12,16,19);
                    projected[n]={screen.x+viewport.X,screen.y+viewport.Y,0,1/depth,colour,depth,0};
                }
                if(valid)for(auto n:triangles)vertices.push_back(projected[n]);
            }
            submitted+=static_cast<unsigned>(vertices.size());
            if(!vertices.empty()&&SUCCEEDED(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,static_cast<UINT>(vertices.size()/3),vertices.data(),sizeof(Vertex))))drew=true;
        }
        state->Apply();state->Release();texture->Release();
        report(drew?"draw submitted":(submitted?"draw failed":"geometry outside camera"),f,submitted);
        if(drew&&!logged_&&log_){log_("Road GPS Line: smoothed ribbon rendered with logarithmic scene-depth occlusion");logged_=true;}
    }
};
using RoadLineRenderer=RoadLineRendererWithScene<>;
}
