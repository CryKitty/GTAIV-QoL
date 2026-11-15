#pragma once
namespace radarmodelink {
inline bool valid(int value){return value>=0&&value<=2;}
struct Result {int before=-1,after=-1,saved=-1;bool acted=false,ok=false;const char* action="unchanged";};
// Backend persistence must succeed BEFORE the first native change. A single
// saved-mode record is also the crash-recovery journal; -1 means no ownership.
class State {
    bool known_=false,enabled_=false;
public:
    void reset(){known_=enabled_=false;}
    template<class Backend> Result transition(bool enabled,Backend& io){
        Result r;if(known_&&enabled_==enabled){r.ok=true;return r;}
        known_=false;r.acted=true;r.before=io.read();r.after=r.before;r.saved=io.load();
        if(!valid(r.before)){r.action="native-mode-unavailable";return r;}
        if(r.saved!=-1&&!valid(r.saved)){r.action="invalid-recovery-record";return r;}
        if(enabled){
            if(r.saved==-1){r.saved=r.before;if(!io.save(r.saved)){r.action="save-failed-native-unchanged";return r;}}
            if(!io.write(0)){r.after=io.read();r.action="disable-failed-recovery-retained";return r;}
            r.after=io.read();if(r.after!=0){r.action="disable-readback-failed";return r;}
            r.action="saved-and-disabled";
        }else if(r.saved!=-1){
            if(!io.write(r.saved)){r.after=io.read();r.action="restore-failed-recovery-retained";return r;}
            r.after=io.read();if(r.after!=r.saved){r.action="restore-readback-failed";return r;}
            if(!io.save(-1)){r.action="restored-recovery-clear-failed";return r;}
            r.action="restored-and-cleared";
        }else r.action="off-no-owned-mode";
        known_=true;enabled_=enabled;r.ok=true;return r;
    }
};
}
