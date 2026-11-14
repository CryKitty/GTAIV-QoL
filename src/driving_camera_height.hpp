#pragma once
#include <algorithm>
namespace qolcamera {
// Nearest preset wins; midpoint ties choose the lower camera.
inline int heightPreset(int centimeters){
    if(centimeters<=22)return 0;
    return centimeters<=82?1:2;
}
inline int presetHeight(int preset){return preset==1?45:preset==2?120:0;}
inline const wchar_t* presetName(int preset){return preset==1?L"Medium":preset==2?L"High":L"Low (Default)";}
enum class HeightGate {Ready,Default,Stopping,Player,Driver,Cutscene,Pause,Fade,Control,Interpolation,ScriptCamera,Natives};
struct DrivingState {
    bool stopping=false,player=false,driver=false,cutscene=false,paused=false,fadedIn=false,control=false,interpolating=false,scriptCamera=false,natives=true;
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
}
