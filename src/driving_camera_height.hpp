#pragma once
#include <algorithm>
#include <cstdint>
namespace qolcamera {
// Nearest preset wins; midpoint ties choose the lower camera.
inline int heightPreset(int centimeters){
    if(centimeters<=22)return 0;
    return centimeters<=82?1:2;
}
inline int presetHeight(int preset){return preset==1?45:preset==2?120:0;}
inline const wchar_t* presetName(int preset){return preset==1?L"Medium":preset==2?L"High":L"Low (Default)";}
inline bool cameraHandle(unsigned handle){return handle!=0&&handle!=~0u;}
// Driver assignment alone does not prove follow-camera readiness.
// Wait for seated, controllable driving, then two valid samples of one camera.
struct CameraEntry {
    unsigned vehicle=0,camera=0,samples=0;std::uint32_t since=0;
    void reset(){*this={};}
    bool seated(std::uint32_t now,unsigned next,bool ready,bool sitting,bool entering){
        if(!ready||!sitting||entering||!cameraHandle(next)){reset();return false;}
        if(vehicle!=next){vehicle=next;since=now;camera=samples=0;return false;}
        return now-since>=500;
    }
    bool observed(unsigned next){
        if(!cameraHandle(next)){camera=samples=0;return false;}
        if(camera!=next){camera=next;samples=1;return false;}
        if(samples<2)++samples;
        return samples>=2;
    }
};
// NativeContext aliases its first argument and result storage. Boolean native
// results use SDK b8; reading a word can retain high bits of the cam handle.
template<class Context,class Native,class Handle> bool cameraInterpolating(Native native,Handle camera){
    Context context;context.Push(camera);native(&context);
    return context.template GetResult<unsigned char>()!=0;
}
enum class HeightGate {Ready,Default,Stopping,Player,Driver,Cutscene,Pause,Fade,Control,Interpolation,ScriptCamera,Natives,CameraReady};
struct DrivingState {
    bool stopping=false,player=false,driver=false,cutscene=false,paused=false,fadedIn=false,control=false,interpolating=false,scriptCamera=false,natives=true,cameraReady=true;
    HeightGate gate(int cm)const{
        if(stopping)return HeightGate::Stopping;
        if(!cm)return HeightGate::Default;
        if(!player)return HeightGate::Player;
        if(!driver)return HeightGate::Driver;
        if(cutscene)return HeightGate::Cutscene;
        if(paused)return HeightGate::Pause;
        if(!fadedIn)return HeightGate::Fade;
        if(!control)return HeightGate::Control;
        if(interpolating)return HeightGate::Interpolation;
        if(scriptCamera)return HeightGate::ScriptCamera;
        if(!natives)return HeightGate::Natives;
        if(!cameraReady)return HeightGate::CameraReady;
        return HeightGate::Ready;
    }
};
enum class HeightCommand {None,Apply,Release};
struct DrivingHeight {
    bool owned=false;int applied=0;
    HeightCommand update(bool normalDriving,int centimeters,bool nativeOffsetActive){
        centimeters=std::clamp(centimeters,0,200);
        if(!normalDriving||!centimeters){
            if(!owned)return HeightCommand::None;
            owned=false;applied=0;return HeightCommand::Release;
        }
        // Never steal a script's pre-existing follow-camera offset.
        if(!owned&&nativeOffsetActive)return HeightCommand::None;
        if(!owned||!nativeOffsetActive||applied!=centimeters){owned=true;applied=centimeters;return HeightCommand::Apply;}
        return HeightCommand::None;
    }
    float meters()const{return static_cast<float>(applied)*.01f;}
};
template<class Write> HeightCommand applyHeight(DrivingHeight& height,const DrivingState& state,int cm,bool active,Write write){
    const auto command=height.update(state.gate(cm)==HeightGate::Ready,cm,active);
    if(command==HeightCommand::Apply)write(true,0.0f,0.0f,height.meters());
    else if(command==HeightCommand::Release)write(false,0.0f,0.0f,0.0f);
    return command;
}
// A follow-camera object may disappear during entry/exit/cutscene changes.
// Keep ownership pending instead of touching that object's offset on teardown.
template<class Write> HeightCommand applySafeHeight(DrivingHeight& height,const DrivingState& state,int cm,bool active,Write write){
    if(state.gate(cm?cm:1)!=HeightGate::Ready)return HeightCommand::None;
    return applyHeight(height,state,cm,active,write);
}

}
