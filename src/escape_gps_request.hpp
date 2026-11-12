#pragma once
#include "../vendor/roadnav/src/marker_geometry.hpp"
namespace policeescape {
// GTA has one GPS calculation owner. Rendered snapshots are independent, but
// requesting a candidate must first release every route we own for that slot.
// Reposition the same helper where supported; removing a live helper on every
// request can invalidate the next calculation while the old job is retiring.
template<class Handle,class Api> Handle requestGps(Handle helper,Handle primary,roadnav::Vec3 point,Api& api){
    const bool restore=api.exists(primary)&&!api.same(primary,helper);
    if(restore)api.route(primary,false);
    if(api.exists(helper)){
        api.route(helper,false);
        if(api.move(helper,point)){api.configure(helper);api.route(helper,true);return helper;}
        api.remove(helper);
    }
    auto next=api.add(point);
    if(!api.exists(next)){if(restore)api.route(primary,true);return Handle{};}
    api.configure(next);api.route(next,true);return next;
}
template<class Handle,class Api> void restorePrimaryGps(Handle helper,Handle primary,Api& api){
    if(api.exists(primary)&&!api.same(primary,helper)){
        if(api.exists(helper))api.route(helper,false);
        api.route(primary,true);
    }
}
}
