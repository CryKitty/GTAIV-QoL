#pragma once
#include <algorithm>
#include <cmath>
namespace cutscenemix {
inline float gain(bool active,bool enabled,int percent){return active&&enabled?std::clamp(percent,0,1600)/100.0f:1.0f;}
inline long decibels(long original,float gain){return gain<=0?-10000:std::clamp(original+static_cast<long>(std::lround(2000*std::log10(gain))),-10000L,0L);}
}
