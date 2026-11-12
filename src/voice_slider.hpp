#pragma once
#include <algorithm>
namespace voiceslider {
// Two equal halves: 0..100% attenuation, then 100..1600% amplification.
inline float Position(int percent){const float value=static_cast<float>(std::clamp(percent,0,1600));return value<=100.0f?value:100.0f+(value-100.0f)/15.0f;}
inline int Percent(float position){position=std::clamp(position,0.0f,200.0f);return static_cast<int>((position<=100.0f?position:100.0f+(position-100.0f)*15.0f)+0.5f);}
}
