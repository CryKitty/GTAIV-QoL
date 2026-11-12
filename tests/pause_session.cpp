#include "../src/pause_session.hpp"
#include "../src/cutscene_clock.hpp"
#include <cassert>
int main(){
    using namespace cutscenepause;Session s;
    assert(s.Observe(true,false,true)==Action::None);
    assert(s.Observe(true,true,true)==Action::None); // held key cannot arm later
    s.Observe(false,true,true);
    assert(s.Observe(true,true,true)==Action::Pause);
    assert(s.Observe(true,true,true)==Action::None);
    s.Observe(false,true,true);
    assert(s.Observe(true,true,true)==Action::Resume);
    s.Observe(false,true,true);
    assert(s.Observe(true,true,true)==Action::Pause);
    assert(s.Observe(false,false,true)==Action::Resume); // cutscene ended
    assert(s.Observe(true,true,false)==Action::None);
    s.Observe(false,true,true);
    assert(s.Observe(true,true,true)==Action::Pause);
    assert(s.Observe(false,true,false)==Action::Resume); // focus loss or menu
    s.Observe(false,true,true);
    assert(s.Observe(true,true,true)==Action::Pause);
    assert(s.Observe(false,true,true)==Action::None&&s.owned); // no timed recovery
    for(unsigned tick=0;tick<100000;++tick)assert(s.Observe(false,true,true)==Action::None&&s.owned);
    assert(s.Observe(true,true,true)==Action::Resume); // explicit resume after a long hold
    CinematicClocks clocks;
    assert(!clocks.Hold(-1,100));
    assert(clocks.Hold(1000,2000));
    assert(clocks.Stable(1000,2000)&&clocks.Stable(1080,2080));
    assert(!clocks.Stable(1081,2000)&&!clocks.Stable(1000,2081));
    assert(!clocks.Stable(919,2000)&&!clocks.Stable(-1,2000));
    assert(clocks.Hold(0,0)&&clocks.Stable(0,0));
    assert(clocks.Hold(2147483647,2147483647)&&!clocks.Stable(0,0));
    Timeline time;
    assert(!time.Begin(-1)&&time.Filter(-1)==-1);
    assert(time.Filter(1000)==1000&&time.Begin(1000));
    assert(time.Filter(1093)==1000); // captured Deck stutter: +93 ms
    assert(time.Filter(6000)==1000); // bookkeeping keeps moving, timeline holds
    time.End(6000);assert(time.Filter(6000)==1000&&time.Filter(6100)==1100);
    assert(time.Begin(6100)&&!time.Begin(6100));
    assert(time.Filter(8100)==1100);time.End(8100);
    assert(time.Filter(8200)==1200); // repeated pauses accumulate once
    assert(time.Filter(50)==50); // a new section resets compensation
    assert(time.Begin(50));assert(time.Filter(50)==50);time.End(50);
    assert(time.Filter(60)==60); // real stopped clocks get no extra correction
    time.Reset();assert(time.Filter(1000)==1000);
    assert(time.Begin(1000));assert(time.Filter(-1)==1000);time.End(-1);
    assert(time.Filter(-1)==-1&&time.Filter(50)==50);
    return 0;
}
