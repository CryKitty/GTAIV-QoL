// Portable equivalence checks + CPU-only stage benchmark. No timing thresholds.
#include "../vendor/roadnav/src/road_line.hpp"
#include "../src/compass.hpp"
#include "../src/police_escape.hpp"
#include <cassert>
#include <chrono>
#include <cstdio>
using roadnav::Vec3;
namespace {
volatile float sink=0;
float originalScore(const std::vector<Vec3>& path,Vec3 player,const std::vector<Vec3>& police){
    if(path.size()<2)return std::numeric_limits<float>::max();
    const float start=roadnav::nearestDistanceAlong(path,player),end=roadnav::routeLength2D(path);
    float result=std::max(0.0f,end-start);
    for(float d=start;d<end&&d<start+700;d+=12){
        const auto a=roadnav::sampleAt(path,d).position,b=roadnav::sampleAt(path,std::min(d+12,end)).position;
        result+=policeescape::segmentRisk(a,b,police);
    }return result;
}
// Frozen pre-optimisation renderer section construction, including surface fit.
std::array<Vec3,2> originalSection(const roadnav::RoadLine& line,const std::vector<float>& lift,std::size_t n,float width,Vec3 forward,roadnav::RoadLineViewBend config,bool fitted){
    auto section=roadnav::roadLineSection(line.points,n,width,std::copysign(1.0f,lift[n]),std::fabs(lift[n]));
    if(fitted)for(unsigned edge=0;edge<2;++edge)section[edge].z+=line.edges[n][edge]-line.points[n].z;
    if(config.strength>0){const auto before=line.points[n?n-1:n],after=line.points[std::min(n+1,line.points.size()-1)];
        section=roadnav::roadLineViewSection(section,{after.x-before.x,after.y-before.y,0},forward,{},line.points[n],config);}
    return section;
}
template<class F> double measure(unsigned count,F run){
    for(unsigned n=0;n<20;++n)sink=sink+run();
    const auto start=std::chrono::steady_clock::now();
    for(unsigned n=0;n<count;++n)sink=sink+run();
    return std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count()/count;
}
void equal(Vec3 a,Vec3 b){assert(a.x==b.x&&a.y==b.y&&a.z==b.z);}
}
int main(){
    std::vector<Vec3> path,police;
    for(unsigned n=0;n<1000;++n){const float d=static_cast<float>(n);path.push_back({30*std::sin(d*.04f),d*1.5f,3*std::sin(d*.01f)});}
    for(unsigned n=0;n<48;++n){const float d=static_cast<float>(n);police.push_back({30*std::cos(d),700+d*12,d<40?0.0f:80.0f});}
    // Dense/degenerate/elevated paths, arbitrary starts, and backwards queries.
    for(unsigned shape=0;shape<4;++shape){auto route=path;
        if(shape==1)for(std::size_t n=1;n<route.size();n+=3)route[n]=route[n-1];
        if(shape==2)route={{1,2,3},{1,2,3},{1,2,3}};
        if(shape==3)route.clear();
        roadnav::ForwardRouteSampler cursor(route);
        for(float d:{-1.0f,0.0f,12.0f,12.0f,50.0f,700.0f,2000.0f,2.0f,900.0f}){
            const auto a=roadnav::sampleAt(route,d),b=cursor.at(d);equal(a.position,b.position);equal(a.direction,b.direction);assert(a.distanceAlong==b.distanceAlong);}
        for(const auto player:{Vec3{},Vec3{0,500,2},Vec3{0,1200,10}})assert(originalScore(route,player,police)==policeescape::pathScore(route,player,police));
    }
    auto line=roadnav::roadLine(path,{},35);const auto lift=roadnav::roadLineLift(line.points,line.bank,{});
    std::vector<std::array<Vec3,2>> sections;
    for(bool fitted:{false,true})for(float strength:{0.0f,1.0f})for(float width:{.5292f,.8568f})for(Vec3 forward:{Vec3{0,1,0},Vec3{1,0,0},Vec3{-1,0,0}}){
        if(fitted){line.edges.resize(line.points.size());for(std::size_t n=0;n<line.points.size();++n)line.edges[n]={line.points[n].z+.1f,line.points[n].z+.2f};}else line.edges.clear();
        roadnav::RoadLineViewBend config{strength,75,1};roadnav::roadLineRenderSections(line,lift,width,forward,{},config,sections);
        for(std::size_t n=0;n<line.points.size();++n){const auto old=originalSection(line,lift,n,width,forward,config,fitted);equal(old[0],sections[n][0]);equal(old[1],sections[n][1]);}
    }
    const Vec3 player{0,700,1};const roadnav::RoadLineViewBend config{1,75,1};
    const auto before=[&](){float result=0;for(float width:{.5292f,.8568f})for(std::size_t i=1;i<line.points.size();++i)for(auto n:{i-1,i}){const auto s=originalSection(line,lift,n,width,{0,1,0},config,true);result+=s[0].z+s[1].z;}return result;};
    const auto after=[&](){float result=0;for(float width:{.5292f,.8568f}){roadnav::roadLineRenderSections(line,lift,width,{0,1,0},{},config,sections);for(std::size_t i=1;i<sections.size();++i)for(auto n:{i-1,i})result+=sections[n][0].z+sections[n][1].z;}return result;};
    assert(before()==after());
    std::vector<compass::Marker> markers;for(unsigned n=0;n<1000;++n)markers.push_back({n+1,n%128,static_cast<float>(n%360)-180,static_cast<float>((n*73)%2000)});
    const auto build=measure(2000,[&](){const auto r=roadnav::roadLine(path,player,35);return r.ahead;});
    const auto ribbonBefore=measure(3000,before),ribbonAfter=measure(3000,after);
    const auto compassUs=measure(2000,[&](){const auto r=compass::layout(markers);return static_cast<float>(r.size());});
    const auto scoreBefore=measure(1000,[&](){return originalScore(path,player,police);});
    const auto scoreAfter=measure(1000,[&](){return policeescape::pathScore(path,player,police);});
    std::printf("CPU profile (us/call): route build %.3f; ribbon sections %zu points, 2 layers %.3f -> %.3f; compass layout 1000 markers %.3f (unchanged); escape score 1000 nodes/48 cops %.3f -> %.3f; checksum %.3f\n",build,line.points.size(),ribbonBefore,ribbonAfter,compassUs,scoreBefore,scoreAfter,static_cast<double>(sink));
    std::puts("Section coordinates and escape scores match the pre-optimisation references; no game/native/GPU/FPS timing is implied.");
}
