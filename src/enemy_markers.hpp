#pragma once
namespace enemymarkers {
struct State {
    bool exists=false,dead=false,incapacitated=false,hostile=false;
    bool capable()const{return exists&&!dead&&!incapacitated;}
    bool threat()const{return capable()&&hostile;}
};
inline bool sameBinding(unsigned expected,unsigned current){return !current||expected==current;}
// A single blip controls both native 3D chevron and compass presentation.
// Always retire it before dropping ownership, even without a new damage event.
template<class Marks,class Sample,class Retire>
void update(Marks& marks,Sample sample,Retire retire){
    for(auto it=marks.begin();it!=marks.end();){
        if(!sample(*it).threat()){retire(*it);it=marks.erase(it);}else ++it;
    }
}
}
