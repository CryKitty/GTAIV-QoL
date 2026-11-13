#include "../vendor/roadnav/src/gps_route_reader.hpp"
#include <cstring>
#include "../vendor/roadnav/src/cue_geometry.hpp"
#include "../vendor/roadnav/src/road_surface.hpp"
#include "../vendor/roadnav/src/tracked_destination.hpp"
#include "../vendor/roadnav/src/road_projection.hpp"
#include "../src/police_escape.hpp"
#include "../src/primary_gps_recovery.hpp"
#include "../src/primary_route_replan.hpp"
#include "../src/escape_gps_request.hpp"
#include "../src/navigation_awareness.hpp"
#include "../src/compass.hpp"
#include <cassert>
#include <cstdio>
using namespace roadnav;
int main(){
    {
        assert(compass::enemyOwnsAutomaticGps(false,true,true,true)); // wanted must not veto acquisition
        assert(!compass::enemyOwnsAutomaticGps(true,true,true,true)); // fixed goal retains priority
        assert(!compass::enemyOwnsAutomaticGps(false,true,false,true)); // Enemy Off returns escape ownership
        assert(!compass::enemyOwnsAutomaticGps(false,false,true,true));
        assert(!compass::enemyOwnsAutomaticGps(false,true,true,false));
        // A target moving every 200ms must not cancel a cold GPS request.
        unsigned requests=1;
        for(unsigned elapsed=200;elapsed<6000;elapsed+=200){
            if(chaseRefreshDue(elapsed,true,false))++requests;
        }
        assert(requests==1);
        const std::vector<Vec3> stale{{0,0,0},{0,100,0}};
        const Vec3 shifted{0,105,0};
        assert(routeDestinationMatches(stale,shifted)); // endpoint tolerance alone is insufficient
        assert(!chaseRefreshDue(200,true,!sameGpsRoad(stale,stale)));
        assert(!sameGpsRoad(stale,{{0,0,0},{0,105,0}}));
        assert(chaseRefreshDue(6000,true,false)); // bounded failure recovery
        assert(chaseRefreshDue(800,true,true)); // completed request can refresh
        assert(!chaseRefreshDue(199,true,true));
        PrimaryWrongWay wrong;
        unsigned fired=0;
        for(unsigned t=0;t<=1500;t+=primaryReplanPollMs)if(wrong.update(t,42,true))fired=t;
        assert(fired==1500); // unchanged sustained/brief-swerve gates
        const Vec3 heading{0,-1,0},target{0,-100,0};
        assert(forwardGpsRoute({{0,0,0},{0,-100,0}}, {},heading,target));
        assert(!forwardGpsRoute({{0,0,0},{0,50,0},{0,-100,0}}, {},heading,target));
    }
    {
        for(const float direction:{-1.0f,1.0f}){
            auto lane=roadLine({{0,0,0},{0,100*direction,0}}, {},10);
            offsetDrivingSide(lane);
            for(auto point:lane.points)assert(std::fabs(point.x-2*direction)<.001f);
            fitRoadSurface(lane,{},RoadLine{},[](float,float,float){return 0.0f;});
            for(auto point:lane.points)assert(std::fabs(point.z-roadSurfaceClearance)<.001f);
        }
        auto curve=roadLine({{0,0,10},{0,30,10},{10,60,11},{30,80,12}}, {},20);
        const auto center=curve.points;offsetDrivingSide(curve);
        for(std::size_t i=0;i<center.size();++i){
            assert(std::fabs(distance2D(center[i],curve.points[i])-2)<.001f);
            assert(curve.points[i].z==center[i].z); // height is fitted afterward, no floor jump
            if(i)assert(distance2D(curve.points[i-1],curve.points[i])<3.5f);
        }
    }
    {
        auto displayed=roadLine({{0,0,0},{0,100,0}},Vec3{},20);
        const auto old=displayed.points;
        assert(updateEscapeSurface(displayed,{},true,true)); // reset job waiting, no new surface
        assert(displayed.points.size()==old.size()&&displayed.points.back().y==old.back().y);
        assert(updateEscapeSurface(displayed,{},true,false)); // failed request/backoff also retains
        auto replacement=roadLine({{0,0,0},{100,0,0}},Vec3{},20);
        assert(!updateEscapeSurface(displayed,std::move(replacement),true,true));
        assert(displayed.points.back().x>0&&!displayed.endsAtDestination);
        assert(!updateEscapeSurface(displayed,{},false,false)); // explicit committed-route clear
        assert(!displayed.visible());
    }

    {
        PrimaryGpsRecovery recovery;
        const Vec3 objective{300,100,0},roadNode{300,0,0};
        assert(recovery.due(0,42,objective,false,false)); // fixed marker requests GPS without a chase tick
        assert(!recovery.due(5999,42,objective,false,false)); // give native GPS its full cold-start window
        assert(recovery.due(6000,42,objective,false,false)); // failed initial request is retried
        assert(!recovery.due(15999,42,objective,false,false));
        assert(!recovery.due(16000,42,objective,false,true)); // escape candidate/wrong-way job owns buffer
        assert(recovery.due(16200,42,objective,false,false)); // recover as soon as borrower releases
        const std::vector<Vec3> road{{0,0,0},roadNode};
        assert(!primaryDestinationMatches(road,objective)); // marker itself is off the drivable road
        assert(primaryDestinationMatches(road,objective,&roadNode)); // native destination's real road node
        assert(!primaryDestinationMatches({{0,0,0},{300,0,40}},objective,&roadNode)); // another level
        assert(!primaryDestinationMatches({{0,0,0},{0,300,0}},objective,&roadNode)); // stale escape goal
        assert(keepPrimaryGpsRoute(road,{100,0,0},objective,&roadNode));
        assert(!keepPrimaryGpsRoute(road,{100,40,0},objective,&roadNode)); // departed road, no indefinite stale ribbon
        assert(!keepPrimaryGpsRoute(road,{100,0,0},{0,300,0})); // destination changed
        assert(!keepPrimaryGpsRoute({}, {},objective,&roadNode));
        assert(!recovery.due(16400,42,objective,true,false));
        assert(recovery.failures==0);
        auto ribbon=roadLine(road,{},20);
        fitRoadSurface(ribbon,{},RoadLine{},[](float,float,float){return 0.0f;});
        pinDestination(ribbon,objective);
        const auto kind=awareness::destinationRouteKind(false,false,false);
        awareness::LineTypes types;types.enemy=false;types.custom=false;
        assert(types.visible(true,kind)&&ribbon.visible()); // nearby enemy/custom Off cannot hide the objective
        assert(roadLineDestination(ribbon).x==objective.x&&roadLineDestination(ribbon).y==objective.y);
        types.mission=false;assert(!types.visible(true,kind)); // Mission Off still deliberately hides it
        assert(recovery.due(16600,43,{350,100,0},false,false)); // new objective has no inherited backoff
        assert(recovery.due(16700,43,{370,100,0},false,false)); // same marker moved to a new fixed goal
        assert(!recovery.due(16800,0,{},false,false)); // no live destination: no request
        assert(recovery.due(0xfffffff0u,44,objective,false,false));
        assert(!recovery.due(0x100u,44,objective,false,false)); // unsigned tick wrap
        assert(recovery.due(0x1760u,44,objective,false,false));
    }

    {
        // A borrowed GPS slot is retried after 30s instead of waiting forever.
        PrimaryGpsRecovery recovery;
        const Vec3 objective{300,100,0};
        assert(recovery.due(0,42,objective,false,false));
        assert(!recovery.due(1000,42,objective,false,true));
        assert(!recovery.due(30999,42,objective,false,true)); // still borrowed inside the window
        assert(recovery.due(31000,42,objective,false,true)); // window expires: retry anyway
        assert(!recovery.due(32000,42,objective,false,true)); // new request still gets its window
    }

    {
        // A wanted pursuit renews at the endpoint without clearing committed geometry.
        const std::vector<Vec3> old{{0,0,0},{300,0,0}};
        auto displayed=roadLine(old,{200,0,0},20);
        const auto saved=displayed.points;
        const Vec3 player{300,0,0},heading{1,0,0};
        assert(policeescape::replanDue(false,true,0,0,0));
        policeescape::ReplanState state;
        state.observe(2,false,{},150,true,heading);state.begin(0);
        assert(updateEscapeSurface(displayed,roadLine(old,player,20),true,true));
        assert(displayed.points.back().x==saved.back().x);
        state.failed(2500);
        assert(updateEscapeSurface(displayed,{},true,false));
        assert(displayed.points.back().x==saved.back().x&&!state.ready(7499));
        assert(state.ready(7500));state.begin(7500);
        const std::vector<Vec3> next{player,{700,0,0}};
        assert(policeescape::usableReplacement(next,player,{},150,heading,20));
        state.committed();
        assert(!updateEscapeSurface(displayed,roadLine(next,player,20),true,false));
        assert(displayed.visible()&&displayed.points.back().x>player.x);
        assert(!updateEscapeSurface(displayed,{},false,false)); // wanted cleared
        assert(!displayed.visible());
    }

    {
        const Vec3 car{400,250,10};
        assert(routeDestinationMatches({{0,0,0},{400,250,9}},car));
        assert(!routeDestinationMatches({{0,0,0},{0,200,0}},car)); // leftover other-goal GPS
        assert(!routeDestinationMatches({{0,0,0},{400,250,30}},car)); // wrong elevation
        assert(!routeDestinationMatches({},car));
    }

    {
        // Mission coordinates are distinct from the road node, including Z.
        const Vec3 mission{3.125f,90.875f,2.75f};
        auto line=roadLine({{0,0,0},{0,95,0}},Vec3{},20);
        assert(line.endsAtDestination);pinDestination(line,mission);
        const auto pin=roadLineDestination(line);
        assert(pin.x==mission.x&&pin.y==mission.y&&pin.z==mission.z);
        auto cues=routeCues({{0,0,0},{0,45,0}},Vec3{},10);pinDestination(cues,mission);
        assert(cues.destination.visible&&cues.destination.position.x==mission.x&&cues.destination.position.y==mission.y&&cues.destination.position.z==mission.z);
        const std::vector<Vec3> chase{{0,0,0},{0,200,0},{0,400,0},{0,600,0}};
        assert(!roadLine(chase,Vec3{},0).endsAtDestination);
        const auto base=roadLine(chase,Vec3{},0,true);
        assert(base.endsAtDestination&&base.points.size()<=500);
        for(float travel=0;travel<=25;travel+=.25f){
            auto live=base;const Vec3 car{1.25f,600+travel,1.0f};
            assert(trackLineEndpoint(live,car));const auto endpoint=live.points.back();
            assert(distance2D(endpoint,car)<.001f&&std::fabs(endpoint.z-car.z)<.001f);
            assert(distance2D(roadLineDestination(live),car)<.001f);
        }
        auto trimmed=base;assert(trackLineEndpoint(trimmed,{1,585,1}));assert(trimmed.points.back().y==585);
        for(const Vec3 invalid:std::vector<Vec3>{{0,700,1},{40,600,1},{0,600,20}}){
            auto safe=base;assert(!trackLineEndpoint(safe,invalid));assert(safe.points.back().y==600);}
        auto missing=base;missing.endsAtDestination=false;
        assert(!trackLineEndpoint(missing,{0,605,1})); // no fabricated unstreamed tail
        auto fitted=base;fitted.edges.assign(fitted.points.size(),{.24f,.24f});
        assert(trackLineEndpoint(fitted,{0,605,1}));assert(fitted.edges.size()==fitted.points.size());
        assert(fitted.bank.size()==fitted.points.size()&&fitted.edges.back()[0]==1);
    }

    // The experiment is opt-in: disabled and forward-aligned sections preserve
    // the baseline. Side-on sections roll as a whole without sinking below road.
    const auto flatSection=roadLineSection(std::vector<Vec3>{{0,0,0},{0,20,0}},0,1);
    const RoadLineViewBend viewBend{1,75,1};
    const auto disabled=roadLineViewSection(flatSection,{0,1,0},{1,0,0},{10,0,2},{},{});
    for(unsigned n=0;n<2;++n)assert(disabled[n].x==flatSection[n].x&&disabled[n].z==flatSection[n].z);
    const auto straightSection=roadLineViewSection(flatSection,{0,1,0},{0,1,0},{10,0,2},{},viewBend);
    for(unsigned n=0;n<2;++n)assert(straightSection[n].x==flatSection[n].x&&straightSection[n].z==flatSection[n].z);
    const auto perpendicularSection=roadLineViewSection(flatSection,{0,1,0},{1,0,0},{10,0,2},{},viewBend);
    assert(std::fabs(perpendicularSection[0].z-perpendicularSection[1].z)>.9f);
    assert(std::min(perpendicularSection[0].z,perpendicularSection[1].z)>=0);
    assert(distance2D(perpendicularSection[0],perpendicularSection[1])<.3f); // Foreshortened complete ribbon.
    {
        float last=0;
        for(int degrees=0;degrees<=90;++degrees){
            const float angle=static_cast<float>(degrees)*.01745329252f;
            const auto section=roadLineViewSection(flatSection,{0,1,0},{std::sin(angle),std::cos(angle),0},{10,0,2},{},viewBend);
            const float height=std::fabs(section[0].z-section[1].z);
            assert(std::isfinite(height)&&height>=last-.00001f&&height-last<.04f);last=height;
        }
    }
    // An L's entire far leg must stay rolled, even hundreds of metres past
    // the corner where curvature/bank is zero and the old per-point ray faded.
    std::vector<Vec3> longLeg{{0,0,0},{0,40,0}};
    for(int distance=10;distance<=1000;distance+=10)longLeg.push_back({static_cast<float>(distance),40,0});
    const auto longBank=roadLineBank(longLeg);
    for(std::size_t n=5;n<longLeg.size();++n){
        assert(longBank[n]==0);
        const auto section=roadLineSection(longLeg,n,1);
        const auto twisted=roadLineViewSection(section,{1,0,0},{0,1,0},{0,-5,2},longLeg[n],viewBend);
        assert(std::fabs(twisted[0].z-twisted[1].z)>.96f);
        const auto off=roadLineViewSection(section,{1,0,0},{0,1,0},{0,-5,2},longLeg[n],{});
        for(unsigned edge=0;edge<2;++edge)assert(off[edge].z==0);
        const auto aligned=roadLineViewSection(section,{0,1,0},{0,1,0},{0,-5,2},longLeg[n],viewBend);
        assert(aligned[0].z==0&&aligned[1].z==0); // Route turns back toward heading.
    }
    // Heading changes, including crossing the axis, fade continuously; section
    // position itself cannot alter the roll or introduce joins on one leg.
    auto previousAngleSection=flatSection;
    for(int degrees=-180;degrees<=180;++degrees){
        const float angle=static_cast<float>(degrees)*.01745329252f;
        const auto section=roadLineViewSection(flatSection,{0,1,0},{std::sin(angle),std::cos(angle),0},{0,-5,2},{},viewBend);
        if(degrees>-180)for(unsigned edge=0;edge<2;++edge)assert(std::fabs(section[edge].z-previousAngleSection[edge].z)<.12f);
        previousAngleSection=section;
    }
    {
        // A primary route already has validated road heights on a shared
        // approach. The new escape starts farther ahead, with no own cache.
        // Collision streaming misses previously discarded that whole escape.
        RoadLine primarySurface;for(int n=0;n<14;++n)primarySurface.points.push_back({0,4+static_cast<float>(n)*1.5f,roadSurfaceClearance});
        primarySurface.ahead=routeLength2D(primarySurface.points);
        RoadLine newEscape;newEscape.points={{0,8,0},{0,9.5f,0},{0,11,0},{0,12.5f,0}};
        const RoadLine noEscapeSurface;
        const auto missing=[](float,float,float){return -10000.0f;};
        auto discarded=newEscape;fitRoadSurface(discarded,{0,0,1},noEscapeSurface,missing);
        assert(!discarded.visible()); // Reproduces the exact no-surface gate.
        const auto sharedStats=fitRoadSurface(newEscape,{0,0,1},noEscapeSurface,missing,&primarySurface);
        assert(sharedStats.reused>0&&primarySurface.visible()&&newEscape.visible());
        assert(newEscape.points.size()==4&&primarySurface.points.size()==14);
        for(const auto p:newEscape.points)assert(std::fabs(p.z-roadSurfaceClearance)<.0001f);
        for(std::size_t n=0;n<primarySurface.points.size();++n){
            assert(primarySurface.points[n].y==4+static_cast<float>(n)*1.5f);
            assert(primarySurface.points[n].z==roadSurfaceClearance);
        }
        auto otherLevel=primarySurface;for(auto& p:otherLevel.points)p.z+=10;
        RoadLine rejected;rejected.points={{0,8,0},{0,9.5f,0},{0,11,0}};
        fitRoadSurface(rejected,{0,0,1},noEscapeSurface,missing,&otherLevel);
        assert(!rejected.visible()); // Never borrow a bridge/other floor.
    }
    // Compare the optimized bank calculation against the original sampling
    // formula over different densities, elevations, duplicates and turn signs.
    for(unsigned count:{0u,1u,2u,20u,220u,500u}){
        std::vector<Vec3> path;for(unsigned i=0;i<count;++i){const float d=static_cast<float>(i);path.push_back({20*std::sin(d*.13f),d*1.3f,3*std::sin(d*.07f)});}
        if(count>20)path[12]=path[11];
        const auto bank=roadLineBank(path);std::vector<float> along(count,0);
        for(unsigned i=1;i<count;++i)along[i]=along[i-1]+distance2D(path[i-1],path[i]);
        for(unsigned i=1;i+1<count;++i){
            const float before=std::max(0.0f,along[i]-10),after=std::min(along.back(),along[i]+10);
            float expected=0;
            if(along[i]-before>=2&&after-along[i]>=2){
                const auto p=sampleAt(path,before).position,q=path[i],r=sampleAt(path,after).position;
                const float x=q.x-p.x,y=q.y-p.y,u=r.x-q.x,v=r.y-q.y;
                const float angle=std::atan2(x*v-y*u,x*u+y*v);
                const float amount=std::clamp((std::fabs(angle)-.08f)/1.2f,0.0f,1.0f);
                expected=std::copysign(amount*amount*(3-2*amount),angle);
            }
            assert(std::fabs(bank[i]-expected)<.000001f);
        }
    }

    {
        std::array<unsigned char,0xdb0*2> memory{};
        const auto fill=[&](int slot,Vec3 end){
            const auto base=static_cast<std::size_t>(slot)*0xdb0;
            const float points[]{0,0,0,0,end.x,end.y,end.z,0};const std::int32_t count=2,state=3;
            std::memcpy(memory.data()+base,points,sizeof(points));std::memcpy(memory.data()+base+0xd60,&count,4);std::memcpy(memory.data()+base+0xd64,&state,4);
        };
        const auto read=[&](std::uintptr_t at,void* out,std::size_t size){if(at>memory.size()||size>memory.size()-at)return false;std::memcpy(out,memory.data()+at,size);return true;};
        {
            // Native buffer -> objective validation -> fitted visible mission line.
            // The escape buffer is populated too, but cannot supply this goal.
            const Vec3 objective{300,100,0},node{300,0,0};
            PrimaryGpsRecovery recovery;
            assert(!recovery.due(0,42,objective,false,true)); // escape job is still computing
            assert(recovery.due(200,42,objective,false,false)); // independent primary handoff while wanted
            fill(0,{0,300,0});fill(1,node);
            NativeGpsRoute mission;
            assert(readGpsRoute(0,read,mission,&node)&&mission.slot==1);
            assert(primaryDestinationMatches(mission.points,objective,&node));
            assert(!recovery.due(400,42,objective,true,false));
            auto line=roadLine(mission.points,{},20);
            fitRoadSurface(line,{},RoadLine{},[](float,float,float){return 0.0f;});
            pinDestination(line,objective);
            assert(line.visible()&&roadLineDestination(line).y==objective.y);
        }
        {
            // Both buffers end near the ahead node. Old backward buffer wins
            // endpoint ranking unless forward validation happens per candidate.
            const Vec3 lead{0,-100,0},origin{},heading{0,-1,0};
            fill(0,lead);fill(1,{0,-98,0});
            const float backward[]{0,0,0,0,0,30,0,0,0,-100,0,0};
            const std::int32_t count=3;
            std::memcpy(memory.data(),backward,sizeof(backward));
            std::memcpy(memory.data()+0xd60,&count,4);
            NativeGpsRoute candidate;
            assert(readGpsRoute(0,read,candidate,&lead,&origin)&&candidate.slot==0);
            assert(!forwardGpsRoute(candidate.points,origin,heading,lead));
            const auto forward=[&](const std::vector<Vec3>& points){return forwardGpsRoute(points,origin,heading,lead);};
            assert(readGpsRoute(0,read,candidate,&lead,&origin,forward)&&candidate.slot==1);
            auto line=roadLine(candidate.points,origin,10);offsetDrivingSide(line,candidate.points);
            fitRoadSurface(line,origin,RoadLine{},[](float,float,float){return 0.0f;});
            assert(line.visible()&&line.points.back().y<0);
        }
        fill(0,{0,300,0});fill(1,{300,0,0});NativeGpsRoute route;const Vec3 target{300,0,0};
        assert(readGpsRoute(0,read,route,&target)&&route.slot==1&&route.points.back().x==300);
        fill(0,target);fill(1,{0,300,0});assert(readGpsRoute(0,read,route,&target)&&route.slot==0);
        fill(0,{300,0,30});fill(1,target);assert(readGpsRoute(0,read,route,&target)&&route.slot==1);
        assert(readGpsRoute(0,read,route)&&route.slot==0); // Preserve ordinary legacy selection.
        fill(0,target);fill(1,target);
        const float newStart[3]={100,0,0};std::memcpy(memory.data()+0xdb0,newStart,sizeof(newStart));
        const Vec3 currentStart{100,0,0};
        assert(readGpsRoute(0,read,route,&target)&&route.slot==0);
        assert(readGpsRoute(0,read,route,&target,&currentStart)&&route.slot==1);
        fill(0,target);fill(1,{target.x+60,target.y,target.z});
        std::memcpy(memory.data()+0xdb0,newStart,sizeof(newStart));
        assert(readGpsRoute(0,read,route,&target,&currentStart)&&route.slot==0); // Endpoint ownership still wins.
        // Primary and escape readers can consume opposite native buffers in
        // the same tick without either snapshot being overwritten by the other.
        const Vec3 escapeTarget{0,300,0};fill(0,escapeTarget);fill(1,target);
        NativeGpsRoute primary,escape;
        assert(readGpsRoute(0,read,primary,&target)&&primary.slot==1);
        assert(readGpsRoute(0,read,escape,&escapeTarget)&&escape.slot==0);
        auto primaryRibbon=roadLine(primary.points,{0,0,1},30);
        auto escapeRibbon=roadLine(escape.points,{0,0,1},30);
        const RoadLine emptySurface;
        const auto flatGround=[](float,float,float){return 0.0f;};
        fitRoadSurface(primaryRibbon,{0,0,1},emptySurface,flatGround);
        fitRoadSurface(escapeRibbon,{0,0,1},emptySurface,flatGround,&primaryRibbon);
        assert(primaryRibbon.visible()&&escapeRibbon.visible());
        assert(primaryRibbon.points.back().x>100&&escapeRibbon.points.back().y>100);
        fill(0,{0,500,0});fill(1,{0,700,0}); // GPS borrowed for the next candidates.
        assert(primary.points.back().x==300&&primary.points.back().y==0);
        assert(escape.points.back().x==0&&escape.points.back().y==300);
        assert(primaryRibbon.visible()&&escapeRibbon.visible());
        fill(0,{300,0,30});fill(1,target);
        // A matching buffer that changes while copied cannot be displayed.
        unsigned states=0;
        const auto changing=[&](std::uintptr_t at,void* out,std::size_t size){
            if(!read(at,out,size))return false;
            if(at==0xdb0+0xd64&&++states==2){const std::int32_t state=0;std::memcpy(out,&state,4);}return true;
        };
        assert(readGpsRoute(0,changing,route,&target)&&route.slot==0);
        memory.fill(0);assert(!readGpsRoute(0,read,route,&target)&&route.points.empty());
    }

    RoadProjection projection;
    for(int i=0;i<4;++i)projection.view[i][i]=1;
    projection.projection[0][0]=projection.projection[1][1]=1;
    projection.projection[2][3]=1;projection.projection[2][2]=1;
    projection.projection[3][2]=-.1f;
    assert(projection.usable());
    ScreenPoint projected;float depth=0;
    assert(projection.projectPoint({0,0,10},1920,1080,projected,depth));
    assert(projected.x==960&&projected.y==540&&depth==10);
    projection.view[3][0]=-1; // The rendered camera moved after the script camera tick.
    assert(projection.projectPoint({0,0,10},1920,1080,projected,depth)&&projected.x<960);
    assert(!projection.projectPoint({0,0,-1},1920,1080,projected,depth));
    CameraFrame mainCamera{{1,0,0},{90,0,0},45};
    assert(projection.matchesCamera(mainCamera));
    assert(!projection.matchesCamera({{100,0,0},{90,0,0},45})); // Phone/mirror/other viewport.
    assert(!projection.matchesCamera({{1,0,0},{-90,0,0},45}));
    projection.valid=true;projection.nearClip=.15f;projection.farClip=1500;
    float sceneNear=1,sceneFar=100;assert(projection.clipPlanes(sceneNear,sceneFar)&&sceneNear==.15f&&sceneFar==1500);
    projection.nearClip=0;assert(!projection.clipPlanes(sceneNear,sceneFar)&&sceneNear==.15f); // Reject HUD clipping planes.
    projection.projection[3][3]=1;assert(!projection.usable()); // Reject orthographic HUD/shadow views.

    assert(roadLine({{0,0,0},{0,20,0}},{0,0,0},0).endsAtDestination);
    assert(!roadLine({{0,0,0},{0,200,0}},{0,0,0},0).endsAtDestination);

    RoadLine flat;flat.points={{0,4,30},{0,5.5f,40},{0,7,20},{0,8.5f,0}};
    fitRoadSurface(flat,{0,0,1},{},[](float,float,float ceiling){return ceiling>=0?0.0f:-10000.0f;});
    assert(flat.points.size()==4);for(const auto& p:flat.points)assert(std::fabs(p.z-roadSurfaceClearance)<.001f);
    auto noisy=flat;for(auto& point:noisy.points)point.z+=.1f;
    stabilizeRoadSurface(noisy,flat);for(const auto& point:noisy.points)assert(std::fabs(point.z-(roadSurfaceClearance+.02f))<.001f);
    auto differentLevel=flat;for(auto& point:differentLevel.points)point.z+=3;
    stabilizeRoadSurface(differentLevel,flat);for(const auto& point:differentLevel.points)assert(std::fabs(point.z-(3+roadSurfaceClearance))<.001f);
    RoadLine hill;for(int i=4;i<35;++i)hill.points.push_back({0,static_cast<float>(i),100});
    fitRoadSurface(hill,{0,0,1},{},[](float,float y,float ceiling){const float z=y<15?-y*.15f:-2.25f;return ceiling>=z?z:-10000;});
    assert(hill.points.size()==31);for(const auto& p:hill.points)assert(std::fabs(p.z-(p.y<15?-p.y*.15f:-2.25f)-roadSurfaceClearance)<.001f);
    auto intermittent=hill;
    const auto fitStats=fitRoadSurface(intermittent,{0,0,1},hill,[](float,float y,float ceiling){const float z=y<15?-y*.15f:-2.25f;return y>12&&y<16?-10000.0f:(ceiling>=z?z:-10000.0f);});
    assert(fitStats.reused>0&&intermittent.points.size()==hill.points.size());
    const std::vector<Vec3> retainedPath{{0,0,0},{0,100,0}};
    assert(keepRoadLineRoute(retainedPath,{0,20,1},1000));
    assert(!keepRoadLineRoute(retainedPath,{30,20,1},1000));
    assert(!keepRoadLineRoute(retainedPath,{0,20,10},1000));
    assert(!keepRoadLineRoute(retainedPath,{0,20,1},2100));
    const std::vector<Vec3> bend{{0,0,0},{0,10,0},{0,20,0},{10,20,0},{20,20,0}};
    const auto bendBank=roadLineBank(bend);
    assert(bendBank[2]<-.5f);
    assert(roadLineBankHeight({0,50,0},{0,0,0})==.65f);
    assert(roadLineBankHeight({0,50,8},{0,0,0})>5);
    const Vec3 crest{0,60,8};
    const float farBank=roadLineBankHeight(crest,{0,0,0});
    const float midBank=roadLineBankHeight(crest,{0,30,0});
    const float closeBank=roadLineBankHeight(crest,{0,56,0});
    assert(farBank<midBank&&midBank<closeBank&&closeBank>15);
    assert(roadLineBankHeight(crest,{0,56,7})<closeBank);
    assert(roadLineBankHeight(crest,{0,56,8})==.65f);
    assert(roadLineBankHeight(crest,{0,56,10})==.65f);
    assert(roadLineBankHeight({0,0,100},{0,0,0})<=16.65f);
    const auto raised=roadLineSection(bend,2,.53f,bendBank[2],closeBank);
    assert(raised[0].z>15&&raised[1].z==bend[2].z);
    const auto section=roadLineSection(bend,2,.53f,bendBank[2]);
    assert(section[0].z>section[1].z&&section[0].z<=.65f);
    assert(roadLineBank({{0,0,0},{0,30,0},{0,60,0}})[1]==0);

    // A symmetric 90-degree corner on an uphill grade must have a mirrored
    // lift ABOVE its terrain, even with the tall hill-visibility boost.
    std::vector<Vec3> symmetric;
    for(int i=-30;i<=30;++i){const float d=static_cast<float>(i);symmetric.push_back(i<=0?Vec3{0,d+30,(d+30)*.2f}:Vec3{d,30,(d+30)*.2f});}
    const auto rawBank=roadLineBank(symmetric),lift=roadLineLift(symmetric,rawBank,{0,26,0});
    assert(std::fabs(lift[30])>10); // Keep the deliberate tall hill cue.
    for(unsigned d=1;d<20;++d)assert(std::fabs(lift[30-d]-lift[30+d])<.0001f);
    for(std::size_t i=1;i+1<lift.size();++i)assert(std::fabs(lift[i+1]-2*lift[i]+lift[i-1])<1.0f); // No sharp peak/kink.
    const auto flatLift=roadLineLift(symmetric,rawBank,{0,26,20});
    assert(std::fabs(flatLift[30])<1&&std::fabs(flatLift[30])<std::fabs(lift[30]));
    auto mirrored=symmetric;for(auto& point:mirrored)point.x=-point.x;
    const auto opposite=roadLineLift(mirrored,roadLineBank(mirrored),{0,26,0});
    for(std::size_t i=0;i<lift.size();++i)assert(std::fabs(lift[i]+opposite[i])<.0001f);
    const std::vector<Vec3> gps{{0,0,0},{0,30,0},{25,30,3},{25,95,8}};
    const auto ribbon=roadLine(gps,{0,0,0},0);assert(ribbon.visible());
    bool corner=false;for(const auto& point:ribbon.points)if(distance2D(point,{0,30,0})<.001f)corner=true;
    assert(corner);assert(std::fabs(ribbon.points.front().y-4)<2);assert(ribbon.points.back().z>0);
    assert(roadLine({{0,0,0},{0,600,0}},{0,0,0},0).visible());
    assert(!roadLine({{0,0,0}},{0,0,0},0).visible());
    assert(!roadLine(gps,{25,95,8},0).visible());
    const auto quad=roadLineQuad({0,0,0},{0,10,1},.5f);
    assert(std::fabs(distance2D(quad[0],quad[1])-.5f)<.001f&&quad[2].z==1);
    assert(roadDeviceDepth(1,.1f,1000)<roadDeviceDepth(10,.1f,1000));
    assert(roadLineLookAhead(0)==35&&roadLineLookAhead(10)<roadLineLookAhead(20)&&roadLineLookAhead(20)<roadLineLookAhead(40));
    const auto fastRibbon=roadLine({{0,0,0},{0,600,0}},{0,0,0},40);
    const auto slowRibbon=roadLine({{0,0,0},{0,600,0}},{0,0,0},0);
    assert(fastRibbon.ahead>slowRibbon.ahead*5);
    const auto smooth=smoothRoadPoint(gps,1,.9f);assert(smooth.x<0&&std::fabs(smooth.x)<=1.25f);
    const auto endSection=roadLineSection(gps,1,.441f);assert(distance2D(endSection[0],endSection[1])>=.441f);
    const float foreground=std::log(5.0f/.1f)/std::log(1000.0f/.1f);
    const float roadSurface=std::log(20.0f/.1f)/std::log(1000.0f/.1f);
    assert(!roadLineVisible(20,foreground,.1f,1000)); // Car/wall in front hides road.
    assert(roadLineVisible(19.95f,roadSurface,.1f,1000)); // Line just above road is visible.
    assert(!roadLineVisible(22,roadSurface,.1f,1000)); // Terrain in front hides line.
    assert(roadLineCoverage(20,foreground,.1f,1000)==0); // Car/wall still occludes completely.
    assert(roadLineCoverage(19.95f,roadSurface,.1f,1000)>.98f);
    const float coverA=roadLineCoverage(20.01f,roadSurface,.1f,1000),coverB=roadLineCoverage(20.02f,roadSurface,.1f,1000);
    assert(coverA>0&&coverA<1&&coverB>0&&coverB<coverA&&coverA-coverB<.1f); // Smooth coverage instead of on/off jitter.
    for(std::size_t edge=1;edge<gps.size();++edge)for(unsigned sample=0;sample<=20;++sample){
        const float t=sample/20.0f;const auto q=smoothRoadPoint(gps,edge,t);
        const Vec3 linear{gps[edge-1].x+(gps[edge].x-gps[edge-1].x)*t,gps[edge-1].y+(gps[edge].y-gps[edge-1].y)*t,0};
        assert(std::isfinite(q.x)&&std::isfinite(q.y)&&distance2D(q,linear)<=1.251f);
    }
    assert(forkContinuation({0,0,0},{0,30,0},{8,90,0},{0,90,0},0)); // Gentle split needs a longer baseline.
    assert(forkContinuation({0,0,0},{0,30,0},{8,78,0},{0,78,0},0)); // Nodes need not land on a 24m circle.
    assert(!forkContinuation({0,0,0},{0,30,0},{4,78,0},{0,78,0},0)); // Still the same road corridor.

    assert(forkContinuation({0,0,0},{0,30,0},{9,53,2},{0,54,0},0));
    assert(!forkContinuation({0,0,0},{0,30,0},{9,53,2},{0,54,0},90));
    assert(!forkContinuation({0,0,0},{0,30,0},{9,53,2},{0,54,8},0));
    assert(!forkContinuation({0,0,0},{0,30,0},{1,54,0},{0,54,0},0));
    assert(crossStreetArm({0,0,0},{0,20,0},{22,20,0},90));
    assert(!crossStreetArm({0,0,0},{0,20,0},{22,20,8},90)); // Grade-separated crossing.
    assert(!crossStreetArm({0,0,0},{0,20,0},{0,42,0},0)); // Straight fork continuation.
    assert(!crossStreetArm({0,0,0},{0,20,0},{22,20,0},0)); // Parallel street, not a crossing arm.
    std::vector<TurnCue> forkCandidates(3);forkCandidates[0].fork=true;forkCandidates[0].distanceAhead=20;
    forkCandidates[1].fork=true;forkCandidates[1].distanceAhead=45;forkCandidates[1].angleDegrees=-20;
    forkCandidates[2].distanceAhead=100;
    pruneForkCues(forkCandidates);assert(forkCandidates.size()==2&&forkCandidates[0].distanceAhead==20&&!forkCandidates[1].fork);
    forkCandidates[1].distanceAhead=40;pruneForkCues(forkCandidates);
    assert(forkCandidates.size()==1&&!forkCandidates[0].fork); // Real intersection owns its cue space.
    const std::vector<Vec3> shallow{{0,0,0},{0,30,0},{15,70,5},{35,130,10}};
    assert(routeCues(shallow,{0,0,0}).count==0);
    const auto fork=routeCues(shallow,{0,0,0},0,{{0,30,0}});
    assert(fork.count==1&&fork.turns[0].fork);
    assert(fork.turns[0].markers[0].position.x>2); // Onto the selected branch.

    assert(persistentFork(shallow,30,{0,18,0}));
    const std::vector<Vec3> toll{{0,0,0},{0,30,0},{12,60,0},{18,100,0},{0,165,0},{0,280,0}};
    assert(!persistentFork(toll,30,{0,18,0})); // Fan out, then rejoin the same road.
    const std::vector<Vec3> tollLane{{0,0,0},{0,30,0},{6,65,0},{7,170,0},{0,280,0}};
    assert(!persistentFork(tollLane,30,{0,18,0})); // A neighboring toll lane is not another corridor.
    const std::vector<Vec3> laterTurn{{0,0,0},{0,30,0},{.3f,42,0},{.5f,85,0},{90,85,0},{200,85,0}};
    assert(!persistentFork(laterTurn,30,{0,18,0})); // A slight bend before a later intersection is not a ramp.
    const std::vector<Vec3> gentle{{0,0,0},{0,30,0},{.8f,42,0},{5,75,1},{14,115,4},{35,230,10}};
    assert(persistentFork(gentle,30,{0,18,0}));
    const auto earlyRamp=routeCues(gentle,{0,0,0},0,{{0,30,0}});
    assert(earlyRamp.count==1&&earlyRamp.turns[0].fork&&std::fabs(earlyRamp.turns[0].angleDegrees)<4);
    assert(!persistentFork({{0,0,0},{0,30,0},{8,65,0}},30,{0,18,0})); // Insufficient route evidence.
    // A ramp may cross the old road later at a different height.
    assert(persistentFork({{0,0,0},{0,30,0},{20,105,5},{2,170,12},{0,240,15}},30,{0,18,0}));
    assert(forkContinuation({0,18,0},{0,30,0},{20,150,5},{0,150,0},0)); // Far continuation nodes are usable.

    CameraFrame approaching{{0,0,2},{0,0,0},60};
    for(const float side:{-1.0f,1.0f}){
        const std::vector<Vec3> branch{{0,0,0},{0,30,0},{side*15,70,5},{side*35,130,10}};
        const auto split=routeCues(branch,{0,0,0},0,{{0,30,0}});
        assert(split.count==1&&split.turns[0].fork);
        const auto& bank=split.turns[0];
        for(unsigned glyph=1;glyph<4;++glyph){
            const auto& first=bank.markers[0].position;const auto& other=bank.markers[glyph].position;
            assert(std::fabs(first.z-other.z)<.001f);
            assert(std::fabs((other.x-first.x)*bank.approach.x+(other.y-first.y)*bank.approach.y)<.001f);
            assert(distance2D(first,other)>1.7f*glyph);
        }
        const auto& turn=split.turns[0];const auto face=cueVertices(turn,0,1,approaching);
        const Vec3 axis{face[2].x-face[5].x,face[2].y-face[5].y,0};
        assert(std::fabs(axis.x*turn.approach.x+axis.y*turn.approach.y)<.001f);
        assert(axis.x*side>0); // The face is visible, and the chevron points to the chosen side.
        ScreenPoint tip{},tail{};assert(project(face[2],approaching,1280,720,tip));assert(project(face[5],approaching,1280,720,tail));
        assert(std::fabs(tip.x-tail.x)>5); // Not edge-on from the driver's approach.
        auto otherCamera=approaching;otherCamera.rotation.z=35;
        const auto unchanged=cueVertices(turn,0,1,otherCamera);
        for(unsigned i=0;i<6;++i)assert(distance2D(face[i],unchanged[i])<.001f);
        assert(cueMarkerCount(turn,approaching)==4);
    }

    roadnav::TurnCue nearbyA{},nearbyB{};
    for(unsigned i=0;i<4;++i){nearbyA.markers[i].position={0,float(i)*2.3f,0};nearbyB.markers[i].position={4,float(i)*2.3f,0};}
    assert(!roadnav::separatedTurnClusters(nearbyA,nearbyB));
    nearbyA.angleDegrees=90;nearbyB.angleDegrees=-45;
    NavigationCues crowded;crowded.count=2;crowded.turns={nearbyA,nearbyB};preferLaterTurn(crowded);
    assert(crowded.count==1&&crowded.turns[0].angleDegrees==-45); // Later wins even if weaker.
    for(auto& marker:nearbyB.markers)marker.position.x=4.5f;
    assert(roadnav::separatedTurnClusters(nearbyA,nearbyB));

    const std::vector<Vec3> tightTurns{{0,0,0},{0,30,0},{8,30,0},{8,80,0}};
    const auto tight=routeCues(tightTurns,{0,0,0});
    assert(tight.count==1&&tight.turns[0].position.x==8&&tight.turns[0].position.y==30);
    for(const auto& marker:tight.turns[0].markers)assert(marker.direction.y>.99f);
    const std::vector<Vec3> ramp{{0,0,0},{0,50,0},{12,90,8},{30,100,10},{100,100,10}};
    auto cues=routeCues(ramp,{0,10,0});
    assert(cues.count>0 && cues.turns[0].angleDegrees < -15);
    const auto& cue=cues.turns[0];
    assert(cue.followsRoute);
    for(const auto& marker:cue.markers){
        assert(marker.position.x>0 && marker.position.y>50 && marker.position.z>0);
        const auto along=nearestDistanceAlong(ramp,marker.position);
        assert(distance2D(sampleAt(ramp,along).position,marker.position)<.001f);
    }
    TurnCue forward{true,{0,20,0},{0,1,0},0,20};
    CameraFrame camera{{0,0,2},{0,0,0},60};
    assert(cueMarkerCount(forward,camera)==4);
    const auto vertices=cueVertices(forward,0,1,camera);
    ScreenPoint tip{},tail{};
    assert(project(vertices[2],camera,1280,720,tip));
    assert(project(vertices[5],camera,1280,720,tail));
    assert(vertices[2].y>vertices[5].y);
    const auto rearVertices=vertices;
    NavigationCues prior{},next{};prior.count=2;prior.turns[0].position={10,0,0};prior.turns[1].position={40,0,0};next.count=1;next.turns[0]=prior.turns[1];
    const auto departed=departingCues(prior,next);
    assert(departed.count==1&&departed.turns[0].position.x==10);
    assert(departingCues(next,next).count==0);
    assert(retirementOpacity(0)==1&&retirementOpacity(100)==.5f&&retirementOpacity(200)==0&&retirementOpacity(201)==0);
    assert(approachOpacity(4.572f,0)==0);
    assert(approachOpacity(0,0)==0);
    assert(approachOpacity(8.072f,0)==1);
    const float halfway=approachOpacity(6.322f,0);
    assert(halfway>0 && halfway<1);
    assert(approachOpacity(5,0)<halfway && approachOpacity(7,0)>halfway);
    assert(approachOpacity(10,50)<1); // highway fade starts early enough to be visible
    camera.position={20,20,2};
    const auto sideVertices=cueVertices(forward,0,1,camera);
    for(unsigned i=0;i<6;++i)assert(distance2D(rearVertices[i],sideVertices[i])==0);
    assert(cueMarkerCount(forward,camera)==4);
    camera.position={0,40,2};
    assert(cueMarkerCount(forward,camera)==4);
    const std::vector<Vec3> fastRoute{{0,0,0},{0,200,0},{20,200,0},{20,350,0}};
    assert(routeCues(fastRoute,{0,0,0},0).count==0);
    assert(routeCues(fastRoute,{0,0,0},35).count==2);
    assert(turnLookAhead(0)==70 && turnLookAhead(20)>70);
    assert(turnLookAhead(35)>turnLookAhead(20) && turnLookAhead(100)==320);
    assert(turnLookAhead(-10)==70);
    // A mild road alignment change cannot consume a real turn's cue slot.
    const std::vector<Vec3> minorThenTurn{{0,0,0},{0,30,0},{5,50,0},{35,50,0},{35,100,0}};
    const auto actualTurns=routeCues(minorThenTurn,{0,0,0});
    assert(actualTurns.count==2 && actualTurns.turns[0].position.y==50);
    const std::vector<Vec3> straight{{0,-100,0},{0,100,0}};
    WrongWayState wrong;
    for(int i=0;i<5;++i)assert(!wrong.update(straight,{0,0,0},{0,-1,0},{0,-4,0},200));
    assert(wrong.update(straight,{0,0,0},{0,-1,0},{0,-4,0},200));
    assert(wrong.update(straight,{0,0,0},{0,-1,0},{},200));
    assert(!wrong.update(straight,{0,0,0},{0,1,0},{0,4,0},200));
    for(int i=0;i<20;++i){
        assert(!wrong.update(straight,{0,0,0},{0,-1,0},{},200));
        assert(!wrong.update(straight,{0,0,0},{0,-1,0},{0,4,0},200));
        assert(!wrong.update(straight,{10,0,0},{0,-1,0},{0,-4,0},200));
        assert(!wrong.update(straight,{0,0,6},{0,-1,0},{0,-4,0},200));
    }
    // One rounded bend must not create adjacent forward/sideways clusters.
    const std::vector<Vec3> rounded{{0,0,0},{0,30,0},{4,40,0},{18,44,0},{100,44,0}};
    const auto roundCues=routeCues(rounded,{0,5,0});
    assert(roundCues.count==1);
    const auto uturn=uTurnCues({0,0,0},{0,1,0});
    assert(uturn.count==1 && uturn.turns[0].kind==CueKind::UTurn);
    camera={{0,0,2},{0,0,0},60};
    assert(cueMarkerCount(uturn.turns[0],camera)==6);
    for(unsigned marker=0;marker<6;++marker)for(const auto& v:cueVertices(uturn.turns[0],marker,1,camera)){
        ScreenPoint p;assert(project(v,camera,1280,720,p));
    }
    auto linked=routeCues({{0,0,0},{0,30,0},{20,30,0},{20,150,0}},{0,5,0});
    assert(linked.count==2);
    const auto sameSide=routeCues({{0,0,0},{0,30,0},{20,30,0},{20,-120,0}},{0,5,0});
    assert(sameSide.count==2); // distinct same-direction tight turns survive

    assert(routeCues(straight,{0,90,0}).destination.visible);
    assert(!routeCues(straight,{0,-90,0}).visible());
    std::puts("RoadNav directional geometry, speed preview, wrong-way and destination tests passed");
}
