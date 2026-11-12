#pragma once
#include "../../../src/navigation_awareness.hpp"
#include "../../minhook/include/MinHook.h"
#include <unordered_map>
#include <mutex>
#include <atomic>
#include "police_search_probe.hpp"
#include "../../../src/date_compass.hpp"

// Observe script declarations and read pool flags; only temporary radar UI is
// changed. Never alter AI relationships, combat tasks or damage history.
class NavigationAwareness {
    using Native=void (__cdecl*)(GameTypes::scrNativeCallContext*);
    inline static NavigationAwareness* active_=nullptr;
    inline static thread_local bool internalRequest_=false;
    inline static std::array<Native,15> originals_{};
    std::array<void*,15> targets_{};
    std::mutex mutex_;
    void (*log_)(const char*)=nullptr;
    std::unordered_map<unsigned,unsigned> combat_;
    std::string brief_;
    std::unordered_map<unsigned,std::string> routeBriefs_;
    unsigned missionRoute_=0,internalRoute_=0;DWORD routeRequestAt_=0;
    datecompass::Session dateSession_{};
    bool taxiRide_=false;float taxiFov_=70;roadnav::Vec3 taxiPlayer_{};
    struct TaxiCamera {float original,applied;};std::unordered_map<unsigned,TaxiCamera> taxiCameras_;
    bool adjustTaxi(GameTypes::scrNativeCallContext* context){
        if(!taxiRide_||!originals_[13])return false;
        std::array<std::uint32_t,3> head{};std::array<unsigned,2> args{};
        if(!read(reinterpret_cast<std::uintptr_t>(context),head)||head[1]!=2||!read(head[2],args))return false;
        float requested=0;std::memcpy(&requested,&args[1],4);
        Scripting::Camera camera{};std::memcpy(&camera,&args[0],4);
        if(!camera.IsValid()||!Scripting::DoesCamExist(camera))return false;
        roadnav::Vec3 point{};Scripting::GetCamPos(camera,&point.x,&point.y,&point.z);
        const float dx=point.x-taxiPlayer_.x,dy=point.y-taxiPlayer_.y,dz=point.z-taxiPlayer_.z;const float distance=std::sqrt(dx*dx+dy*dy+dz*dz);
        const float adjusted=awareness::rearTaxiFov(requested,taxiFov_,taxiRide_,distance);
        if(adjusted==requested)return false;
        if(!taxiCameras_.count(args[0])){
            if(taxiCameras_.size()>=8)return false;
            taxiCameras_[args[0]]={requested,adjusted};
            if(log_)log_("Taxi rear-seat FOV: scoped interior camera widened");
        }else taxiCameras_[args[0]].applied=adjusted;
        NativeContext widened;widened.Push(args[0]);widened.Push(adjusted);originals_[13](&widened);return true;
    }
    void restoreTaxi(){
        taxiRide_=false;
        for(const auto& item:taxiCameras_){Scripting::Camera camera{};const auto handle=item.first;std::memcpy(&camera,&handle,4);
            if(!camera.IsValid()||!Scripting::DoesCamExist(camera))continue;
            float current=0;Scripting::GetCamFov(camera,&current);
            if(std::isfinite(current)&&std::fabs(current-item.second.applied)<.05f)Scripting::SetCamFov(camera,item.second.original);
        }
        taxiCameras_.clear();
    }
    std::uintptr_t poolGlobal_=0;
    bool poolTried_=false,armed_=false;
    policeprobe::Capture policeProbe_;
    DWORD polled_=0;
    struct Mark {unsigned ped;Scripting::Blip blip;bool owned;unsigned colour,display;};
    std::vector<Mark> marks_;
    template<class T> static bool read(std::uintptr_t address,T& value){SIZE_T bytes=0;return address>=0x10000&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),&value,sizeof(value),&bytes)&&bytes==sizeof(value);}
    static std::string stringAt(std::uintptr_t address,unsigned limit=480){
        std::string out;for(unsigned i=0;i<limit;++i){char c=0;if(!read(address+i,c))return {};if(!c)return out;out+=c;}return {};
    }
    static std::string localized(const std::string& key){
        if(key.empty()||key.size()>63)return {};
        const auto ptr=NativeInvoke::Invoke<Scripting::NATIVE_GET_STRING_FROM_TEXT_FILE,const char*>(key.c_str());
        return ptr?stringAt(reinterpret_cast<std::uintptr_t>(ptr)):std::string{};
    }
    void capture(unsigned index,GameTypes::scrNativeCallContext* context){
        std::array<std::uint32_t,3> head{};std::array<unsigned,6> args{};SIZE_T bytes=0;
        if(!read(reinterpret_cast<std::uintptr_t>(context),head)||head[1]<(index==14?4u:index>=13?1u:2u)||head[1]>32||head[2]<0x10000)return;
        const auto count=std::min(head[1],6u);
        if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(head[2]),args.data(),count*4,&bytes)||bytes!=count*4)return;
        if(index==14){
            if(stringAt(args[0],32)!="go_on_date")return;
            datecompass::Preferences prefs;
            std::array<std::uint32_t,datecompass::argumentWords> words{};
            if(args[2]==datecompass::argumentWords&&read(args[1],words))prefs=datecompass::preferences(words);
            std::lock_guard<std::mutex> lock(mutex_);dateSession_.begin(prefs,GetTickCount());
            if(log_)log_(prefs.valid?"Compass date: captured live per-venue preferences":"Compass date: unsupported argument layout; venues stay neutral");
            return;
        }
        if(index==12){
            if(internalRequest_)return;
            // Observe the script's actual route request, never infer an objective
            // from unrelated nearby businesses or mission contacts.
            Scripting::Blip blip{};std::memcpy(&blip,&args[0],4);
            if(!Scripting::DoesBlipExist(blip))return;
            int alpha=0;NativeInvoke::Invoke<Scripting::NATIVE_GET_BLIP_ALPHA,Scripting::ScriptVoid>(blip,&alpha);
            std::lock_guard<std::mutex> lock(mutex_);
            if(args[0]==internalRoute_)return;
            if(log_){char line[160];std::snprintf(line,sizeof(line),"GPS request: blip=%u enabled=%u type=%u sprite=%u alpha=%d",args[0],args[1],static_cast<unsigned>(Scripting::GetBlipInfoIdType(blip)),static_cast<unsigned>(Scripting::GetBlipSprite(blip)),alpha);log_(line);}
            if(args[1]&&alpha>0&&Scripting::GetMissionFlag()){
                missionRoute_=args[0];routeRequestAt_=GetTickCount();if(!brief_.empty())routeBriefs_[missionRoute_]=brief_;
            }else if(!args[1]){routeBriefs_.erase(args[0]);if(missionRoute_==args[0])missionRoute_=0;}
            return;
        }
        if(index>=10){std::lock_guard<std::mutex> lock(mutex_);combat_[args[0]]=args[1];return;}
        const auto key=stringAt(args[0],64);if(key.empty())return;
        std::vector<std::string> inserts;
        if(index==2||index==3)inserts.push_back(localized(stringAt(args[1],64)));
        if(index==4||index==5)inserts.push_back(stringAt(args[1]));
        if(index>=6&&index<=9)inserts.push_back(std::to_string(static_cast<std::int32_t>(args[1])));
        if(index==8||index==9)inserts.push_back(std::to_string(static_cast<std::int32_t>(args[2])));
        const auto text=awareness::briefText(localized(key),inserts);
        if(!text.empty()&&text!="NULL"){
            std::lock_guard<std::mutex> lock(mutex_);
            if(brief_!=text&&log_){const auto line=std::string("Compass mission brief: ")+key+" text="+text.substr(0,160);log_(line.c_str());}
            brief_=text;if(missionRoute_)routeBriefs_[missionRoute_]=text;
        }
    }
    template<unsigned Index> static void __cdecl observe(GameTypes::scrNativeCallContext* context){
        if constexpr(Index==13){if(active_&&active_->adjustTaxi(context))return;}
        else if(active_)active_->capture(Index,context);
        if(originals_[Index])originals_[Index](context);
    }
    void discoverPool(){
        if(poolTried_)return;poolTried_=true;
        const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleA(nullptr));IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS nt{};
        if(!read(base,dos)||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<=0||dos.e_lfanew>0x1000||!read(base+dos.e_lfanew,nt)||nt.Signature!=IMAGE_NT_SIGNATURE||nt.FileHeader.NumberOfSections>32)return;
        const auto sections=base+dos.e_lfanew+offsetof(IMAGE_NT_HEADERS,OptionalHeader)+nt.FileHeader.SizeOfOptionalHeader;
        unsigned matches=0;std::uintptr_t found=0;
        // Plugin-SDK CPools::ms_pPedPool signature, relocated from executable code.
        for(unsigned i=0;i<nt.FileHeader.NumberOfSections;++i){
            IMAGE_SECTION_HEADER section{};if(!read(sections+i*sizeof(section),section)||!(section.Characteristics&IMAGE_SCN_MEM_EXECUTE)||section.Misc.VirtualSize>32*1024*1024||section.VirtualAddress>=nt.OptionalHeader.SizeOfImage||section.Misc.VirtualSize>nt.OptionalHeader.SizeOfImage-section.VirtualAddress)continue;
            std::vector<unsigned char> code(section.Misc.VirtualSize);SIZE_T bytes=0;
            if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(base+section.VirtualAddress),code.data(),code.size(),&bytes))continue;
            for(std::size_t j=0;j+16<=bytes;++j){const auto* c=code.data()+j;
                if(c[0]==0xa1&&c[5]==0xf3&&c[6]==0x0f&&c[7]==0x11&&c[8]==0x7c&&c[9]==0x24&&c[11]==0xf3&&c[12]==0x0f&&c[13]==0x11&&c[14]==0x74&&c[15]==0x24){std::uint32_t pointer=0;std::memcpy(&pointer,c+1,4);found=pointer;++matches;}
            }
        }
        if(matches==1&&found>=base&&found+4<=base+nt.OptionalHeader.SizeOfImage)poolGlobal_=found;
        if(log_){char line[120];std::snprintf(line,sizeof(line),"Compass attacker pool: signature matches=%u available=%u",matches,poolGlobal_?1u:0u);log_(line);}
    }
    static Scripting::Ped pedHandle(unsigned handle){Scripting::Ped ped{};std::memcpy(&ped,&handle,sizeof(handle));return ped;}
    bool samePed(Mark& mark){using namespace Scripting;return DoesBlipExist(mark.blip)&&GetBlipInfoIdType(mark.blip)==BLIP_TYPE_CHAR&&GetBlipInfoIdPedIndex(mark.blip).Get()==mark.ped;}
    void restore(Mark& mark){
        using namespace Scripting;if(!samePed(mark))return;
        if(mark.owned){RemoveBlip(mark.blip);return;}
        ColourIndex colour=0;GetBlipColour(mark.blip,&colour);if(colour==9)ChangeBlipColour(mark.blip,mark.colour);
        const auto display=NativeInvoke::Invoke<NATIVE_GET_BLIP_INFO_ID_DISPLAY,u32>(mark.blip);
        if(display==4)ChangeBlipDisplay(mark.blip,static_cast<eBlipDisplay>(mark.display));
    }
