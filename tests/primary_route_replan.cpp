#include <cassert>
#include <cmath>
#include <map>
#include "../src/primary_route_replan.hpp"
#include "../src/escape_gps_request.hpp"
using roadnav::Vec3;
struct Gps {
    unsigned next=2,active=1;Vec3 player{},goal{};std::vector<Vec3> nativePath;
    const std::vector<Vec3> oneWayBlock{{0,-30,0},{0,-85,0},{0,-110,0},{40,-110,0},{40,100,0},{0,100,0}};std::map<unsigned,Vec3> pins{{1,{0,100,0}}};
    bool exists(unsigned id){return pins.count(id)!=0;}bool same(unsigned a,unsigned b){return a==b;}
    void route(unsigned id,bool enabled){
        if(!enabled){if(active==id)active=0;return;}
        active=id;goal=pins.at(id);nativePath.clear();
        // Synthetic native GPS: only the legal forward edges of this one-way
        // block are available. Queries use its actual current player origin.
        unsigned begin=0,end=0;
        for(unsigned i=0;i<oneWayBlock.size();++i){
            if(roadnav::distance2D(player,oneWayBlock[i])<.1f)begin=i;
            if(roadnav::distance2D(goal,oneWayBlock[i])<.1f)end=i;
        }
        if(begin<=end)for(unsigned i=begin;i<=end;++i)nativePath.push_back(oneWayBlock[i]);
    }
    bool move(unsigned id,Vec3 p){pins[id]=p;return true;}void remove(unsigned id){pins.erase(id);}
    unsigned add(Vec3 p){pins[++next]=p;return next;}void configure(unsigned){}
};
int main(){
    using namespace roadnav;
    const std::vector<Vec3> old{{0,0,0},{0,100,0}};const Vec3 backwards{0,-1,0},velocity{0,-10,0};
    PrimaryWrongWay brief;
    for(unsigned t=0;t<900;t+=100)assert(!brief.update(t,1,true));
    assert(!brief.update(900,1,false));
    for(unsigned t=1000;t<2500;t+=100)assert(!brief.update(t,1,true));
    assert(brief.update(2500,1,true));
    for(unsigned t=2600;t<12500;t+=100)assert(!brief.update(t,1,true));
    assert(brief.update(12500,1,true)); // Same ten-second request backoff.
    PrimaryWrongWay failedRequest;
    for(unsigned t=0;t<=1500;t+=100)failedRequest.update(t,1,true);
    assert(failedRequest.retaining(1500)&&failedRequest.retaining(11499));
    assert(!failedRequest.retaining(11500));
    PrimaryWrongWay sustained;
    for(unsigned t=0;t<1500;t+=100)assert(!sustained.update(t,1,movingAgainstRoute(old,{0,-static_cast<float>(t)*.01f,0},backwards,velocity)));
    assert(sustained.update(1500,1,movingAgainstRoute(old,{0,-15,0},backwards,velocity)));
    assert(!movingAgainstRoute(old,{0,0,0},backwards,{}));
    assert(!movingAgainstRoute(old,{0,0,0},backwards,{0,-1,0}));
    assert(!movingAgainstRoute(old,{0,0,0},backwards,{0,10,0}));
    assert(!movingAgainstRoute(old,{0,0,0},{1,0,0},velocity));
    assert(!movingAgainstRoute(old,{0,0,10},backwards,velocity));
    assert(movingAgainstRoute(old,{0,0,0},backwards,{0,-2.5f,0}));
    PrimaryWrongWay stopped;
    for(unsigned t=0;t<10000;t+=200)assert(!stopped.update(t,1,movingAgainstRoute(old,{0,0,0},backwards,{})));
    PrimaryWrongWay gaps;assert(!gaps.update(0,1,true));assert(!gaps.update(3000,1,true));
    assert(!gaps.update(3200,2,true));
    for(unsigned t=3400;t<4800;t+=200)assert(!gaps.update(t,2,true));
    assert(gaps.update(4800,2,true));
    assert(!samePrimaryGoal({0,0,0},{3,0,0})&&!samePrimaryGoal({0,0,0},{0,0,4}));
    PrimaryWrongWay moved;
    for(unsigned t=0;t<1500;t+=100)assert(!moved.update(t,1,true,{0,100,0}));
    assert(!moved.update(1500,1,true,{100,100,0}));
    for(unsigned t=1600;t<3000;t+=100)assert(!moved.update(t,1,true,{100,100,0}));
    assert(moved.update(3000,1,true,{100,100,0}));
    PrimaryWrongWay wrap;const unsigned start=0xfffffc00u;
    for(unsigned dt=0;dt<1500;dt+=100)assert(!wrap.update(start+dt,1,true));
    assert(wrap.update(start+1500,1,true));
    Vec3 lead{};
    assert(forwardRoadTarget({0,-30,0},backwards,10,[](Vec3 probe,unsigned,Vec3& p){p=probe;return true;},lead));
    assert(lead.y<-60&&lead.x==0);
    assert(!forwardRoadTarget({0,0,0},backwards,10,[](Vec3,unsigned,Vec3& p){p={0,30,0};return true;},lead));
    assert(!forwardRoadTarget({0,0,0},backwards,10,[](Vec3 probe,unsigned,Vec3& p){p={probe.x,probe.y,20};return true;},lead));
    const Vec3 current{0,-30,0};lead={0,-85,0};
    const std::vector<Vec3> ahead{{0,-30,0},{0,-55,0},{0,-85,0}};
    const std::vector<Vec3> wrong{{0,-30,0},{0,20,0},{20,20,0},{20,-85,0},{0,-85,0}};
    assert(forwardGpsRoute(ahead,current,backwards,lead));
    assert(!forwardGpsRoute(wrong,current,backwards,lead)); // Correct endpoint alone is insufficient.
    assert(!forwardGpsRoute(ahead,current,backwards,{0,100,0}));
    Gps gps;gps.player=current;const auto original=gps.pins.at(1);
    auto helper=policeescape::requestGps(0u,1u,lead,gps);
    assert(helper!=1&&gps.active==helper&&gps.goal.y==-85);
    assert(gps.pins.at(1).y==original.y); // Never move/delete the user's destination.
    assert(forwardGpsRoute(gps.nativePath,gps.player,backwards,lead));
    assert(gps.nativePath[1].y<gps.player.y); // The fresh native lead-in starts WITH travel.
    gps.player={0,-85,0};assert(reachedForwardNode(gps.player,lead,backwards));
    policeescape::restorePrimaryGps(helper,1u,gps);gps.remove(helper);
    assert(gps.active==1&&gps.goal.y==100&&gps.player.y==-85);
    // A native continuation around the block can then reach the original pin;
    // every leg comes from GPS, not reversed/invented one-way road geometry.
    assert(gps.nativePath.size()==5&&gps.nativePath.back().y==100);
    assert(forwardGpsRoute(gps.nativePath,gps.player,backwards,original));
    assert(primaryOriginMatches(gps.nativePath,gps.player));
    assert(!primaryOriginMatches(old,gps.player)); // Do not republish the stale old-origin plan.
}
