#pragma once
#include "d3d_hook_service.hpp"
#include "cue_geometry.hpp"
#include <array>
#include <atomic>
#include <vector>
#include "../../../src/navigation_awareness.hpp"

namespace roadnav {
struct CueFrame {NavigationCues cues{};CameraFrame camera{};DWORD timestamp=0;Vec3 player{};float speed=0;awareness::RouteKind kind=awareness::RouteKind::Blue;bool destinationColours=false;};
class TurnRenderer final : public ID3DDeviceHook {
    SRWLOCK lock_=SRWLOCK_INIT;
    CueFrame frame_{},retired_{};
    DWORD retiredAt_=0;
    void (*trace_)(const char*)=nullptr;
    struct Vertex {float x,y,z,rhw;DWORD colour;};
    volatile LONG renderLogged_=0,drawLogged_=0;
public:
    explicit TurnRenderer(void (*trace)(const char*)=nullptr):trace_(trace){}
    void submit(CueFrame frame){
        AcquireSRWLockExclusive(&lock_);
        const auto departed=departingCues(frame_.cues,frame.cues);
        if(departed.visible()){retired_=frame_;retired_.cues=departed;retiredAt_=frame.timestamp;}
        frame_=frame;ReleaseSRWLockExclusive(&lock_);
    }
    void clear(){AcquireSRWLockExclusive(&lock_);frame_={};retired_={};ReleaseSRWLockExclusive(&lock_);}
    void OnCreateDevice(ID3DDevice*,PresentParameters*) override{}
    void OnResetDevice(ID3DDevice*,PresentParameters*) override{}
    void OnLostDevice(ID3DDevice*) override{clear();}
    void OnDestroyDevice(ID3DDevice*) override{clear();}
    void OnRender(ID3DDevice* device) override{
        // Our EndScene may synchronously re-enter ScriptHook's render service.
        static thread_local bool inside=false;if(inside)return;inside=true;
        struct Scope {bool& value;~Scope(){value=false;}} scope{inside};
        if(InterlockedCompareExchange(&renderLogged_,1,0)==0 && trace_)trace_("D3D render callback active");
        const auto now=GetTickCount();
        AcquireSRWLockShared(&lock_);const auto frame=frame_,retired=retired_;const float retiring=retirementOpacity(now-retiredAt_);
        ReleaseSRWLockShared(&lock_);
        if((!frame.cues.visible()&&(!retired.cues.visible()||retiring<=0)) || now-frame.timestamp>500)return;
        D3DVIEWPORT9 viewport{};if(FAILED(device->GetViewport(&viewport)))return;
        // Two clusters (up to 6 chevrons each) plus destination, border and fill.
        std::array<Vertex,624> vertices{};unsigned count=0;
        constexpr float shape[6][2]={{-.60f,.65f},{-.05f,.65f},{.60f,0},{-.05f,-.65f},{-.60f,-.65f},{.05f,0}};
        constexpr unsigned triangles[12]={0,1,5,1,2,5,5,2,3,5,3,4};
        for(unsigned layer=0;layer<2;++layer){
            const float scale=layer==0?1.10f:0.89f;
            for(unsigned batch=0;batch<2;++batch){
            const auto& cues=batch?retired.cues:frame.cues;
            const float opacity=batch?retiring:1.0f;if(opacity<=0)continue;
            const auto append=[&](const std::array<Vec3,6>& points,DWORD alpha){
                const auto rgb=awareness::routeColour(batch?retired.kind:frame.kind,frame.destinationColours,GetTickCount());
                const DWORD colour=layer==0?D3DCOLOR_ARGB(alpha,rgb[0]/6,rgb[1]/6,rgb[2]/6):D3DCOLOR_ARGB(alpha,rgb[0],rgb[1],rgb[2]);
                std::array<Vertex,6> projected{};
                for(unsigned v=0;v<6;++v){
                    ScreenPoint screen{};
                    if(!project(points[v],frame.camera,static_cast<float>(viewport.Width),static_cast<float>(viewport.Height),screen))return;
                    projected[v]={screen.x+viewport.X,screen.y+viewport.Y,0,1,colour};
                }
                if(count+12>vertices.size())return;
                for(auto v:triangles)vertices[count++]=projected[v];
            };
            for(unsigned turn=0;turn<cues.count;++turn){
                const auto& cue=cues.turns[turn];
                for(unsigned marker=0;marker<cueMarkerCount(cue,frame.camera);++marker){
                    const float distance=std::max(0.0f,cue.distanceAhead);
                    // Match route selection's speed-aware range, including the linked turn.
                    const float limit=cues.lookAhead+20.0f+(turn==0?0.0f:35.0f);
                    const Vec3 approachPoint=cue.followsRoute?cue.markers[0].position:cue.position;
                    float nearFade=cue.kind==CueKind::UTurn?1.0f:approachOpacity(std::min(distance2D(frame.player,approachPoint),distance2D(frame.player,cue.position)),frame.speed);
                    if(batch)nearFade=std::min(nearFade,approachOpacity(std::min(distance2D(retired.player,approachPoint),distance2D(retired.player,cue.position)),retired.speed));
                    const float fade=std::clamp((limit-distance)/20.0f,0.0f,1.0f)*nearFade;
                    const DWORD alpha=static_cast<DWORD>((marker==0||marker==3?170.0f:225.0f)*fade*(turn==0?1.0f:0.82f));
                    const auto points=cueVertices(cue,marker,scale,frame.camera);
                    append(points,static_cast<DWORD>(alpha*opacity));
                }
            }
            if(cues.destination.visible){
                const auto& destination=cues.destination;
                const float yaw=frame.camera.rotation.z*0.01745329252f;
                const Vec3 right{std::cos(yaw),std::sin(yaw),0};
                std::array<Vec3,6> points{};
                for(unsigned v=0;v<6;++v){
                    // Rotate the > shape downward. Its tip stays over the exact
                    // GPS endpoint; the vertical plane faces the camera.
                    const float across=shape[v][1]*scale;
                    points[v]={destination.position.x+right.x*across,destination.position.y+right.y*across,
                               destination.position.z+0.85f-shape[v][0]*scale};
                }
                const float fade=std::clamp((73-destination.distanceAhead)/15.0f,0.0f,1.0f)*approachOpacity(distance2D(frame.player,destination.position),frame.speed);
                append(points,static_cast<DWORD>(230*fade*opacity));
            }
        }
        }
        if(!count)return;
        IDirect3DStateBlock9* state=nullptr;
        if(FAILED(device->CreateStateBlock(D3DSBT_ALL,&state)))return;
        if(FAILED(state->Capture())){state->Release();return;}
        device->SetVertexShader(nullptr);device->SetPixelShader(nullptr);
        device->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE);
        device->SetTexture(0,nullptr);
        device->SetRenderState(D3DRS_ZENABLE,FALSE);device->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
        device->SetRenderState(D3DRS_LIGHTING,FALSE);device->SetRenderState(D3DRS_FOGENABLE,FALSE);
        device->SetRenderState(D3DRS_STENCILENABLE,FALSE);device->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
        device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);device->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID);
        device->SetRenderState(D3DRS_SHADEMODE,D3DSHADE_GOURAUD);
        device->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);device->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);
        device->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);device->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
        device->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD);device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,FALSE);
        device->SetRenderState(D3DRS_COLORWRITEENABLE,0xf);device->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);
        device->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);
        device->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE);
        device->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);
        device->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE);
        device->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE);
        device->SetTextureStageState(1,D3DTSS_ALPHAOP,D3DTOP_DISABLE);
        const HRESULT drawn=device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,count/3,vertices.data(),sizeof(Vertex));
        if(SUCCEEDED(drawn) && InterlockedCompareExchange(&drawLogged_,1,0)==0 && trace_)trace_("Turn chevrons submitted to D3D");
        state->Apply();state->Release();
    }
};
}