public:
    // OnStart can run during ASI loading. Leave game code untouched until
    // CompassHud runs on the initialized ScriptHook game loop.
    void start(void (*log)(const char*)){log_=log;}
    void activate(){
        if(armed_)return;armed_=true;
        active_=this;const auto init=MH_Initialize();if(init!=MH_OK&&init!=MH_ERROR_ALREADY_INITIALIZED)return;
        using Lookup=void* (__cdecl*)(unsigned);using Names=void* (__cdecl*)(const char*);
        const auto module=GetModuleHandleA("ScriptHook.dll");
        const auto lookup=module?reinterpret_cast<Lookup>(GetProcAddress(module,"?GetNativeAddressFromHash@Game@@SAPAXI@Z")):nullptr;
        const auto names=module?reinterpret_cast<Names>(GetProcAddress(module,"?GetNativeAddress@Game@@SAPAXPBD@Z")):nullptr;
        using namespace Scripting;
        // Read-only evidence for locating the CE search circle. Do not invoke
        // SET_FAKE_WANTED_CIRCLE: it changes the police state rather than reading it.
        if(log_){
            const char* probes[]={"SET_FAKE_WANTED_CIRCLE","WANTED_STARS_ARE_FLASHING","REPORT_POLICE_SPOTTING_SUSPECT"};
            const unsigned probeHashes[]={NATIVE_SET_FAKE_WANTED_CIRCLE,NATIVE_WANTED_STARS_ARE_FLASHING,0x07d97f81u};
            const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleA(nullptr));
            for(unsigned probe=0;probe<3;++probe){
                auto address=lookup&&probeHashes[probe]?lookup(probeHashes[probe]):nullptr;if(!address&&names)address=names(probes[probe]);
                std::array<unsigned char,384> code{};if(!address||!read(reinterpret_cast<std::uintptr_t>(address),code))continue;
                for(unsigned offset=0;offset<code.size();offset+=48){
                    char line[240];int written=std::snprintf(line,sizeof(line),"Police search probe: %s rva=%08x +%03x ",probes[probe],static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(address)-base),offset);
                    for(unsigned i=offset;i<offset+48;++i)written+=std::snprintf(line+written,sizeof(line)-written,"%02x",static_cast<unsigned>(code[i]));
                    log_(line);
                }
            }
        }
        policeProbe_.start(lookup,names);
        const NativeHashes hashes[]={NATIVE_PRINT,NATIVE_PRINT_NOW,NATIVE_PRINT_STRING_IN_STRING,NATIVE_PRINT_STRING_IN_STRING_NOW,NATIVE_PRINT_STRING_WITH_LITERAL_STRING,NATIVE_PRINT_STRING_WITH_LITERAL_STRING_NOW,NATIVE_PRINT_WITH_NUMBER,NATIVE_PRINT_WITH_NUMBER_NOW,NATIVE_PRINT_WITH_2_NUMBERS,NATIVE_PRINT_WITH_2_NUMBERS_NOW,NATIVE_TASK_COMBAT,NATIVE_TASK_COMBAT_TIMED,NATIVE_SET_ROUTE,NATIVE_SET_CAM_FOV,NATIVE_START_NEW_SCRIPT_WITH_ARGS};
        const char* labels[]={"PRINT","PRINT_NOW","PRINT_STRING_IN_STRING","PRINT_STRING_IN_STRING_NOW","PRINT_STRING_WITH_LITERAL_STRING","PRINT_STRING_WITH_LITERAL_STRING_NOW","PRINT_WITH_NUMBER","PRINT_WITH_NUMBER_NOW","PRINT_WITH_2_NUMBERS","PRINT_WITH_2_NUMBERS_NOW","TASK_COMBAT","TASK_COMBAT_TIMED","SET_ROUTE","SET_CAM_FOV","START_NEW_SCRIPT_WITH_ARGS"};
        const Native callbacks[]={observe<0>,observe<1>,observe<2>,observe<3>,observe<4>,observe<5>,observe<6>,observe<7>,observe<8>,observe<9>,observe<10>,observe<11>,observe<12>,observe<13>,observe<14>};
        for(unsigned i=0;i<targets_.size();++i){if(targets_[i])continue;auto address=lookup?lookup(hashes[i]):nullptr;if(!address&&names)address=names(labels[i]);
            if(!address||MH_CreateHook(address,reinterpret_cast<void*>(callbacks[i]),reinterpret_cast<void**>(&originals_[i]))!=MH_OK)continue;
            if(MH_EnableHook(address)!=MH_OK){MH_RemoveHook(address);originals_[i]=nullptr;continue;}targets_[i]=address;
        }
        if(log_){unsigned count=0;for(auto target:targets_)if(target)++count;char line[100];std::snprintf(line,sizeof(line),"Compass mission/combat native observers: %u/15",count);log_(line);}
    }
    bool policeCircle(unsigned player,unsigned stars,roadnav::Vec3& center,float& radius){return policeProbe_.circle(player,stars,center.x,center.y,center.z,radius);}
    void policeSample(unsigned playerIndex,unsigned stars,roadnav::Vec3 player){policeProbe_.sample(playerIndex,stars,player.x,player.y,player.z);}
    void configureTaxi(float fov){taxiFov_=fov==0?0:(fov>=40&&fov<=120?fov:70);}
    void taxiTick(){
        using namespace Scripting;
        const bool cutscene=NativeInvoke::Invoke<NATIVE_HAS_CUTSCENE_LOADED,b8>()&&!NativeInvoke::Invoke<NATIVE_HAS_CUTSCENE_FINISHED,b8>();
        Ped player{};GetPlayerChar(ConvertIntToPlayerIndex(GetPlayerId()),&player);
        bool rear=false;
        if(taxiFov_>0&&!cutscene&&!IsPauseMenuActive()&&player.IsValid()&&DoesCharExist(player)&&IsCharInAnyCar(player)&&
           (IsCharInTaxi(player)||NativeInvoke::Invoke<NATIVE_IS_PLAYER_IN_SHORTCUT_TAXI,u32>())){
            Vehicle car{};GetCarCharIsUsing(player,&car);
            if(car.IsValid()&&DoesVehicleExist(car))for(unsigned seat:{1u,2u}){
                if(NativeInvoke::Invoke<NATIVE_IS_CAR_PASSENGER_SEAT_FREE,b8>(car,seat))continue;
                Ped passenger{};GetCharInCarPassengerSeat(car,seat,&passenger);if(passenger.Get()==player.Get())rear=true;
            }
            if(rear)GetCharCoordinates(player,&taxiPlayer_.x,&taxiPlayer_.y,&taxiPlayer_.z);
        }
        if(!rear){if(taxiRide_||!taxiCameras_.empty())restoreTaxi();return;}
        taxiRide_=true;
        for(auto it=taxiCameras_.begin();it!=taxiCameras_.end();){
            Camera camera{};const auto handle=it->first;std::memcpy(&camera,&handle,4);
            if(!DoesCamExist(camera)){it=taxiCameras_.erase(it);continue;}
            float current=0;GetCamFov(camera,&current);roadnav::Vec3 point{};GetCamPos(camera,&point.x,&point.y,&point.z);
            const float distance=std::sqrt((point.x-taxiPlayer_.x)*(point.x-taxiPlayer_.x)+(point.y-taxiPlayer_.y)*(point.y-taxiPlayer_.y)+(point.z-taxiPlayer_.z)*(point.z-taxiPlayer_.z));
            const float adjusted=awareness::rearTaxiFov(it->second.original,taxiFov_,true,distance);
            if(originals_[13]&&std::fabs(current-it->second.applied)<.05f&&std::fabs(adjusted-current)>.05f){NativeContext changed;changed.Push(handle);changed.Push(adjusted);originals_[13](&changed);it->second.applied=adjusted;}
            ++it;
        }
        Camera active{};GetRootCam(&active);if(!active.IsValid())GetGameCamChild(&active);
        if(active.IsValid()&&DoesCamExist(active)&&!taxiCameras_.count(active.Get())){float fov=0;GetCamFov(active,&fov);if(fov>=35&&fov<=55)SetCamFov(active,fov);}
    }
    void clearMarks(){for(auto& mark:marks_)restore(mark);marks_.clear();polled_=0;}
    void stop(){restoreTaxi();clearMarks();for(auto& target:targets_)if(target){MH_DisableHook(target);MH_RemoveHook(target);target=nullptr;}active_=nullptr;originals_={};std::lock_guard<std::mutex> lock(mutex_);combat_.clear();brief_.clear();routeBriefs_.clear();missionRoute_=0;dateSession_={};poolGlobal_=0;poolTried_=false;armed_=false;}
    std::string brief(bool mission){std::lock_guard<std::mutex> lock(mutex_);if(!mission)brief_.clear();return brief_;}
    std::string briefFor(unsigned handle){std::lock_guard<std::mutex> lock(mutex_);const auto found=routeBriefs_.find(handle);return found==routeBriefs_.end()?std::string{}:found->second;}
    void requestInternalRoute(Scripting::Blip blip,bool enabled){
        const bool previous=internalRequest_;internalRequest_=true;
        Scripting::SetRoute(blip,enabled);internalRequest_=previous;
    }
    void internalRoute(unsigned handle){std::lock_guard<std::mutex> lock(mutex_);internalRoute_=handle;}
    unsigned routeRequestAge(unsigned target){std::lock_guard<std::mutex> lock(mutex_);return missionRoute_==target&&routeRequestAt_?GetTickCount()-routeRequestAt_:0xffffffffu;}
    datecompass::Preferences datePreferences(bool active,DWORD now){
        std::lock_guard<std::mutex> lock(mutex_);
        return dateSession_.current(active,now);
    }
    unsigned missionRoute(bool mission){std::lock_guard<std::mutex> lock(mutex_);if(!mission)missionRoute_=0;return missionRoute_;}
    bool attacking(unsigned ped)const{for(const auto& mark:marks_)if(mark.ped==ped)return true;return false;}
    bool due(DWORD now)const{return !polled_||now-polled_>=250;}
    void tick(Scripting::Ped player,roadnav::Vec3 position,const std::vector<Scripting::Blip>& blips,bool fullSnapshot){
        using namespace Scripting;const DWORD now=GetTickCount();if(polled_&&now-polled_<250)return;polled_=now;
        {std::lock_guard<std::mutex> lock(mutex_);for(auto it=routeBriefs_.begin();it!=routeBriefs_.end();){Blip blip{};const unsigned handle=it->first;std::memcpy(&blip,&handle,4);if(!DoesBlipExist(blip))it=routeBriefs_.erase(it);else ++it;}}
        discoverPool();std::vector<unsigned> handles;
        std::uint32_t address=0;awareness::PedPool pool{};unsigned char playerFlag=0;
        if(poolGlobal_&&read(poolGlobal_,address)&&read(address,pool)&&(player.Get()>>8)<pool.size&&read(pool.flags+(player.Get()>>8),playerFlag)&&awareness::validPool(pool,player.Get(),playerFlag)){
            std::vector<unsigned char> flags(pool.size);SIZE_T bytes=0;
            if(ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(pool.flags),flags.data(),flags.size(),&bytes)&&bytes==flags.size())
                for(unsigned i=0;i<pool.size;++i){const auto handle=awareness::poolHandle(i,flags[i]);if(handle)handles.push_back(handle);}
        }
        // Existing entity markers remain useful on unsupported pool layouts.
        for(auto blip:blips)if(DoesBlipExist(blip)&&GetBlipInfoIdType(blip)==BLIP_TYPE_CHAR)handles.push_back(GetBlipInfoIdPedIndex(blip).Get());
        for(auto& mark:marks_)handles.push_back(mark.ped);
        {std::lock_guard<std::mutex> lock(mutex_);for(const auto& entry:combat_)handles.push_back(entry.first);}
        std::sort(handles.begin(),handles.end());handles.erase(std::unique(handles.begin(),handles.end()),handles.end());
        for(auto it=marks_.begin();it!=marks_.end();){auto ped=pedHandle(it->ped);roadnav::Vec3 point{};
            const bool live=DoesCharExist(ped)&&!IsCharDead(ped);if(live)GetCharCoordinates(ped,&point.x,&point.y,&point.z);
            if(!live||roadnav::distance2D(point,position)>220||!samePed(*it)){restore(*it);it=marks_.erase(it);}else ++it;
        }
        Vehicle vehicle{};GetCarCharIsUsing(player,&vehicle);
        for(auto handle:handles){if(handle==player.Get()||marks_.size()>=32||attacking(handle))continue;auto ped=pedHandle(handle);
            if(!DoesCharExist(ped)||IsCharDead(ped))continue;roadnav::Vec3 point{};GetCharCoordinates(ped,&point.x,&point.y,&point.z);const float distance=roadnav::distance2D(point,position);if(distance>180)continue;
            bool ordered=false;{std::lock_guard<std::mutex> lock(mutex_);const auto found=combat_.find(handle);ordered=found!=combat_.end()&&found->second==player.Get();}
            const bool damage=HasCharBeenDamagedByChar(player,ped,false)||(vehicle.IsValid()&&DoesVehicleExist(vehicle)&&HasCarBeenDamagedByChar(vehicle,ped));
            const bool aggressive=IsPedInCombat(ped)||IsCharShooting(ped)||IsCharInMeleeCombat(ped);
            if(!awareness::attacker(damage,ordered,false,distance,aggressive))continue;
            Blip marker{};for(auto blip:blips)if(DoesBlipExist(blip)&&GetBlipInfoIdType(blip)==BLIP_TYPE_CHAR&&GetBlipInfoIdPedIndex(blip).Get()==handle){marker=blip;break;}
            const bool owned=!marker.IsValid();if(owned&&!fullSnapshot)continue;
            if(owned)AddBlipForChar(ped,&marker);
            if(!marker.IsValid()||!DoesBlipExist(marker))continue;
            // A native may reuse an existing marker; only claim ownership if it
            // was absent from the full live snapshot supplied by CompassHud.
            ColourIndex colour=0;GetBlipColour(marker,&colour);const auto display=NativeInvoke::Invoke<NATIVE_GET_BLIP_INFO_ID_DISPLAY,u32>(marker);
            marks_.push_back({handle,marker,owned,colour,display});
            ChangeBlipColour(marker,9);ChangeBlipDisplay(marker,static_cast<eBlipDisplay>(4));if(owned)SetBlipAsShortRange(marker,true);
            if(owned)ChangeBlipNameFromAscii(marker,"Attacker");
            if(log_){char line[140];std::snprintf(line,sizeof(line),"Compass attacker marked: ped=%u blip=%u owned=%u damage=%u combat-order=%u",handle,marker.Get(),owned?1u:0u,damage?1u:0u,ordered?1u:0u);log_(line);}
        }
        {std::lock_guard<std::mutex> lock(mutex_);for(auto it=combat_.begin();it!=combat_.end();)if(!DoesCharExist(pedHandle(it->first)))it=combat_.erase(it);else ++it;}
    }
};
