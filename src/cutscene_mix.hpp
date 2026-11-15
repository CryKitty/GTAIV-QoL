#pragma once
#include <algorithm>
#include <cmath>
namespace cutscenemix {
inline bool active(bool readable,unsigned state,bool sceneLoaded,bool sceneFinished){return readable&&(state==8||state==9)&&sceneLoaded&&!sceneFinished;}
inline float requested(int percent){return std::clamp(percent,0,1600)/100.0f;}
inline float halfBoost(float sliderGain){return 1.0f+(sliderGain-1.0f)*.5f;}
inline float gain(bool active,bool enabled,int percent){return active&&enabled?halfBoost(requested(percent)):1.0f;}
inline long decibels(long original,float gain){return gain<=0?-10000:std::clamp(original+static_cast<long>(std::lround(2000*std::log10(gain))),-10000L,0L);}
}
