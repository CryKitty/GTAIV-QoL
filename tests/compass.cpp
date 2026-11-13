#include "../src/compass.hpp"
#include "../src/compass_notifications.hpp"
#include "../src/compass_overlay.hpp"
#include "../src/compass_waypoint.hpp"
#include "../src/compass_feedback.hpp"
#include "../src/qol_menu_labels.hpp"
#include "../src/wanted_compass.hpp"
#include "../src/date_compass.hpp"
#include <cwchar>
#include "../src/navigation_awareness.hpp"
#include "../src/police_escape.hpp"
#include "../src/escape_gps_request.hpp"
#include <set>
#include <cassert>
#include <cstdio>
#include <limits>
struct AlphaNative {
    static int next,calls;
    template<std::uint32_t Hash,class R> static R invoke(std::uint32_t handle,int* alpha){
        static_assert(Hash==0x297af6c8u,"Correct opacity native");
        assert(handle==0x00050002u && alpha!=nullptr);++calls;*alpha=next;
        return 0; // No opacity in the return value; only the output argument counts.
    }
};
int AlphaNative::next=128;int AlphaNative::calls=0;
struct FakeBlips {
    std::vector<unsigned> firstCalls,nextCalls;
    unsigned index=0;
    std::uint32_t first(unsigned category){
        assert(category<128);firstCalls.push_back(category);index=0;
        if(category==1)return 101;
        if(category==4)return 101; // Duplicate across categories must not hide a later marker.
        if(category==8)return 808; // Waypoint sprite is not excluded as a player icon.
        return 0;
    }
    std::uint32_t next(unsigned category){
        assert(category<128);nextCalls.push_back(category);++index;
        if(category==1)return 101; // Broken iterator cycle must terminate.
        if(category==4 && index==1)return 404;
        return 0;
    }
    bool exists(std::uint32_t handle){return handle==101||handle==404||handle==808;}
};
struct RadarTable {
    std::uint32_t handleAt(unsigned slot){
        if(slot==2)return 0x00050002u;
        if(slot==3)return 0x00060003u;
        if(slot==4)return 0x00050002u; // Stale pointer in the wrong slot.
        if(slot==5)return 0x00060005u; // Native reports deleted.
        return 0;
    }
    bool exists(std::uint32_t handle){return handle==0x00050002u||handle==0x00060003u;}
};
struct FakeGps {
    std::set<unsigned> blips{1,2},active{1};bool movable=true,addSucceeds=true;unsigned moves=0,removes=0;
    bool exists(unsigned h){return blips.count(h)!=0;}
    bool same(unsigned a,unsigned b){return a==b;}
    void route(unsigned h,bool enabled){if(enabled)active.insert(h);else active.erase(h);assert(active.size()<=1);}
    bool move(unsigned h,roadnav::Vec3){assert(!active.count(h)&&!active.count(1));++moves;return movable;}
    void remove(unsigned h){assert(!active.count(h));blips.erase(h);++removes;}
    unsigned add(roadnav::Vec3){assert(!blips.count(2));if(!addSucceeds)return 0;blips.insert(3);return 3;}
    void configure(unsigned){}
};
int main(){
    {
        wantedcompass::Background bg;
        auto red=bg.update({true,2,false,false},true,0),blue=bg.update({true,2,false,false},true,400);
        assert(red.a&&blue.a&&red.r>red.b&&blue.b>blue.r&&red.a<=85);
        assert(!bg.update({true,2,true,false},true,500).a); // lost sight within zone
        assert(bg.update({true,2,true,true},true,600).g>100);
        const auto fade=bg.update({true,0,false,false},true,700);
        assert(fade.a==80&&bg.update({true,0,false,false},true,1600).a<fade.a);
        assert(!bg.update({true,0,false,false},true,2500).a);
        assert(!bg.update({true,2,false,false},false,2600).a);
        assert(!bg.update({false,2,false,false},true,2700).a); // missing native => no guess
        assert(std::wcsstr(qolmenu::label("QOL_UPDATES",false),QOL_VERSION_WIDE));
        policeescape::ReplanState reset;
        assert(reset.observe(2,false,{0,0,0},250,false,{0,1,0}));reset.begin(0);reset.committed();
        assert(reset.observe(2,false,{0,0,0},250,true,{1,0,0})); // sighting reset without flashing transition
        assert(reset.pending&&reset.heading.x==1&&!reset.ready(200));
        assert(reset.observe(2,false,{60,0,0},250,true,{1,0,0}));
        assert(reset.heading.x==1&&reset.ready(2500));
        reset.begin(2500);assert(reset.heading.x==1); // requested course survives backoff/begin
        assert(policeescape::followsHeading({{0,0,0},{30,0,0},{400,0,0}}, {},reset.heading));
        assert(!policeescape::followsHeading({{0,0,0},{-30,0,0},{400,0,0}}, {},reset.heading));
        auto choices=policeescape::candidates({}, {},250,0,[](roadnav::Vec3 a,roadnav::Vec3& b){b=a;return true;},[](roadnav::Vec3 a,roadnav::Vec3 b){return roadnav::distance2D(a,b);},{},true);
        assert(!choices.empty());for(const auto& choice:choices)assert(choice.point.x>0);
    }

    {
        // Arriving at one destination is not the end of a wanted pursuit.
        using roadnav::Vec3;
        const std::vector<Vec3> old{{0,0,0},{300,0,0}};
        const Vec3 player{300,0,0},travel{1,0,0};
        assert(policeescape::remaining(old,player)==0);
        assert(policeescape::replanDue(false,true,100,0,0));
        assert(!policeescape::replanDue(true,true,4999,0,0,true,20));
        assert(policeescape::replanDue(true,true,5000,0,0,true,20)); // continuous sight, unchanged zone/risk
        assert(!policeescape::replanDue(true,true,5000,0,0,false,20));
        assert(!policeescape::replanDue(true,true,5000,0,0,true,0));
        const auto choices=policeescape::candidates(player,{},150,0,
            [](Vec3 a,Vec3& b){b=a;return true;},
            [](Vec3 a,Vec3 b){return roadnav::distance2D(a,b);},{},true,policeescape::planningLead(30));
        assert(!choices.empty());
        const std::vector<Vec3> next{player,choices.front().point}; // stand-in for returned native GPS
        assert(next.back().x>player.x);
        assert(policeescape::usableReplacement(next,player,{},150,travel,30));
        assert(!policeescape::usableReplacement(old,player,{},150,travel,30));
        assert(!policeescape::usableReplacement(next,player,{},150,{-1,0,0},30));
        assert(!policeescape::usableReplacement(next,player,next.back(),150,travel,30));
        policeescape::ReplanState retry;
        retry.observe(2,false,{},150,true,travel);retry.begin(0);retry.failed(200);
        // Continuous moving search circles cannot cancel failure backoff.
        for(unsigned t=1000;t<5000;t+=1000){
            retry.observe(2,false,{static_cast<float>(t),0,0},150,true,travel);
            assert(!retry.ready(t));
        }
        assert(retry.ready(5200));retry.begin(5200);retry.failed(5400);
        retry.observe(2,false,{6000,0,0},150,true,travel);
        assert(!retry.ready(15399)&&retry.ready(15400));
        assert(policeescape::planningLead(30)==360&&policeescape::planningLead(0)==180);
    }

    assert(compass::waypointMiles(1609.344f)=="1.00 mi");
    assert(compass::waypointMiles(804.672f)=="0.50 mi");
    assert(compass::waypointMiles(16093.44f)=="10.0 mi");
    assert(compass::waypointMiles(-1).empty());
    assert(compass::waypointMiles(0)=="0.00 mi");
    assert(compass::waypointMiles(16077.347f)=="9.99 mi");
    assert(compass::waypointMiles(16898.112f)=="10.5 mi");
    assert(compass::waypointMiles(std::numeric_limits<float>::infinity()).empty());
    assert(compass::waypointLabelY(true)>compass::waypointLabelY(false));
    assert(!compass::hoverReadout(42,42,true,false)); // pinned waypoint has its own persistent rows
    assert(compass::hoverReadout(7,42,true,false)); // other icons still hover
    assert(!compass::hoverReadout(7,42,false,false));
    assert(!compass::hoverReadout(7,42,true,false,true)); // separate original objective blip represented by pin
    assert(!compass::hoverReadout(7,42,false,true,true)); // holding still has ring/caption, no duplicate distance
    assert(compass::hoverReadout(8,42,true,false,false)); // unrelated objective/icon remains hoverable
    assert(compass::hoverReadout(7,0,true,false,false)); // without persistent pin, objective still hovers
    assert(awareness::destinationRouteKind(false,false,false)==awareness::RouteKind::Mission);
    assert(awareness::destinationRouteKind(false,false,true)==awareness::RouteKind::Custom);
    assert(awareness::destinationRouteKind(true,false,false)==awareness::RouteKind::Enemy);
    assert(awareness::destinationRouteKind(false,true,false)==awareness::RouteKind::Friend);


    {
        // A newly created contact/date sprite is discovered without radar-table
        // addresses, survives mission/range gates, and wins overlap with shops.
        struct LateDate {
            bool initiated=false;
            unsigned first(unsigned sprite){return initiated&&sprite==54?5401u:0u;}
            unsigned next(unsigned){return 0u;}
            bool exists(unsigned handle){return initiated&&handle==5401u;}
        } backend;
        assert(compass::collectBlips(backend).empty());backend.initiated=true;
        const auto discovered=compass::collectBlips(backend);assert(discovered.size()==1&&discovered[0]==5401);
        assert(compass::markerInRange(8000,100,true,54));assert(!compass::hideAmbientMissionMarker(true,false,false,false,54));
        const auto icons=compass::layout({{11,21,1,20,0,255,0},{5401,54,1,8000,0,255,0}});
        assert(icons.size()==1&&icons.front().marker.handle==5401);
        assert(std::strcmp(compass::fallbackSymbol(54),"<3")==0);
        assert(std::strcmp(compass::fallbackSymbol(8),"X")==0);
        using namespace datecompass;
        std::array<std::uint32_t,argumentWords> words{};words[0]=123;words[2]=3;words[14]=venueCount;
        for(unsigned i=0;i<venueCount;++i)words[15+i]=50;
        words[15]=70;words[16]=0;words[17]=35;words[18]=95;
        auto prefs=preferences(words);assert(prefs.valid&&prefs.girlfriend==3);
        Session session;assert(!session.current(false,1).valid);session.begin(prefs,2);
        assert(!session.current(false,3).valid);assert(session.current(true,4).valid);
        assert(!session.current(false,5).valid);assert(!session.current(true,6).valid); // no stale girlfriend
        session.begin(prefs,0xfffffff0u);assert(session.current(true,32).valid);
        auto invalid=words;invalid[14]=28;assert(!preferences(invalid).valid);
        invalid=words;invalid[2]=5;assert(!preferences(invalid).valid);
        invalid=words;invalid[15]=101;assert(!preferences(invalid).valid);
        Table table{};table[0]=venueCount;
        for(unsigned i=0;i<venueCount;++i){auto* row=table.data()+1+i*recordWords;
            std::memcpy(row,"VENUE",6);row[8]=21;row[9]=1;
            const roadnav::Vec3 point{100.0f+100*i,200,10};std::memcpy(row+10,&point,sizeof(point));}
        std::array<Venue,venueCount> places{};assert(venues(table,places));
        assert(match(prefs,places,true,21,{100,200,10})==Preference::Liked);
        assert(match(prefs,places,true,21,{200,200,10})==Preference::Disliked);
        assert(preference(35)==Preference::Neutral&&preference(50)==Preference::Neutral&&preference(34)==Preference::Disliked&&preference(95)==Preference::Liked);
        assert(colour(Preference::Liked)==8&&colour(Preference::Disliked)==9&&colour(Preference::Unknown)==0xffffffffu&&colour(Preference::Neutral)==0xffffffffu);
        assert(match(prefs,places,false,21,{100,200,10})==Preference::Unknown);
        assert(colour(match(prefs,places,false,21,{100,200,10}))==0xffffffffu);
        assert(match({},places,true,21,{100,200,10})==Preference::Unknown);
        assert(match(prefs,places,true,22,{100,200,10})==Preference::Unknown);
        places[1]=places[0];assert(match(prefs,places,true,21,{100,200,10})==Preference::Unknown);
        table[0]=28;assert(!venues(table,places));table[0]=venueCount;
        table[1+8]=200;assert(!venues(table,places));
        // A hostile ped riding in a vehicle is a moving vehicle chase candidate,
        // even when an unowned/stale GPS buffer exists. Real goals still win.
        const std::vector<compass::ChaseCandidate> chase{{77,450,20,true}};
        assert(compass::chooseChaseTarget(chase,0)==77);
        assert(compass::chaseEnabled(false,true,true)); // Pause-menu On overrides old hidden gate.
        assert(!compass::chaseEnabled(false,true,false));
        assert(!compass::chaseEnabled(false,false,true));
        assert(compass::chaseEnabled(true,false,false)); // Legacy turn-cue behaviour unchanged.
        assert(compass::allowChase(true,false,false,false));
        assert(!compass::allowChase(true,true,false,false));
        assert(!compass::allowChase(true,false,false,true));
        awareness::LineTypes switches;switches.enemy=false;
        assert(!switches.visible(true,awareness::RouteKind::Enemy));switches.enemy=true;
        assert(switches.visible(true,awareness::RouteKind::Enemy));
    }

    {
        // The SDK's shared heart/date sprite ignores distance on the compass,
        // including while a mission filters ordinary ambient contact icons.
        assert(!compass::hideAmbientMissionMarker(true,false,false,false,54));
        assert(compass::hideAmbientMissionMarker(true,false,false,false,52));
        assert(compass::dateMarker(54));assert(!compass::dateMarker(53)&&!compass::dateMarker(55));
        for(float distance:{0.0f,50.0f,5000.0f,100000.0f}){
            assert(compass::markerInRange(distance,100,true,54));
            assert(compass::markerInRange(distance,100,false,3)); // mission/long-range unchanged
        }
        assert(!compass::markerInRange(5000,100,true,52)); // ordinary POI still local
        assert(!compass::markerInRange(-1,100,true,54));
        assert(!compass::markerInRange(std::numeric_limits<float>::infinity(),100,true,54));
        const auto far=compass::layout({{123,54,0,5000,7,255,0,false,false}});
        assert(far.size()==1&&far.front().marker.distance==5000); // distance is not rewritten
        const char* keys[]={"QOL_ESCAPE","QOL_MISSION","QOL_FRIEND","QOL_ENEMY","QOL_CUSTOM"};
        for(const auto key:keys){
            assert(std::wcsncmp(qolmenu::label(key,true),L"    ",4)==0);
            assert(!std::wcsstr(qolmenu::label(key,true),L"~c~"));
            assert(std::wcsstr(qolmenu::label(key,false),L"~c~"));
            assert(std::wcsstr(qolmenu::label(key,false),L"~s~"));
        }
        assert(!qolmenu::label("QOL_TAXI",false));
    }

    {
        // All hold/status/arrival messages use the same objective-label slot.
        compass::WaypointFeedback feedback;
        const char* messages[]={"Setting waypoint","Waypoint set","Waypoint cleared","Waypoint unavailable","Destination reached"};
        for(const auto message:messages){
            feedback.show(message,100,42,.53f,1800);feedback.track(42,.54f);
            const auto label=feedback.draw(101);
            assert(label.visible()&&label.x==.54f&&label.y==compass::StripLayout::focusLabelY);
            assert(label.y>compass::StripLayout::iconY&&label.x<=compass::StripLayout::right);
            assert(label.size==.22f&&label.width==.85f&&label.height==1.10f&&label.alpha==235);
            feedback.track(99,.3f);assert(feedback.draw(1899).x==.54f);
            assert(!feedback.draw(1900).visible());
        }
        feedback.show("Waypoint set",0xfffffff0u,42,.5f);assert(feedback.draw(32).visible());
        const auto held=compass::caption("Setting waypoint",.51f),done=compass::caption("Waypoint set",.51f);
        assert(held.x==done.x&&held.y==done.y&&held.size==done.size);
        using Kind=awareness::RouteKind;
        const Kind kinds[]={Kind::Escape,Kind::Mission,Kind::Friend,Kind::Enemy,Kind::Custom};
        awareness::LineTypes defaults;
        for(const auto kind:kinds){assert(defaults.visible(true,kind));assert(!defaults.visible(false,kind));}
        for(unsigned off=0;off<5;++off){awareness::LineTypes types{off!=0,off!=1,off!=2,off!=3,off!=4};
            for(unsigned i=0;i<5;++i){assert(types.visible(true,kinds[i])==(i!=off));assert(!types.visible(false,kinds[i]));}}
        assert(!awareness::LineTypes({true,true,false,true,true}).visible(true,Kind::Blue));
    }

    using roadnav::Vec3;
    // Vehicle status no longer gates this shared policy. Preserve the existing
    // phone/combat gates and one action per fresh hold.
    compass::WaypointHold vehicleHold;
    for(unsigned t=0;t<900;t+=100)assert(!vehicleHold.update(true,compass::waypointEligible(false,false),42,t));
    assert(vehicleHold.update(true,compass::waypointEligible(false,false),42,900)==42);
    assert(!vehicleHold.update(true,true,42,1000));
    vehicleHold.update(false,true,42,1100);
    assert(!vehicleHold.update(true,compass::waypointEligible(true,false),42,1200));
    assert(!vehicleHold.update(true,true,42,1300));
    assert(!compass::waypointEligible(false,true));
    assert(!compass::waypointEligible(true,true));
    // Entry and exit cancel the old context's hold until release. A fresh hold
    // still works inside a vehicle; repeated vehicle ticks cannot re-arm it.
    compass::WaypointVehicleGate vehicleGate;compass::WaypointHold transitionHold;
    assert(!vehicleGate.update(false));
    for(unsigned t=0;t<800;t+=100)transitionHold.update(true,true,42,t);
    assert(vehicleGate.update(true));transitionHold.update(true,false,0,800);
    for(unsigned t=900;t<2000;t+=100){assert(!vehicleGate.update(true));assert(!transitionHold.update(true,true,42,t));}
    assert(transitionHold.blocked&&transitionHold.target==0&&transitionHold.progress(2000)==0);
    transitionHold.update(false,true,42,2000);
    for(unsigned t=2100;t<3000;t+=100)assert(!transitionHold.update(true,true,42,t));
    assert(transitionHold.update(true,true,42,3000)==42);
    assert(vehicleGate.update(false));transitionHold.update(true,false,0,3100);
    assert(transitionHold.blocked&&transitionHold.target==0);
    policeescape::ReplanState planner;
    assert(planner.observe(2,false,{0,0,0},250)&&planner.pending&&planner.ready(0));
    planner.begin(0);planner.committed();
    assert(!planner.observe(2,false,{1,1,0},251)&&!planner.pending);
    assert(planner.observe(2,true,{0,0,0},250)); // Police lose the player.
    assert(!planner.ready(2499)&&planner.ready(2500));
    planner.begin(2500);
    assert(planner.observe(2,false,{0,0,0},250)); // Re-acquired mid-calculation.
    assert(planner.pending&&!planner.ready(2600));
    assert(planner.observe(3,false,{0,0,0},400));
    assert(planner.observe(3,false,{30,0,0},400));
    assert(planner.observe(3,false,{30,0,0},420));
    // Failure never disarms retries; backoff is bounded and survives wrap.
    planner.failed(5000);assert(planner.pending&&!planner.ready(9999)&&planner.ready(10000));
    planner.failed(10000);assert(!planner.ready(19999)&&planner.ready(20000));
    planner.failed(20000);assert(planner.delay()==10000&&planner.ready(30000));
    planner.begin(30000);planner.committed();assert(!planner.pending&&planner.failures==0);
    planner.failed(0xfffffff0u);assert(!planner.ready(0x100u)&&planner.ready(0x1380u));
    planner={};assert(!planner.observed&&planner.ready(0)); // Wanted cleared/new chase.

    FakeGps gps;
    assert(policeescape::requestGps(2u,1u,Vec3{10,20,0},gps)==2&&gps.moves==1&&gps.removes==0);
    assert(gps.active==std::set<unsigned>{2});
    policeescape::restorePrimaryGps(2u,1u,gps);assert(gps.active==std::set<unsigned>{1});
    gps.movable=false;
    assert(policeescape::requestGps(2u,1u,Vec3{20,30,0},gps)==3&&gps.removes==1);
    assert(gps.active==std::set<unsigned>{3});
    FakeGps failed;failed.movable=false;failed.addSucceeds=false;
    assert(!policeescape::requestGps(2u,1u,Vec3{},failed));assert(failed.active==std::set<unsigned>{1});

    Vec3 selected{};unsigned samples=0;
    const auto snap=[&](Vec3 a,Vec3& b){++samples;b=a;return true;};
    const auto cost=[](Vec3 a,Vec3 b){return roadnav::distance2D(a,b)+10;};
    assert(policeescape::destination({90,0,0},{0,0,0},150,0,snap,cost,selected));
    assert(samples==16&&selected.x>290&&std::fabs(selected.y)<1);
    assert(policeescape::destination({250,0,0},{0,0,0},150,0,snap,cost,selected));
    assert(selected.x>390); // Wanted persists outside the circle: keep an outward road route.
    assert(!policeescape::destination({0,0,0},{0,0,0},150,0,snap,[](Vec3,Vec3){return -1.0f;},selected));
    assert(!policeescape::destination({0,0,0},{0,0,0},150,0,[](Vec3,Vec3& b){b={0,0,0};return true;},cost,selected));
    // The safer road wins even if it is longer. Equal endpoints alone cannot
    // distinguish these paths: only one passes the visible police location.
    const std::vector<Vec3> direct{{0,0,0},{150,0,0},{300,0,0}};
    const std::vector<Vec3> detour{{0,0,0},{0,160,0},{300,160,0},{300,0,0}};
    assert(policeescape::pathScore(direct,{0,0,0},{})<policeescape::pathScore(detour,{0,0,0},{}));
    assert(policeescape::pathScore(direct,{0,0,0},{{150,0,0}})>policeescape::pathScore(detour,{0,0,0},{{150,0,0}}));
    // Police behind the car and on an isolated elevated road are not treated
    // as blockers of the remaining path.
    assert(policeescape::pathScore(direct,{290,0,0},{{0,0,0}})==policeescape::pathScore(direct,{290,0,0},{}));
    assert(policeescape::pathScore(direct,{0,0,0},{{150,0,80}})==policeescape::pathScore(direct,{0,0,0},{}));
    const std::vector<Vec3> dense{{0,0,0},{75,0,0},{150,0,0},{225,0,0},{300,0,0}};
    assert(std::fabs(policeescape::pathScore(direct,{0,0,0},{{150,0,0}})-policeescape::pathScore(dense,{0,0,0},{{150,0,0}}))<1);
    assert(policeescape::remaining(direct,{290,0,0})<11);
    assert(policeescape::needsForwardRoute(direct,{290,0,0},20)); // Old line has run out.
    assert(policeescape::needsForwardRoute(direct,{150,45,0},20)); // Old road is behind/off route.
    assert(!policeescape::needsForwardRoute(direct,{100,0,0},20));
    assert(policeescape::refreshLead(30)==180); // Request before driving past the endpoint.
    assert(policeescape::refreshLead(0)==100);
    assert(policeescape::onPath(direct,{150,3,0}));
    assert(!policeescape::onPath(direct,{150,25,0}));
    assert(!policeescape::replanDue(true,true,4000,100,100));
    assert(!policeescape::replanDue(true,true,6000,100,200));
    assert(policeescape::replanDue(true,true,6000,100,500));
    assert(policeescape::replanDue(true,true,12000,100,100));
    assert(policeescape::replanDue(false,true,2000,100,100));
    assert(policeescape::replanDue(true,false,2000,100,100));
    // A marker outside the circle is insufficient: the returned GPS endpoint
    // can be short of it, or the police can move the circle while it calculates.
    const std::vector<Vec3> stopsInside{{0,0,0},{140,0,0}};
    assert(policeescape::matches(stopsInside,{180,0,0}));
    assert(!policeescape::exits(stopsInside,{0,0,0},150));
    assert(policeescape::exits(direct,{0,0,0},150));
    assert(!policeescape::exits(direct,{200,0,0},150)); // Circle reset invalidates old endpoint.
    assert(policeescape::matches(direct,{300,0,0}));
    // A populated stale slot must not win over the other slot's requested route.
    const std::vector<Vec3> staleSlot{{0,0,0},{0,300,0}},matchingSlot{{0,0,0},{300,0,0}};
    assert(policeescape::endpointError(matchingSlot,{300,0,0})<policeescape::endpointError(staleSlot,{300,0,0}));
    assert(policeescape::endpointError({{0,0,0},{300,0,30}},{300,0,0})>policeescape::endpointError(matchingSlot,{300,0,0}));
    assert(policeescape::routeWait(0)>=6000&&policeescape::routeWait(20)<policeescape::routeWait(0));

    assert(!policeescape::matches(direct,{300,200,0}));
    auto options=policeescape::candidates({90,0,0},{0,0,0},150,0,snap,cost,{{200,0,0}});
    assert(!options.empty()&&std::fabs(options.front().point.y)>100);
    for(const auto& option:options)assert(roadnav::distance2D(option.point,{0,0,0})>=270); // 60m outside plus GPS endpoint slack.
    assert(awareness::routeColour(awareness::RouteKind::Escape,false,0)!=awareness::routeColour(awareness::RouteKind::Escape,false,400));

    assert(compass::missionYellow(5)&&compass::missionYellow(14));
    assert(compass::missionYellow(0xffff20)&&!compass::missionYellow(9)&&!compass::missionYellow(4));

    assert(awareness::objectiveLabel(true,false,false,0,false,1000));
    assert(awareness::objectiveLabel(true,false,false,8,false,20));
    assert(!awareness::objectiveLabel(true,false,false,8,false,100));
    assert(!awareness::objectiveLabel(true,true,false,0,true,0));
    assert(!awareness::objectiveLabel(false,false,false,0,true,0));

    assert(awareness::rearTaxiFov(40,120,true,1)==120);
    assert(awareness::rearTaxiFov(45,120,true,1)==120);
    assert(awareness::rearTaxiFov(40,121,true,1)==40);
    assert(awareness::rearTaxiFov(40,80,true,1)==80);
    assert(awareness::rearTaxiFov(45,80,true,1)==85); // Retain camera zoom movement.
    assert(awareness::rearTaxiFov(30,80,true,1)==30); // Destination-selection camera.
    assert(awareness::rearTaxiFov(40,80,false,1)==40); // Driver/front passenger/other cars.
    assert(awareness::rearTaxiFov(40,80,true,10)==40); // Exterior camera.
    assert(awareness::rearTaxiFov(40,0,true,1)==40); // Feature disabled.
    assert(compass::hostileMarkerType(1)&&compass::hostileMarkerType(2));
    assert(!compass::hostileMarkerType(4)&&!compass::hostileMarkerType(5));
    assert(!awareness::attacker(true,false,false,10,false)); // Accidental vehicle contact.
    assert(awareness::attacker(true,false,false,10,true)); // Damage plus active aggression.
    assert(awareness::attacker(false,true,false,10,false)); // Explicit attack order.
    assert(!awareness::attacker(true,true,true,10,true));

    assert(std::fabs((compass::StripLayout::lineY-compass::raisedMeterY(800))*800-10)<.001f);
    assert(std::fabs((compass::StripLayout::lineY-compass::raisedMeterY(1080))*1080-10)<.001f);
    assert(compass::iconScale(false,false)==.90f&&compass::iconScale(true,false)==.85f&&compass::iconScale(false,true)==.85f);
    using awareness::RouteKind;
    assert(awareness::activeRoute(RouteKind::Mission,false,true)==RouteKind::Custom);
    assert(awareness::activeRoute(RouteKind::Custom,true,true)==RouteKind::Enemy);
    assert(awareness::activeRoute(RouteKind::Mission,false,false)==RouteKind::Mission);
    assert((awareness::routeColour(RouteKind::Enemy,true)==std::array<unsigned,3>{235,55,45}));
    assert((awareness::routeColour(RouteKind::Custom,true)==std::array<unsigned,3>{85,210,95}));
    assert(awareness::routeColour(RouteKind::Mission,true)!=awareness::routeColour(RouteKind::Custom,true));
    assert(awareness::routeColour(RouteKind::Enemy,false)==awareness::routeColour(RouteKind::Blue,true));
    assert(awareness::attacker(true,false,false,25)&&awareness::attacker(false,true,false,25));
    assert(!awareness::attacker(false,false,false,25)&&!awareness::attacker(true,false,true,25)&&!awareness::attacker(true,false,false,250));
    assert(awareness::validPool({0x10000,0x20000,100,0xF00},0x1205,5));
    assert(!awareness::validPool({0x10000,0x20000,100,0xF00},0x1205,6));
    assert(!awareness::validPool({0x10000,0x20000,1000000,0xF00},0x1205,5));
    assert(awareness::poolHandle(18,5)==0x1205&&awareness::poolHandle(18,0x85)==0);
    assert(awareness::briefText("Go to the ~y~old hospital~s~ and get ~a~ back.",{"Elizabeta's coke"})=="Go to the old hospital and get Elizabeta's coke back.");
    assert(awareness::briefText("Collect ~1~ of ~1~ packages.",{"2","10"})=="Collect 2 of 10 packages.");
    const auto brief=awareness::briefLines("Go to the old hospital and get Elizabeta's coke back. Then escape the police and return to the contact safely.");
    assert(brief.size()==2&&brief[0].size()<=58);
    compass::Hostility hostility;
    assert(!hostility.enemy(11,22,false)); // Neutral grey stays grey.
    hostility.peds[22]=true;
    assert(hostility.enemy(11,22,false)); // Explicit enemy starts grey.
    hostility.blips[11]=true;
    assert(hostility.friendly(11,22));
    assert(!hostility.enemy(11,22,true)); // Friendly overrides even a red tint.
    hostility.blips[12]=false;
    assert(hostility.enemy(12,0,false)); // Explicit hostile vehicle/blip.
    assert(!hostility.enemy(13,23,false)); // Other handles remain neutral.
    compass::DamageFlash damage,armourDamage;
    damage.update(.8f,0);assert(damage.alpha(0)==0); // Initial load is quiet, even at tick zero.
    damage.update(.5f,1100);assert(damage.value==.5f&&damage.alpha(1100)==240);
    assert(damage.alpha(1210)==0&&damage.alpha(1320)==240);
    assert(damage.alpha(1980)>0&&damage.alpha(2100)==0);
    damage.update(.4f,1900);assert(damage.alpha(1900)==240); // Another hit restarts the pulse.
    damage.update(.9f,2000);assert(damage.alpha(2000)==0); // Healing does not flash red.
    armourDamage.update(.2f,1000);armourDamage.update(0,1100);
    assert(armourDamage.alpha(1100)==240&&damage.alpha(1100)==0); // Depleted armour still flashes; health is independent.
    damage.update(.8f,0xffffff80u);assert(damage.alpha(0)==0&&damage.alpha(0x5cu)>0); // Clock wrap.
    compass::MessageNotice notice;
    assert(notice.alpha(0)==0);notice.update(true,1000);
    assert(notice.waiting&&notice.alpha(1000)==240&&notice.alpha(1300)==120);
    assert(notice.alpha(60000)==240); // Unread notice persists beyond arrival blink.
    notice.update(true,60100);assert(notice.alpha(60400)==120); // Another text announces again.
    notice.update(false,61000);assert(!notice.waiting&&notice.alpha(61000)==0);
    notice.update(true,0xffffff80u);assert(notice.alpha(0)>0);
    for(float aspect:{4.0f/3,16.0f/10,16.0f/9,21.0f/9}){
        const auto envelope=compass::messageEnvelope(aspect);
        assert(envelope.x-envelope.width/2>0&&envelope.x+envelope.width/2<compass::StripLayout::left-.01f);
        assert(envelope.y==.040f&&envelope.y<compass::StripLayout::iconY&&envelope.height>0);
        assert(envelope.x+envelope.width/2<compass::StripLayout::left-.02f);
    }
    const auto icon=compass::messageEnvelope(1.6f);
    assert(icon.height*800<=14.1f&&icon.width*1280<=22.5f);
    const auto mesh=compass::envelopeMesh(100,100,icon.width*1280,icon.height*800,240,1);
    assert(mesh.size()==108);bool transparent=false,opaque=false;
    for(const auto& v:mesh){assert(std::isfinite(v.x)&&std::isfinite(v.y)&&v.alpha<=240);transparent=transparent||v.alpha==0;opaque=opaque||v.alpha==240;}
    assert(transparent&&opaque); // Each continuous stroke has AA fringe coverage.
    assert(compass::holdRingMesh(100,100,20,0,235,2).empty());
    const auto quarter=compass::holdRingMesh(100,100,20,.25f,235,2);
    const auto halfRing=compass::holdRingMesh(100,100,20,.5f,235,2);
    const auto fullRing=compass::holdRingMesh(100,100,20,1,235,2);
    assert(quarter.size()==24*18&&halfRing.size()==48*18&&fullRing.size()==96*18);
    for(const auto& vertex:quarter){assert(vertex.x>=99.99f&&vertex.y<=100.01f);assert(vertex.alpha<=235);}
    for(const auto& vertex:fullRing){const float radius=std::hypot(vertex.x-100,vertex.y-100);assert(radius>=18.1f&&radius<=21.9f);}
    assert(compass::sameWaypointDestination({10,20,1},{11,20,1})); // Contact vs pin.
    assert(!compass::sameWaypointDestination({10,20,1},{11,20,12})); // Separate floor.
    assert(compass::waypointReached({10,20,1},{20,20,1})); // Arrive on foot or in car.
    assert(!compass::waypointReached({10,20,1},{24,20,1}));
    assert(!compass::waypointReached({10,20,1},{10,20,12}));
    for(unsigned venue:{21u,22u,47u,49u,51u,52u,57u,66u,70u,71u})assert(compass::socialVenue(venue));
    for(unsigned other:{8u,27u,29u,42u,50u,58u,59u,69u})assert(!compass::socialVenue(other));
    compass::WaypointHold hold;
    assert(!hold.update(true,true,42,1000));
    assert(!hold.update(true,true,42,1200)&&hold.progress(1200)>0);
    for(unsigned t=1400;t<1900;t+=200)assert(!hold.update(true,true,42,t));
    assert(hold.update(true,true,42,1900)==42);
    assert(!hold.update(true,true,42,2000)); // One pin per hold.
    hold.update(false,true,42,2100);hold.update(true,true,42,2200);
    hold.update(true,true,43,2300);assert(!hold.update(true,true,43,2400)&&hold.progress(2400)==0); // Changing focus cancels.
    hold.update(false,true,43,2500);hold.update(true,false,43,2600);
    assert(!hold.update(true,true,43,2700)&&hold.progress(2700)==0); // Holding through phone/menu cannot arm.
    hold.update(false,true,43,2800);hold.update(true,true,43,2900);
    assert(!hold.update(true,true,43,3900)&&hold.progress(3900)==0); // Pause/frame gap cancels.
    hold.update(false,true,43,4000);hold.update(true,true,43,0xffffff00u);
    for(unsigned t=100;t<900;t+=100)assert(!hold.update(true,true,43,0xffffff00u+t));
    assert(hold.update(true,true,43,0xffffff00u+900)==43); // Clock wrap.
    assert(compass::ambientMissionSprite(29)&&compass::ambientMissionSprite(32)&&compass::ambientMissionSprite(27));
    assert(!compass::ambientMissionSprite(0)&&!compass::ambientMissionSprite(8)&&!compass::ambientMissionSprite(65)&&!compass::ambientMissionSprite(94));
    assert(compass::enemyColour(9)&&compass::enemyColour(1)&&compass::enemyColour(0xFFFF3030u));
    assert(!compass::enemyColour(4)&&!compass::enemyColour(8));

    using compass::allowChase;using compass::chooseChaseTarget;
    assert(allowChase(true,false,false,false));
    assert(!allowChase(true,true,true,true)); // User waypoint wins.
    assert(!allowChase(true,false,false,true)); // Existing mission route wins.
    assert(!allowChase(false,false,true,true)); // Feature off releases its route.
    assert(allowChase(true,false,true,true)); // Keep following our route.
    const std::vector<compass::ChaseCandidate> enemies{{1,200,12},{2,100,8},{3,30,0},{4,4000,20}};
    assert(chooseChaseTarget({{8,100,0,true}},0)==8);
    assert(chooseChaseTarget(enemies,0)==2);
    assert(chooseChaseTarget(enemies,1)==1); // Do not switch targets at each intersection.
    assert(chooseChaseTarget({{1,200,0},{2,50,10}},1)==1); // Retain a stopped target.
    assert(chooseChaseTarget({{3,30,0},{4,4000,20}},0)==0);
    assert(chooseChaseTarget({{1,std::numeric_limits<float>::quiet_NaN(),20}},0)==0);
    assert(compass::StripLayout::barHeight==.010f);

    RadarTable table;std::vector<std::uint32_t> merged{0x00050002u};
    compass::appendRadarTable(table,merged);
    assert((merged==std::vector<std::uint32_t>{0x00050002u,0x00060003u}));
    assert(compass::readBlipAlpha<AlphaNative>(0x00050002u)==128);
    AlphaNative::next=0;assert(compass::readBlipAlpha<AlphaNative>(0x00050002u)==0);
    AlphaNative::next=999;assert(compass::readBlipAlpha<AlphaNative>(0x00050002u)==255);
    assert(AlphaNative::calls==3);
    compass::FrameGate gate;
    assert(gate.accept(0));assert(!gate.accept(0));
    assert(gate.accept(1));assert(!gate.accept(1));assert(gate.accept(2));
    assert(gate.accept(0xFFFFFFFFu));assert(gate.accept(0));
    assert(compass::StripLayout::lineY<.039f);
    assert(compass::StripLayout::distanceY-compass::StripLayout::lineY<.09f);
    assert(compass::StripLayout::focusLabelY<compass::StripLayout::distanceY);
    const auto healthFull=compass::meterSpan(1,true),healthHalf=compass::meterSpan(.5f,true);
    const auto armourFull=compass::meterSpan(1,false),armourHalf=compass::meterSpan(.5f,false);
    assert(healthHalf.right==healthFull.right && healthHalf.left>healthFull.left);
    assert(armourHalf.left==armourFull.left && armourHalf.right<armourFull.right);
    assert(compass::meterSpan(0,true).left==healthFull.right);
    FakeBlips native;
    const auto handles=compass::collectBlips(native);
    assert(native.firstCalls.size()==128);
    for(unsigned i=0;i<10;++i)assert(native.firstCalls[i]==i);
    assert((handles==std::vector<std::uint32_t>{101,404,808}));

    using namespace compass;
    assert(std::fabs(relativeBearing(0,10,0))<.001f);
    assert(std::fabs(relativeBearing(10,0,0)-90)<.001f);
    assert(std::fabs(relativeBearing(-10,0,90))<.001f);
    assert(wrap(181)==-179 && wrap(-181)==179 && wrap(720)==0);
    assert(healthFraction(200,200)==1 && healthFraction(150,200)==.5f && healthFraction(100,200)==0);
    assert(healthFraction(500,200)==1 && healthFraction(0,100)==0);
    assert(armourFraction(50,100)==.5f && armourFraction(0,0)==0);
    assert(radarDisplay(2)&&radarDisplay(4)&&!radarDisplay(0)&&!radarDisplay(1));
    assert(inRange(300,300)&&!inRange(301,300)&&!inRange(std::numeric_limits<float>::quiet_NaN(),300));
    assert(markerInRange(5000,50,false)); // Distant mission stays visible.
    assert(!markerInRange(51,50,true) && markerInRange(50,50,true));
    assert(!markerInRange(std::numeric_limits<float>::quiet_NaN(),50,false));
    const auto custom=markerColour(0xFF123456u);assert(custom[0]==0x12 && custom[1]==0x34 && custom[2]==0x56);
    assert(markerColour(7)[0]>markerColour(8)[0]);
    std::vector<Marker> markers{{1,29,0,100,0,255,0},{2,8,1,200,0,255,3},{3,1,45,50,0,255,2},{4,2,160,20,0,255,2},{5,3,-175,10,0,255,2}};
    const auto placed=layout(markers);assert(placed.size()==4);assert(placed[0].marker.handle==5);
    assert(focus(placed,0)==1);
    bool right=false,left=false;for(const auto& item:placed){if(item.marker.handle==4)right=item.rear&&item.x==1;if(item.marker.handle==5)left=item.rear&&item.x==-1;}
    assert(right&&left);
    assert(layout({{10,0,1,10,0,255,0},{20,8,0,1000,0,255,3}}).front().marker.handle==10);
    const auto turned=layout({{2,8,4.5f,200,0,255,3}});
    assert(focus(turned,2)==2 && focus(turned,0)==0);
    assert(focus(layout({{2,8,6,200,0,255,3}}),2)==0);
    assert(focus(layout({{2,8,30,200,0,255,3}}),0)==0);
    std::puts("Compass bearings, filtering, overlap priority, center focus and health tests passed");
}
