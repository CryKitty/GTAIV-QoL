#include "../src/voice_slider.hpp"
#include <cassert>
int main(){assert(voiceslider::Percent(0)==0);assert(voiceslider::Percent(100)==100);assert(voiceslider::Percent(200)==1600);assert(voiceslider::Position(100)==100);assert(voiceslider::Position(1600)==200);for(int i=0;i<=1600;++i)assert(voiceslider::Percent(voiceslider::Position(i))==i);for(int i=1;i<=200;++i)assert(voiceslider::Percent(static_cast<float>(i))>=voiceslider::Percent(static_cast<float>(i-1)));}
