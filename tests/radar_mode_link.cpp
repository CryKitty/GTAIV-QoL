#include "../src/radar_mode_link.hpp"
#include <cassert>
struct Backend {
    int native=1,journal=-1,writes=0,saves=0;
    bool disk=true,memory=true;
    int read(){return native;}int load(){return journal;}
    bool save(int value){++saves;if(!disk)return false;journal=value;return true;}
    bool write(int value){++writes;if(!memory)return false;native=value;return true;}
};
int main(){
    for(int mode=0;mode<=2;++mode){Backend io;io.native=mode;radarmodelink::State state;
        auto r=state.transition(true,io);assert(r.ok&&io.native==0&&io.journal==mode);
        io.native=2;const auto writes=io.writes,saves=io.saves;
        for(int frame=0;frame<1000;++frame)assert(state.transition(true,io).ok);
        assert(io.native==2&&io.writes==writes&&io.saves==saves); // manual change untouched
        assert(state.transition(false,io).ok&&io.native==mode&&io.journal==-1);
        io.native=2;assert(state.transition(true,io).ok&&io.journal==2&&io.native==0);
        radarmodelink::State nextSession;assert(nextSession.transition(true,io).ok&&io.journal==2);
        assert(nextSession.transition(false,io).ok&&io.native==2&&io.journal==-1);
    }
    Backend io;radarmodelink::State off;assert(off.transition(false,io).ok&&io.writes==0&&io.saves==0);
    io.journal=1;io.native=0;radarmodelink::State recovery;assert(recovery.transition(false,io).ok&&io.native==1&&io.journal==-1);
    io.disk=false;radarmodelink::State fail;assert(!fail.transition(true,io).ok&&io.native==1&&io.writes==1);
    io.disk=true;io.memory=false;assert(!fail.transition(true,io).ok&&io.native==1&&io.journal==1);
    io.memory=true;assert(fail.transition(true,io).ok&&io.native==0&&io.journal==1);
    io.disk=false;assert(!fail.transition(false,io).ok&&io.native==1&&io.journal==1);
    io.disk=true;assert(fail.transition(false,io).ok&&io.journal==-1);
    io.journal=7;radarmodelink::State invalid;const auto writes=io.writes;
    assert(!invalid.transition(true,io).ok&&io.writes==writes);
}
