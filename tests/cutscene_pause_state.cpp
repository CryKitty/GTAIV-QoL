#include "../src/cutscene_pause_state.hpp"
#include <cassert>
using namespace cutscenepause;
int main() {
    Transition t;
    t.Observe(false,false); assert(t.phase==Phase::Idle);
    t.Observe(true,true); assert(t.phase==Phase::Playing && !t.requestPause);
    t.Observe(true,false); assert(!t.requestPause);
    t.Observe(true,true); assert(t.phase==Phase::Paused && t.requestPause);
    t.RejectTransition(); assert(t.phase==Phase::Playing);
    t.Observe(true,true); assert(t.phase==Phase::Paused && t.requestPause);
    t.Observe(true,true); assert(t.phase==Phase::Playing && t.requestResume);
    t.Observe(false,false); assert(t.phase==Phase::Idle && !t.requestResume);
}
