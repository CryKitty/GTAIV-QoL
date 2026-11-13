#pragma once
#include <algorithm>
namespace qolcamera {
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
}
