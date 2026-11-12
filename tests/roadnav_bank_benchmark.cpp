// Optional portable microbenchmark: g++ -std=c++17 -O2 tests/roadnav_bank_benchmark.cpp -o /tmp/roadnav-bank-bench
#include "../vendor/roadnav/src/road_line.hpp"
#include <chrono>
#include <cstdio>
#include <cassert>
static std::vector<float> originalBank(const std::vector<roadnav::Vec3>& points){
    using namespace roadnav;
    std::vector<float> bank(points.size(),0),along(points.size(),0);
    for(std::size_t i=1;i<points.size();++i)along[i]=along[i-1]+distance2D(points[i-1],points[i]);
    for(std::size_t i=1;i+1<points.size();++i){
        const float before=std::max(0.0f,along[i]-10),after=std::min(along.back(),along[i]+10);
        if(along[i]-before<2||after-along[i]<2)continue;
        const auto p=sampleAt(points,before).position,q=points[i],r=sampleAt(points,after).position;
        const float x=q.x-p.x,y=q.y-p.y,u=r.x-q.x,v=r.y-q.y;
        const float angle=std::atan2(x*v-y*u,x*u+y*v);
        const float amount=std::clamp((std::fabs(angle)-.08f)/1.2f,0.0f,1.0f);
        bank[i]=std::copysign(amount*amount*(3-2*amount),angle);
    }
    return bank;
}
int main(){
    using namespace roadnav;
    std::vector<Vec3> points;for(int i=0;i<220;++i){const float d=static_cast<float>(i);points.push_back({30*std::sin(d*.04f),d*1.5f,0});}
    assert(originalBank(points)==roadLineBank(points));
    volatile float sink=0;
    const auto measure=[&](auto bank){
        constexpr int runs=4000;
        const auto start=std::chrono::steady_clock::now();
        for(int i=0;i<runs;++i){const auto result=bank(points);sink=sink+result[100];}
        return std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count()/runs;
    };
    // Warm both paths, then compare identical inputs/compiler/process.
    measure(originalBank);measure(roadLineBank);
    const auto before=measure(originalBank),after=measure(roadLineBank);
    std::printf("220 points, 4000 calls: original %.3f us/call; optimized %.3f us/call; %.2fx; checksum %.6f\n",before,after,before/after,static_cast<double>(sink));
}
