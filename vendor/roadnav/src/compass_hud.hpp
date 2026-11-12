#pragma once
#include "../../../src/compass.hpp"
#include <algorithm>
#include <atomic>
#include <unordered_set>
#include <mutex>
#include "navigation_awareness.hpp"
#include "../../minhook/include/MinHook.h"

#include "minimap_layout.hpp"
#include "phone_notifications.hpp"
#include "compass_notice_renderer.hpp"
#include "compass_controller.hpp"
#include "../../../src/compass_waypoint.hpp"

// Change only the minimap layout position. Keep all native preferences and
// visibility answers intact, including those used by wanted stars and messages.
class CompassHud {
    using RadarNative=void (__cdecl*)(GameTypes::scrNativeCallContext*);
    MinimapLayout minimap_;
    PhoneNotifications messages_;
    compass::NoticeRenderer* noticeRenderer_=nullptr;
    compass::WaypointHold waypointHold_{};
    CompassController controller_{};bool waypointDownLast_=false;
    compass::WaypointVehicleGate vehicleGate_{};bool inputShooting_=false;DWORD inputReportAt_=0;
    Scripting::Blip compassWaypoint_{};
    DWORD socialAt_=0;bool social_=false;
    unsigned compassWaypointSource_=0;roadnav::Vec3 compassWaypointPoint_{};
    DWORD waypointFeedbackAt_=0;
    const char* waypointFeedback_="";
    unsigned routeHelper_=0;
    inline static compass::Hostility hostility_{};
    inline static std::mutex hostilityMutex_;
    inline static RadarNative originalEnemy_=nullptr,originalFriendly_=nullptr;
    void* enemyTarget_=nullptr;void* friendlyTarget_=nullptr;
    // scrNativeCallContext's x86 ABI is defined in ScriptHook GameTypes.h.
    // Read only its two scalar arguments; always forward the original context.
    static bool declaration(GameTypes::scrNativeCallContext* context,std::array<unsigned,2>& args){
        std::array<std::uint32_t,3> header{};
        return read(reinterpret_cast<std::uintptr_t>(context),header)&&header[1]>=2&&header[1]<=32&&header[2]>=0x10000&&read(header[2],args)&&args[0]&&args[1]<=1;
    }
    static void __cdecl declareEnemy(GameTypes::scrNativeCallContext* context){
        std::array<unsigned,2> args{};const bool valid=declaration(context,args);
        if(originalEnemy_)originalEnemy_(context);
        if(valid){std::lock_guard<std::mutex> lock(hostilityMutex_);hostility_.peds[args[0]]=args[1]!=0;}
    }
    static void __cdecl declareFriendly(GameTypes::scrNativeCallContext* context){
        std::array<unsigned,2> args{};const bool valid=declaration(context,args);
        if(originalFriendly_)originalFriendly_(context);
        if(valid){std::lock_guard<std::mutex> lock(hostilityMutex_);hostility_.blips[args[0]]=args[1]!=0;}
    }
    bool policeMarker(Scripting::Blip blip){
        using namespace Scripting;
        if(awareness::policeSprite(static_cast<unsigned>(GetBlipSprite(blip))))return true;
        if(GetBlipInfoIdType(blip)!=BLIP_TYPE_CAR)return false;
        auto car=GetBlipInfoIdCarIndex(blip);if(!car.IsValid()||!DoesVehicleExist(car))return false;
        eModel model{};GetCarModel(car,&model);
        return model==MODEL_POLICE||model==MODEL_POLICE2||model==MODEL_NOOSE||model==MODEL_POLPATRIOT||model==MODEL_FBI||model==MODEL_POLMAV;
    }
    bool friendMarker(Scripting::Blip blip){
        using namespace Scripting;const auto type=GetBlipInfoIdType(blip);Ped ped{};
        if(type==BLIP_TYPE_CHAR)ped=GetBlipInfoIdPedIndex(blip);
        else if(type==BLIP_TYPE_CAR){auto car=GetBlipInfoIdCarIndex(blip);if(car.IsValid()&&DoesVehicleExist(car))GetDriverOfCar(car,&ped);}
        std::lock_guard<std::mutex> lock(hostilityMutex_);return hostility_.friendly(blip.Get(),ped.Get());
    }
    bool enemyMarker(Scripting::Blip blip,unsigned colour,unsigned sprite){
        using namespace Scripting;
        const auto type=GetBlipInfoIdType(blip);Ped ped{};
        if(!compass::hostileMarkerType(static_cast<unsigned>(type))){enemies_.erase(blip.Get());return false;}
        if(type==BLIP_TYPE_CHAR)ped=GetBlipInfoIdPedIndex(blip);
        else if(type==BLIP_TYPE_CAR){auto car=GetBlipInfoIdCarIndex(blip);if(car.IsValid()&&DoesVehicleExist(car))GetDriverOfCar(car,&ped);}
        const bool red=compass::enemyColour(colour)||sprite==BLIP_DWAYNE_RED||sprite==BLIP_PLAYBOY_X_RED||enemies_.count(blip.Get())!=0;
        bool enemy=false;{std::lock_guard<std::mutex> lock(hostilityMutex_);enemy=!hostility_.friendly(blip.Get(),ped.Get())&&(awareness_.attacking(ped.Get())||((type==BLIP_TYPE_CHAR||type==BLIP_TYPE_CAR)?hostility_.enemy(blip.Get(),ped.Get(),red):red));}
        if(enemy)enemies_.insert(blip.Get());else enemies_.erase(blip.Get());
        return enemy;
    }

    std::vector<Scripting::Blip> blips_;
    std::array<std::string,128> names_{};
    std::array<Scripting::Texture,128> textures_{};
    std::array<bool,128> tried_{};
    Scripting::TextureDict dictionary_{};
    DWORD scanAt_=0,logAt_=0;
    compass::FrameGate frameGate_{};
    unsigned focused_=0;
    std::unordered_set<unsigned> enemies_;
    NavigationAwareness awareness_;
    compass::DamageFlash healthDamage_{},armourDamage_{};
    bool compassDefault_=true;
    bool hidden_=false,initialized_=false,scanLogged_=false,startupComplete_=false;
    std::uintptr_t blipTable_=0;
    float fallbackRange_=300;
    bool metric_=false;
    void (*log_)(const char*)=nullptr;
    template<class T> static bool read(std::uintptr_t address,T& value){
        SIZE_T bytes=0;return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),&value,sizeof(value),&bytes)&&bytes==sizeof(value);
    }
    static std::string nameAt(std::uintptr_t address){
        char text[64]{};SIZE_T bytes=0;
        if(!address||!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),text,sizeof(text),&bytes)||bytes!=sizeof(text))return {};
        if(!std::memchr(text,0,sizeof(text))||std::strncmp(text,"radar_",6)!=0)return {};
        for(unsigned i=0;text[i];++i)if(!((text[i]>='a'&&text[i]<='z')||(text[i]>='0'&&text[i]<='9')||text[i]=='_'))return {};
        return text;
    }
    void discoverNames(){
        // IV-SDK documents this table as RadarBlipSpriteFilenames[130], with
        // north=7 and waypoint=8. Locate it by validated contents, never by a
        // version-specific address. Read-only section scans happen once.
        const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleA(nullptr));
        IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS nt{};
        if(!read(base,dos)||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<=0||dos.e_lfanew>0x1000||
           !read(base+dos.e_lfanew,nt)||nt.Signature!=IMAGE_NT_SIGNATURE||nt.FileHeader.NumberOfSections>32)return;
        const auto sections=base+dos.e_lfanew+offsetof(IMAGE_NT_HEADERS,OptionalHeader)+nt.FileHeader.SizeOfOptionalHeader;
        for(unsigned section=0;section<nt.FileHeader.NumberOfSections;++section){
            IMAGE_SECTION_HEADER h{};if(!read(sections+section*sizeof(h),h))continue;
            if(!(h.Characteristics&IMAGE_SCN_MEM_READ)||(h.Characteristics&IMAGE_SCN_MEM_EXECUTE)||h.Misc.VirtualSize>32*1024*1024)continue;
            if(h.VirtualAddress>=nt.OptionalHeader.SizeOfImage||h.Misc.VirtualSize>nt.OptionalHeader.SizeOfImage-h.VirtualAddress)continue;
            std::vector<unsigned char> data(h.Misc.VirtualSize);SIZE_T bytes=0;
            if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(base+h.VirtualAddress),data.data(),data.size(),&bytes))continue;
            for(std::size_t offset=28;offset+8<=bytes;offset+=4){
                std::uint32_t north=0,waypoint=0;std::memcpy(&north,data.data()+offset,4);
                // Check candidate pointer is within this executable before reading.
                if(north<base||north>=base+nt.OptionalHeader.SizeOfImage)continue;
                if(nameAt(north)!="radar_north")continue;
                std::memcpy(&waypoint,data.data()+offset+4,4);
                if(nameAt(waypoint)!="radar_waypoint")continue;
                const auto table=base+h.VirtualAddress+offset-28;
                unsigned valid=0;std::array<std::string,128> names{};
                for(unsigned i=0;i<128;++i){std::uint32_t pointer=0;if(read(table+i*4,pointer))names[i]=nameAt(pointer);if(!names[i].empty())++valid;}
                if(valid>=20){names_=std::move(names);if(log_)log_("Compass: validated live radar icon-name table");return;}
            }
        }
        if(log_)log_("Compass: radar icon table unavailable; using fallback marker symbols");
    }
    void initialize(){
        char path[MAX_PATH]{};GetModuleFileNameA(nullptr,path,MAX_PATH);char* slash=std::strrchr(path,'\\');
        if(slash){strcpy_s(slash+1,MAX_PATH-static_cast<std::size_t>(slash+1-path),"GTAIV-QoL.ini");
            const int range=GetPrivateProfileIntA("Compass","FallbackRadiusMeters",300,path);
            fallbackRange_=static_cast<float>(std::clamp(range,50,1000));
            metric_=GetPrivateProfileIntA("Compass","MetricDistances",0,path)!=0;}
        discoverNames();dictionary_=Scripting::GetTxd("blips");
        if(log_)log_(dictionary_.IsValid()?"Compass: borrowed game blips texture dictionary found":"Compass: blips dictionary unavailable; fallback symbols active");
        initialized_=true;
    }
    static bool position(Scripting::Blip blip,roadnav::Vec3& point){
        using namespace Scripting;
        switch(GetBlipInfoIdType(blip)){
        case BLIP_TYPE_CAR:{auto car=GetBlipInfoIdCarIndex(blip);if(!car.IsValid()||!DoesVehicleExist(car))return false;GetCarCoordinates(car,&point.x,&point.y,&point.z);break;}
        case BLIP_TYPE_CHAR:{auto ped=GetBlipInfoIdPedIndex(blip);if(!ped.IsValid()||!DoesCharExist(ped))return false;GetCharCoordinates(ped,&point.x,&point.y,&point.z);break;}
        case BLIP_TYPE_OBJECT:{auto object=GetBlipInfoIdObjectIndex(blip);if(!object.IsValid()||!DoesObjectExist(object))return false;GetObjectCoordinates(object,&point.x,&point.y,&point.z);break;}
        case BLIP_TYPE_PICKUP:{auto pickup=GetBlipInfoIdPickupIndex(blip);if(!pickup.IsValid()||!DoesPickupExist(pickup))return false;GetPickupCoordinates(pickup,&point.x,&point.y,&point.z);break;}
        case BLIP_TYPE_COORD:case BLIP_TYPE_CONTACT:{Vector3 p{};GetBlipCoords(blip,&p);point={p.X,p.Y,p.Z};break;}
        default:return false;
        }
        return std::isfinite(point.x)&&std::isfinite(point.y)&&std::isfinite(point.z);
    }
    static Scripting::Blip asBlip(std::uint32_t handle){
        Scripting::Blip blip{};static_assert(sizeof(blip)==sizeof(handle),"ScriptHook blip ABI");
        std::memcpy(&blip,&handle,sizeof(handle));return blip;
    }
    struct NativeCalls {
        template<std::uint32_t Hash,class R,class... Args> static R invoke(Args... args){return NativeInvoke::Invoke<Hash,R>(args...);}
    };
    static_assert(compass::blipAlphaHash==Scripting::NATIVE_GET_BLIP_ALPHA,"Blip alpha native hash");
    struct BlipBackend {
        CompassHud* hud;
        std::uint32_t first(unsigned category){
            if(!hud->scanLogged_&&hud->log_){char line[80];std::snprintf(line,sizeof(line),"Compass: enumerate blip category %u",category);hud->log_(line);}
            return NativeInvoke::Invoke<Scripting::NATIVE_GET_FIRST_BLIP_INFO_ID,u32>(category);
        }
        std::uint32_t next(unsigned category){return NativeInvoke::Invoke<Scripting::NATIVE_GET_NEXT_BLIP_INFO_ID,u32>(category);}
        bool exists(std::uint32_t handle){return Scripting::DoesBlipExist(asBlip(handle));}
    };
    void discoverBlipTable(const std::vector<std::uint32_t>& known){
        // SHDN enumerates the complete radar pointer table, including engine POIs,
        // using the script handle at record +4. Locate it from live native handles
        // instead of assuming an executable version or a fixed global address.
        if(known.size()<2)return;
        const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleA(nullptr));
        IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS nt{};
        if(!read(base,dos)||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<=0||dos.e_lfanew>0x1000||
           !read(base+dos.e_lfanew,nt)||nt.Signature!=IMAGE_NT_SIGNATURE||nt.FileHeader.NumberOfSections>32)return;
        const auto sections=base+dos.e_lfanew+offsetof(IMAGE_NT_HEADERS,OptionalHeader)+nt.FileHeader.SizeOfOptionalHeader;
        const auto anchor=known.front();const unsigned slot=anchor&0xffffu;if(slot>=1500)return;
        for(unsigned section=0;section<nt.FileHeader.NumberOfSections;++section){
            IMAGE_SECTION_HEADER h{};if(!read(sections+section*sizeof(h),h))continue;
            if(!(h.Characteristics&IMAGE_SCN_MEM_WRITE)||(h.Characteristics&IMAGE_SCN_MEM_EXECUTE)||h.Misc.VirtualSize>32*1024*1024)continue;
            if(h.VirtualAddress>=nt.OptionalHeader.SizeOfImage||h.Misc.VirtualSize>nt.OptionalHeader.SizeOfImage-h.VirtualAddress)continue;
            std::vector<unsigned char> data(h.Misc.VirtualSize);SIZE_T bytes=0;
            if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(base+h.VirtualAddress),data.data(),data.size(),&bytes))continue;
            for(std::size_t offset=slot*4;offset+4<=bytes;offset+=4){
                std::uint32_t pointer=0,handle=0;std::memcpy(&pointer,data.data()+offset,4);
                if(pointer<0x10000||!read(static_cast<std::uintptr_t>(pointer)+4,handle)||handle!=anchor)continue;
                const auto candidate=base+h.VirtualAddress+offset-slot*4;
                if(candidate<base||candidate+1500*4>base+nt.OptionalHeader.SizeOfImage)continue;
                bool valid=true;
                for(auto expected:known){
                    const unsigned index=expected&0xffffu;
                    if(index>=1500||!read(candidate+index*4,pointer)||pointer<0x10000||
                       !read(static_cast<std::uintptr_t>(pointer)+4,handle)||handle!=expected){valid=false;break;}
                }
                if(valid){blipTable_=candidate;if(log_)log_("Compass: complete radar table validated against native handles");return;}
            }
        }
        if(log_)log_("Compass: complete radar table unavailable; native iterator fallback active");
    }
    void scan(){
        BlipBackend backend{this};auto handles=compass::collectBlips(backend);
        if(!scanLogged_)discoverBlipTable(handles);
        if(blipTable_){
            struct TableBackend {
                std::array<std::uint32_t,1500> pointers{};
                std::uint32_t handleAt(unsigned slot){
                    std::uint32_t handle=0;
                    if(pointers[slot]>=0x10000)CompassHud::read(static_cast<std::uintptr_t>(pointers[slot])+4,handle);
                    return handle;
                }
                bool exists(std::uint32_t handle){return Scripting::DoesBlipExist(CompassHud::asBlip(handle));}
            } table;
            if(read(blipTable_,table.pointers))compass::appendRadarTable(table,handles);
        }
        blips_.clear();for(auto handle:handles)blips_.push_back(asBlip(handle));
        if(!scanLogged_&&log_){char line[100];std::snprintf(line,sizeof(line),"Compass: category enumeration complete; blips=%u",static_cast<unsigned>(blips_.size()));log_(line);}
        {std::lock_guard<std::mutex> lock(hostilityMutex_);
            for(auto it=hostility_.peds.begin();it!=hostility_.peds.end();){Scripting::Ped ped{};const auto handle=it->first;std::memcpy(&ped,&handle,sizeof(handle));if(!Scripting::DoesCharExist(ped))it=hostility_.peds.erase(it);else ++it;}
            for(auto it=hostility_.blips.begin();it!=hostility_.blips.end();)if(!Scripting::DoesBlipExist(asBlip(it->first)))it=hostility_.blips.erase(it);else ++it;
        }
        for(auto it=enemies_.begin();it!=enemies_.end();)if(!Scripting::DoesBlipExist(asBlip(*it)))it=enemies_.erase(it);else ++it;
        scanLogged_=true;
    }
    static void rect(float x,float y,float w,float h,unsigned r,unsigned g,unsigned b,unsigned a){
        Scripting::DrawRect(x,y,w,h,static_cast<u8>(r),static_cast<u8>(g),static_cast<u8>(b),static_cast<u8>(a));
    }
    static void textStyle(float size,unsigned alpha,float widthScale=1.0f,float heightScale=1.0f){
        using namespace Scripting;
        SetTextFont(static_cast<eTextFont>(0));SetTextScale(size*widthScale,size*1.22f*heightScale);SetTextCentre(true);SetTextRightJustify(false);SetTextJustify(false);
        SetTextWrap(0,1);SetTextBackground(false);SetTextProportional(true);SetTextColour(226,229,221,static_cast<u8>(alpha));SetTextEdge(true,0,0,0,220);
    }
    static void text(float x,float y,const char* value,float size=0.25f,unsigned alpha=245,float widthScale=1.0f,float heightScale=1.0f){
        textStyle(size,alpha,widthScale,heightScale);
        Scripting::DisplayTextWithLiteralString(x,y,"STRING",value);
    }

    void clearCompassWaypoint(){
        if(compassWaypoint_.IsValid()&&Scripting::DoesBlipExist(compassWaypoint_)){Scripting::SetRoute(compassWaypoint_,false);Scripting::RemoveBlip(compassWaypoint_);}
        compassWaypoint_={};compassWaypointSource_=0;scanAt_=0;
    }
    void reconcileCompassWaypoint(){
        if(!compassWaypoint_.IsValid())return;
        if(!Scripting::DoesBlipExist(compassWaypoint_)){compassWaypoint_={};return;}
        // Clicking this pin in the pause map creates the engine's normal pin
        // at the same location. Treat that as the standard waypoint toggle;
        // clicking elsewhere replaces the compass pin with the native one.
        for(auto blip:blips_)if(blip.Get()!=compassWaypoint_.Get()&&Scripting::DoesBlipExist(blip)&&Scripting::GetBlipSprite(blip)==Scripting::BLIP_WAYPOINT){
            roadnav::Vec3 point{};const bool same=position(blip,point)&&roadnav::distance2D(point,compassWaypointPoint_)<15;
            clearCompassWaypoint();
            if(same){NativeInvoke::Invoke<Scripting::NATIVE_SWITCH_OFF_WAYPOINT,Scripting::ScriptVoid>();if(Scripting::DoesBlipExist(blip))Scripting::RemoveBlip(blip);}
            else Scripting::SetRoute(blip,true);
            break;
        }
    }
    void setFocusedWaypoint(unsigned handle,DWORD now){
        using namespace Scripting;
        const auto target=asBlip(handle);roadnav::Vec3 point{};
        if(!DoesBlipExist(target)||!position(target,point))return;
        if(compassWaypoint_.IsValid()&&(handle==compassWaypoint_.Get()||handle==compassWaypointSource_||
           compass::sameWaypointDestination(point,compassWaypointPoint_))){
            clearCompassWaypoint();waypointFeedback_="Waypoint cleared";
        }else{
            Blip next{};AddBlipForCoord(point.x,point.y,point.z,&next);
            if(!next.IsValid()||!DoesBlipExist(next)){waypointFeedback_="Waypoint unavailable";waypointFeedbackAt_=now;return;}
            // Use supported scripting natives for this CE prototype. No legacy
            // CFrontEnd address or unverified native waypoint-global writes.
            NativeInvoke::Invoke<NATIVE_SWITCH_OFF_WAYPOINT,ScriptVoid>();
            clearCompassWaypoint();compassWaypoint_=next;compassWaypointSource_=handle;compassWaypointPoint_=point;
            ChangeBlipSprite(next,BLIP_WAYPOINT);ChangeBlipColour(next,7);ChangeBlipAlpha(next,255);
            ChangeBlipNameFromAscii(next,"Compass waypoint");SetBlipAsShortRange(next,false);SetRoute(next,true);
            waypointFeedback_="Waypoint set";
        }
        waypointFeedbackAt_=now;scanAt_=0;
        if(log_){char line[170];std::snprintf(line,sizeof(line),"Compass waypoint: %s focus=%u pin=%u at=%.1f,%.1f,%.1f",waypointFeedback_,handle,compassWaypoint_.Get(),point.x,point.y,point.z);log_(line);}
    }

public:
    explicit CompassHud(void (*log)(const char*)=nullptr,compass::NoticeRenderer* noticeRenderer=nullptr):noticeRenderer_(noticeRenderer),log_(log){}
    void start(){
        char path[MAX_PATH]{};GetModuleFileNameA(nullptr,path,MAX_PATH);
        char* slash=std::strrchr(path,'\\');if(slash){slash[1]=0;strcat_s(path,"GTAIV-QoL.ini");}
        compassDefault_=GetPrivateProfileIntA("Compass","Enabled",1,path)!=0;
        // Defer preference resolution until the initialized ScriptHook tick.
        awareness_.start(log_);
        messages_.start(log_);
        // Observe only hostility declarations, never shared drawing functions.
        // Install before gameplay scripts create their mission enemies.
        const auto module=GetModuleHandleA("ScriptHook.dll");
        using Lookup=void* (__cdecl*)(std::uint32_t);using Names=void* (__cdecl*)(const char*);
        const auto lookup=module?reinterpret_cast<Lookup>(GetProcAddress(module,"?GetNativeAddressFromHash@Game@@SAPAXI@Z")):nullptr;
        const auto names=module?reinterpret_cast<Names>(GetProcAddress(module,"?GetNativeAddress@Game@@SAPAXPBD@Z")):nullptr;
        const auto init=MH_Initialize();if(init!=MH_OK&&init!=MH_ERROR_ALREADY_INITIALIZED)return;
        auto install=[&](unsigned hash,const char* name,void* detour,RadarNative& original,void*& target){
            if(target)return;auto address=lookup?lookup(hash):nullptr;if(!address&&names)address=names(name);
            if(!address||MH_CreateHook(address,detour,reinterpret_cast<void**>(&original))!=MH_OK)return;
            if(MH_EnableHook(address)!=MH_OK){MH_RemoveHook(address);original=nullptr;return;}target=address;
        };
        install(0x7FED5FF2u,"SET_CHAR_AS_ENEMY",reinterpret_cast<void*>(&declareEnemy),originalEnemy_,enemyTarget_);
        install(0xF290CFD8u,"SET_BLIP_AS_FRIENDLY",reinterpret_cast<void*>(&declareFriendly),originalFriendly_,friendlyTarget_);
        if(log_){char line[100];std::snprintf(line,sizeof(line),"Compass: explicit hostility observers enemy=%u friendly=%u",enemyTarget_?1u:0u,friendlyTarget_?1u:0u);log_(line);}
    }
    void requestNavigationRoute(Scripting::Blip blip,bool enabled){awareness_.requestInternalRoute(blip,enabled);}
    void setRouteHelper(unsigned handle){routeHelper_=handle;awareness_.internalRoute(handle);}
    unsigned missionRouteAge(unsigned target){return awareness_.routeRequestAge(target);}
    bool navigationTargetEnemy(Scripting::Blip target){Scripting::ColourIndex colour=0;Scripting::GetBlipColour(target,&colour);return enemyMarker(target,colour,static_cast<unsigned>(Scripting::GetBlipSprite(target)));}
    bool navigationTargetPosition(Scripting::Blip target,roadnav::Vec3& point){return Scripting::DoesBlipExist(target)&&position(target,point);}
    void refreshNavigationBlips(){const DWORD now=GetTickCount();if(!scanAt_||now-scanAt_>=500){scan();scanAt_=now;}reconcileCompassWaypoint();}
    Scripting::Blip navigationWaypoint(){
        refreshNavigationBlips();reconcileCompassWaypoint();
        for(auto blip:blips_)if(Scripting::DoesBlipExist(blip)&&Scripting::GetBlipSprite(blip)==Scripting::BLIP_WAYPOINT)return blip;
        return {};
    }
    Scripting::Blip navigationMissionTarget(){
        using namespace Scripting;
        const bool mission=GetMissionFlag()!=0;const auto requested=awareness_.missionRoute(mission);
        if(!mission)return {};
        refreshNavigationBlips();Blip only{},yellow{};unsigned count=0,yellowCount=0;
        for(auto blip:blips_){
            if(blip.Get()==routeHelper_||!DoesBlipExist(blip)||policeMarker(blip))continue;
            const auto sprite=static_cast<unsigned>(GetBlipSprite(blip));const auto type=GetBlipInfoIdType(blip);
            if(!compass::radarDisplay(NativeInvoke::Invoke<NATIVE_GET_BLIP_INFO_ID_DISPLAY,u32>(blip))||!compass::readBlipAlpha<NativeCalls>(blip.Get()))continue;
            if(blip.Get()==requested&&!compass::ambientMissionSprite(sprite))return blip;
            if(type!=BLIP_TYPE_COORD||sprite==BLIP_WAYPOINT||compass::ambientMissionSprite(sprite))continue;
            ColourIndex colour=0;GetBlipColour(blip,&colour);
            if(compass::missionYellow(colour)){yellow=blip;++yellowCount;}
            if(sprite<=BLIP_OBJECTIVE_5){only=blip;++count;}
        }
        if(yellowCount==1)return yellow;
        if(yellowCount>1)return {};
        return count==1?only:Blip{};
    }

    awareness::RouteKind routeKind(roadnav::Vec3 endpoint){
        refreshNavigationBlips();using namespace Scripting;
        auto result=GetMissionFlag()?awareness::RouteKind::Mission:awareness::RouteKind::Blue;float best=65;
        for(auto blip:blips_){
            if(!DoesBlipExist(blip)||policeMarker(blip))continue;const auto sprite=static_cast<unsigned>(GetBlipSprite(blip));
            if(sprite!=BLIP_WAYPOINT&&sprite>BLIP_OBJECTIVE_5)continue;
            if(!compass::radarDisplay(NativeInvoke::Invoke<NATIVE_GET_BLIP_INFO_ID_DISPLAY,u32>(blip)))continue;
            roadnav::Vec3 point{};if(!position(blip,point))continue;const float distance=roadnav::distance2D(endpoint,point);if(distance>best)continue;
            ColourIndex colour=0;GetBlipColour(blip,&colour);
            result=enemyMarker(blip,colour,sprite)?awareness::RouteKind::Enemy:sprite==BLIP_WAYPOINT?awareness::RouteKind::Custom:awareness::RouteKind::Mission;best=distance;
        }
        return result;
    }
    Scripting::Blip chaseTarget(roadnav::Vec3 player,Scripting::Ped playerPed,unsigned previous){
        refreshNavigationBlips();
        std::vector<compass::ChaseCandidate> candidates;
        Scripting::Vehicle playerCar{};Scripting::GetCarCharIsUsing(playerPed,&playerCar);
        for(auto blip:blips_){
            using namespace Scripting;
            if(!DoesBlipExist(blip)||policeMarker(blip)||!compass::readBlipAlpha<NativeCalls>(blip.Get()))continue;
            ColourIndex colour=0;GetBlipColour(blip,&colour);
            if(!enemyMarker(blip,colour,static_cast<unsigned>(GetBlipSprite(blip))))continue;
            const auto display=NativeInvoke::Invoke<NATIVE_GET_BLIP_INFO_ID_DISPLAY,u32>(blip);
            if(!compass::radarDisplay(display))continue;
            float speed=0;bool inVehicle=false;const auto type=GetBlipInfoIdType(blip);
            if(type==BLIP_TYPE_CAR){auto car=GetBlipInfoIdCarIndex(blip);if(!car.IsValid()||car.Get()==playerCar.Get()||!DoesVehicleExist(car)||IsCarDead(car))continue;GetCarSpeed(car,&speed);inVehicle=true;}
            else if(type==BLIP_TYPE_CHAR){auto ped=GetBlipInfoIdPedIndex(blip);if(!ped.IsValid()||ped.Get()==playerPed.Get()||!DoesCharExist(ped)||IsCharDead(ped))continue;GetCharSpeed(ped,&speed);if(IsCharInAnyCar(ped)){Vehicle car{};GetCarCharIsUsing(ped,&car);if(car.IsValid()&&DoesVehicleExist(car)&&!IsCarDead(car)){GetCarSpeed(car,&speed);inVehicle=true;}}}
            else continue;
            roadnav::Vec3 point{};if(!position(blip,point))continue;
            const float distance=roadnav::distance2D(player,point);
            candidates.push_back({blip.Get(),distance,speed,inVehicle});
        }
        return asBlip(compass::chooseChaseTarget(candidates,previous));
    }
    std::vector<roadnav::Vec3> knownPolice(roadnav::Vec3 player){
        refreshNavigationBlips();using namespace Scripting;
        std::vector<roadnav::Vec3> result;
        for(auto blip:blips_){
            if(result.size()>=48)break;
            if(!DoesBlipExist(blip)||!policeMarker(blip)||!compass::readBlipAlpha<NativeCalls>(blip.Get())||
               !compass::radarDisplay(NativeInvoke::Invoke<NATIVE_GET_BLIP_INFO_ID_DISPLAY,u32>(blip)))continue;
            roadnav::Vec3 point{};if(!position(blip,point)||roadnav::distance2D(player,point)>1200)continue;
            bool duplicate=false;for(auto old:result)if(roadnav::distance2D(old,point)<8)duplicate=true;
            if(!duplicate)result.push_back(point);
        }
        return result;
    }
    bool policeCircle(unsigned player,unsigned stars,roadnav::Vec3& center,float& radius){return awareness_.policeCircle(player,stars,center,radius);}
    void releaseRadar(){minimap_.release();hidden_=false;focused_=0;if(noticeRenderer_)noticeRenderer_->clear();}
    void stop(){
        CompassInput::stop();
        awareness_.stop();messages_.stop();clearCompassWaypoint();waypointHold_={};vehicleGate_={};inputShooting_=false;inputReportAt_=0;
        releaseRadar();
        for(auto target:{enemyTarget_,friendlyTarget_})if(target){MH_DisableHook(target);MH_RemoveHook(target);}
        enemyTarget_=friendlyTarget_=nullptr;originalEnemy_=originalFriendly_=nullptr;
        {std::lock_guard<std::mutex> lock(hostilityMutex_);hostility_={};}
        for(auto texture:textures_)if(texture.IsValid())Scripting::ReleaseTexture(texture);
        // GetTxd borrows the game-owned dictionary; never unload it.
        textures_={};tried_={};dictionary_={};blips_.clear();enemies_.clear();healthDamage_={};armourDamage_={};blipTable_=0;initialized_=false;scanAt_=0;frameGate_={};scanLogged_=startupComplete_=false;
    }
    void drawArrival(){
        if(!qolclient::Enabled(QOL_COMPASS,compassDefault_)||Scripting::IsPauseMenuActive()||!Scripting::IsScreenFadedIn())return;
        text(.5f,.84f,"Destination reached",.3f,240,.85f,1.1f);
    }
    void configureTaxi(float fov){awareness_.configureTaxi(fov);}
    void tick(){
        using namespace Scripting;
        const auto inputNow=GetTickCount();
        const bool nativeWaypointDown=IsButtonPressed(0,static_cast<ePadButton>(10))!=0;
        bool rawController=false;const bool waypointDown=controller_.left(nativeWaypointDown,rawController);
        const bool waypointStart=waypointDown&&!waypointDownLast_;waypointDownLast_=waypointDown;
        const auto cancelWaypoint=[&](){
            CompassInput::publish(false,0,nullptr);
            if(noticeRenderer_)noticeRenderer_->clear();waypointHold_.update(waypointDown,false,0,inputNow);
            if(waypointStart&&log_)log_("Compass waypoint: hold rejected by pause/fade/control/cutscene/compass gate");
        };
        awareness_.activate();awareness_.configureTaxi(static_cast<float>(qolclient::Get(QOL_TAXI_FOV,70)));awareness_.taxiTick();
        if(!qolclient::Enabled(QOL_COMPASS,compassDefault_)){awareness_.clearMarks();awareness_.brief(false);releaseRadar();cancelWaypoint();return;}
        minimap_.enable(log_);hidden_=minimap_.active();
        const auto player=ConvertIntToPlayerIndex(GetPlayerId());Ped ped{};GetPlayerChar(player,&ped);
        if(IsPauseMenuActive()||!IsScreenFadedIn()||!IsHUDPreferenceSwitchedOn()||!IsPlayerControlOn(player)||!ped.IsValid()||!DoesCharExist(ped)){
            focused_=0;cancelWaypoint();return;
        }
        const bool cutscene=NativeInvoke::Invoke<NATIVE_HAS_CUTSCENE_LOADED,b8>() && !NativeInvoke::Invoke<NATIVE_HAS_CUTSCENE_FINISHED,b8>();
        if(cutscene){awareness_.clearMarks();focused_=0;cancelWaypoint();return;}
        const bool inVehicle=IsCharInAnyCar(ped)!=0,vehicleChanged=vehicleGate_.update(inVehicle);
        if(vehicleChanged)waypointHold_.update(waypointDown,false,0,inputNow);
        const bool shooting=IsCharShooting(ped)!=0;
        if(vehicleChanged||shooting!=inputShooting_||(shooting&&inputNow-inputReportAt_>=5000)){
            inputShooting_=shooting;inputReportAt_=inputNow;
            if(log_){const auto& pad=controller_.sample();char line[320];std::snprintf(line,sizeof(line),
                "Compass input observation: at=%lu vehicle=%u transition=%u shooting=%u raw=%u buttons=0x%04X lt=%u rt=%u packet=%lu hold-blocked=%u",
                inputNow,inVehicle?1u:0u,vehicleChanged?1u:0u,shooting?1u:0u,rawController?1u:0u,static_cast<unsigned>(pad.Gamepad.wButtons),
                static_cast<unsigned>(pad.Gamepad.bLeftTrigger),static_cast<unsigned>(pad.Gamepad.bRightTrigger),pad.dwPacketNumber,waypointHold_.blocked?1u:0u);log_(line);}
        }
        Camera camera{};GetRootCam(&camera);if(!camera.IsValid())GetGameCamChild(&camera);if(!camera.IsValid()){focused_=0;cancelWaypoint();return;}
        CompassInput::start(log_);
        CompassInput::publish(!messages_.phoneInUse()&&!IsMobilePhoneCallOngoing(),minimap_.frontendAddress(),&messages_);
        const auto gameFrame=NativeInvoke::Invoke<NATIVE_GET_FRAME_COUNT,u32>();
        if(!frameGate_.accept(gameFrame))return;
        if(!initialized_)initialize();
        const DWORD now=GetTickCount();if(!scanAt_||now-scanAt_>=500){scan();scanAt_=now;}reconcileCompassWaypoint();
        roadnav::Vec3 playerPoint{},rotation{};GetCharCoordinates(ped,&playerPoint.x,&playerPoint.y,&playerPoint.z);GetCamRot(camera,&rotation.x,&rotation.y,&rotation.z);
        if(compassWaypoint_.IsValid()&&compass::waypointReached(playerPoint,compassWaypointPoint_)){
            clearCompassWaypoint();waypointFeedback_="Destination reached";waypointFeedbackAt_=now;
            if(log_)log_("Compass waypoint: cleared on arrival");
        }
        u32 policeStars=0;StoreWantedLevel(player,&policeStars);awareness_.policeSample(player,policeStars,playerPoint);
        using Range=float (__cdecl*)();Range getRange=nullptr;
        const auto ff=GetModuleHandleA("GTAIV.EFLC.FusionFix.asi");if(ff)getRange=reinterpret_cast<Range>(GetProcAddress(ff,"QoLCompassRadarRange"));
        const float liveRange=getRange?getRange():0;const bool live=std::isfinite(liveRange)&&liveRange>=10&&liveRange<=5000;
        const float range=live?liveRange:fallbackRange_;
        const bool mission=GetMissionFlag()!=0;
        if(!mission){social_=false;socialAt_=0;}
        else if(!socialAt_||now-socialAt_>=1000){
            socialAt_=now;social_=false;
            // These are the shipped outing scripts, not the resident friendship
            // managers or dating contact scripts, which run outside outings too.
            for(const char* name:{"go_on_date","date_activity","date_food",
                "romandarts","romandrink","romaneat","romanpool","romanshow","romanstrip","romantenpin",
                "jacobdarts","jacobdrink","jacobeat","jacoblift","jacobpool","jacobshow","jacobstrip",
                "bruciedrink","brucieeat","brucielift","brucieshow","bruciestrip","brucietenpin",
                "dwaynedrink","dwayneeat","dwayneshow","dwaynestrip","dwaynetenpin",
                "packiedarts","packiedrink","packiepool","packieshow","packiestrip","packietenpin",
                "jimhangout","terryhangout","clayhangout","dominicanfriend","friends1","friends2"})
                if(GetNumberOfInstancesOfStreamedScript(name)){social_=true;break;}
        }
        const auto missionBrief=awareness_.brief(mission);
        auto missionTarget=navigationMissionTarget();
        roadnav::Vec3 missionPoint{};const bool haveMissionPoint=missionTarget.IsValid()&&position(missionTarget,missionPoint);
        if(awareness_.due(now)){scan();scanAt_=now;} // Fresh ownership snapshot before creating any attacker blip.
        awareness_.tick(ped,playerPoint,blips_,blipTable_!=0);
        std::vector<compass::Marker> markers;
        unsigned displayFiltered=0,invalidPosition=0,rangeFiltered=0,alphaFiltered=0,textureIcons=0,fallbackIcons=0;
        for(auto blip:blips_){
            if(blip.Get()==routeHelper_||!DoesBlipExist(blip))continue;
            const auto display=NativeInvoke::Invoke<NATIVE_GET_BLIP_INFO_ID_DISPLAY,u32>(blip);
            if(!compass::radarDisplay(display)){++displayFiltered;continue;}
            const auto alpha=compass::readBlipAlpha<NativeCalls>(blip.Get());if(!alpha){++alphaFiltered;continue;}
            roadnav::Vec3 point{};if(!position(blip,point)){++invalidPosition;continue;}
            const unsigned sprite=static_cast<unsigned>(GetBlipSprite(blip));if(sprite>=128||sprite==BLIP_PLAYER||sprite==BLIP_NORTH)continue;
            const float distance=roadnav::distance2D(playerPoint,point);
            const bool outingVenue=social_&&compass::socialVenue(sprite);
            if(!compass::markerInRange(distance,range,IsBlipShortRange(blip)!=0&&!outingVenue)){++rangeFiltered;continue;}
            ColourIndex col=0;GetBlipColour(blip,&col);
            const auto type=GetBlipInfoIdType(blip);
            const bool entity=type==BLIP_TYPE_CAR||type==BLIP_TYPE_CHAR||type==BLIP_TYPE_OBJECT||type==BLIP_TYPE_PICKUP;
            const bool enemy=enemyMarker(blip,col,sprite);
            if(mission&&!entity&&!enemy&&!outingVenue&&compass::ambientMissionSprite(sprite))continue;
            const int priority=sprite==BLIP_WAYPOINT?3:(sprite<=BLIP_OBJECTIVE_5?2:0);
            const bool police=policeMarker(blip);
            const bool objective=!outingVenue&&!police&&awareness::objectiveLabel(mission,enemy,entity,sprite,blip.Get()==missionTarget.Get(),haveMissionPoint?roadnav::distance2D(point,missionPoint):100000.0f);
            if(police)col=enemy?9:3;else if(friendMarker(blip))col=8;else if(enemy)col=9;else if(objective&&sprite!=BLIP_WAYPOINT)col=5;
            markers.push_back({blip.Get(),sprite,compass::relativeBearing(point.x-playerPoint.x,point.y-playerPoint.y,rotation.z),distance,col,std::min(alpha,255u),priority,enemy,objective});
        }
        if(!startupComplete_&&log_)log_("Compass: marker snapshot ready");
        const auto placed=compass::layout(std::move(markers));focused_=compass::focus(placed,focused_);
        const bool onFoot=!inVehicle,phone=messages_.phoneInUse()||IsMobilePhoneCallOngoing(),combat=shooting||IsPlayerTargettingAnything(player);
        const bool waypointEligible=compass::waypointEligible(phone,combat);
        unsigned holdFocus=focused_;
        // Keep the same selected marker through small camera bob at the edge
        // of the normal focus window; another focused marker still cancels.
        if(!holdFocus&&waypointHold_.target)for(const auto& item:placed)if(item.marker.handle==waypointHold_.target&&std::fabs(item.marker.bearing)<=8)holdFocus=waypointHold_.target;
        if(waypointStart&&log_){char line[220];std::snprintf(line,sizeof(line),"Compass waypoint: press raw=%u native=%u focus=%u on-foot=%u phone=%u combat=%u",rawController?1u:0u,nativeWaypointDown?1u:0u,holdFocus,onFoot?1u:0u,phone?1u:0u,combat?1u:0u);log_(line);}
        const bool blockedBefore=waypointHold_.blocked;
        if(const auto target=waypointHold_.update(waypointDown,waypointEligible,holdFocus,now))setFocusedWaypoint(target,now);
        if(!blockedBefore&&waypointHold_.blocked&&log_)log_("Compass waypoint: hold cancelled; release and hold again (focus/gate/tick gap)");
        float aspect=NativeInvoke::Invoke<NATIVE_GET_ASPECT_RATIO,float>();if(!std::isfinite(aspect)||aspect<.5f||aspect>5)aspect=16.0f/9;
        // Restrained GTA IV palette: charcoal, ivory, green health, blue armour.
        using Layout=compass::StripLayout;
        constexpr float left=Layout::left,right=Layout::right,mid=Layout::center,half=Layout::half;
        if(!startupComplete_&&log_)log_("Compass: drawing native strip");
        // No filled panel or bottom border; the top line is the health/armour meter.
        for(unsigned tick=0;tick<24;++tick){
            const float bearing=compass::wrap(static_cast<float>(tick)*15+rotation.z);if(std::fabs(bearing)>90)continue;
            const float x=mid+half*bearing/90;
            rect(x,Layout::tickY,.0007f,tick%6==0?.005f:.003f,210,214,204,135);
            bool covered=false;for(const auto& item:placed)if(std::fabs(x-(mid+half*item.x))<.0483f/(2*aspect)+.006f){covered=true;break;}
            if(tick%6==0&&!covered){const char* labels[]={"N","E","S","W"};text(x,Layout::labelY,labels[tick/6],.23f,235,.78f);}
        }
        const float hold=waypointHold_.progress(now);
        const auto message=messages_.snapshot();
        compass::NoticeFrame noticeFrame{message.alpha(now),now,minimap_.frontendAddress()};
        for(const auto& item:placed){
            const auto& marker=item.marker;const float x=mid+half*item.x,iconY=Layout::iconY;
            if(!tried_[marker.sprite]&&dictionary_.IsValid()){
                tried_[marker.sprite]=true;if(!names_[marker.sprite].empty())textures_[marker.sprite]=GetTexture(dictionary_,names_[marker.sprite].c_str());
            }
            const bool centered=marker.handle==focused_;const float scale=compass::iconScale(marker.enemy,marker.priority>=2);const float height=(centered?.023f:.020f)*2.10f*scale;
            if(textures_[marker.sprite].IsValid()){
                ++textureIcons;
                if(!startupComplete_&&log_){char line[80];std::snprintf(line,sizeof(line),"Compass: drawing texture sprite=%u",marker.sprite);log_(line);}
                const auto rgb=compass::markerColour(marker.colour);
                NativeInvoke::Invoke<NATIVE_DRAW_SPRITE,ScriptVoid>(textures_[marker.sprite],x,iconY,height/aspect,height,0.0f,rgb[0],rgb[1],rgb[2],std::min(marker.alpha,210u));
            }else {++fallbackIcons;const auto rgb=compass::markerColour(marker.colour);textStyle(.672f*scale,marker.alpha);SetTextColour(static_cast<u8>(rgb[0]),static_cast<u8>(rgb[1]),static_cast<u8>(rgb[2]),static_cast<u8>(marker.alpha));DisplayTextWithLiteralString(x,iconY-.011f,"STRING",marker.sprite==BLIP_WAYPOINT?"X":(marker.sprite<=BLIP_OBJECTIVE_5?"*":"."));}
            const bool holding=hold>0&&marker.handle==waypointHold_.target;
            if(holding){noticeFrame.holdX=x;noticeFrame.holdY=iconY;noticeFrame.holdRadius=height*.62f;noticeFrame.holdProgress=hold;}
            if(centered||holding){
                float distanceY=Layout::distanceY;
                auto objectiveText=marker.enemy?std::string{}:awareness_.briefFor(marker.handle);
                if(objectiveText.empty()&&marker.missionDestination&&missionTarget.IsValid())objectiveText=awareness_.briefFor(missionTarget.Get());
                if(objectiveText.empty()&&marker.missionDestination)objectiveText=missionBrief;
                if(holding){
                    text(x,Layout::focusLabelY,"Setting waypoint",.22f,235,.85f,1.10f);
                }else if(!objectiveText.empty()){
                    const auto lines=awareness::briefLines(objectiveText);
                    for(unsigned row=0;row<lines.size();++row)text(mid,Layout::focusLabelY+.024f*row,lines[row].c_str(),.22f,235,.85f,1.10f);
                    distanceY=Layout::focusLabelY+.024f*static_cast<float>(lines.size());
                }else{
                    textStyle(.22f,235,.85f,1.10f);
                    NativeInvoke::Invoke<NATIVE_DISPLAY_TEXT_WITH_BLIP_NAME,ScriptVoid>(mid,Layout::focusLabelY,"STRING",marker.handle);
                }
                char distance[32];const float number=metric_?marker.distance:marker.distance*3.2808399f;
                std::snprintf(distance,sizeof(distance),"%.0f %s",number,metric_?"m":"ft");text(mid,distanceY,distance,.22f,215,1.0f,1.10f);}
        }
        rect(mid,Layout::lineY-.002f,.0015f,.006f,235,237,225,195);
        if(!startupComplete_&&log_)log_("Compass: reading health and armour");
        u32 health=0,armour=0,maxHealth=200,maxArmour=100;GetCharHealth(ped,&health);GetCharArmour(ped,&armour);
        NativeInvoke::Invoke<NATIVE_GET_PLAYER_MAX_HEALTH,ScriptVoid>(player,&maxHealth);GetPlayerMaxArmour(player,&maxArmour);
        const float hp=compass::healthFraction(static_cast<int>(health),static_cast<int>(maxHealth));
        const float ap=compass::armourFraction(static_cast<int>(armour),static_cast<int>(maxArmour));
        unsigned screenWidth=0,screenHeight=0;NativeInvoke::Invoke<NATIVE_GET_SCREEN_RESOLUTION,ScriptVoid>(&screenWidth,&screenHeight);
        const float barY=compass::raisedMeterY(screenHeight);constexpr float barWidth=Layout::barWidth;
        rect((left+mid)/2-.001f,barY,barWidth,Layout::barHeight,0,0,0,190);rect((mid+right)/2+.001f,barY,barWidth,Layout::barHeight,0,0,0,190);
        const auto healthSpan=compass::meterSpan(hp,true),armourSpan=compass::meterSpan(ap,false);
        if(hp>0)rect((healthSpan.left+healthSpan.right)/2,barY,healthSpan.right-healthSpan.left,Layout::barHeight,133,181,104,210);
        if(ap>0)rect((armourSpan.left+armourSpan.right)/2,barY,armourSpan.right-armourSpan.left,Layout::barHeight,125,179,199,210);
        healthDamage_.update(hp,now);armourDamage_.update(ap,now);
        const auto flashMeter=[&](const compass::DamageFlash& flash,bool health){
            const auto alpha=flash.alpha(now);if(!alpha)return;
            const auto span=compass::meterSpan(1,health);
            rect((span.left+span.right)/2,barY,span.right-span.left,Layout::barHeight,235,42,38,alpha);
        };
        flashMeter(healthDamage_,true);flashMeter(armourDamage_,false);
        if(noticeRenderer_)noticeRenderer_->submit(noticeFrame);
        if(hold<=0&&waypointFeedbackAt_&&now-waypointFeedbackAt_<1800)text(Layout::right+.065f,Layout::iconY,waypointFeedback_,.19f,235);
        // Reset native text state touched by our HUD calls for other script HUDs.
        SetTextCentre(false);SetTextEdge(false,0,0,0,0);SetTextScale(.3f,.3f);SetTextColour(255,255,255,255);
        startupComplete_=true;
        if(!logAt_||now-logAt_>=10000){if(log_){char line[180];std::snprintf(line,sizeof(line),"Compass: blips=%u drawn=%u range=%.1fm (%s) focus=%u hp=%.2f armour=%.2f",static_cast<unsigned>(blips_.size()),static_cast<unsigned>(placed.size()),range,live?"live":"fallback",focused_,hp,ap);log_(line);
            std::snprintf(line,sizeof(line),"Compass minimap: mask=%s radar-layout-queries=%u ring=%s frame-calls=%u hidden=%u fill=%u/%u native-visibility=original",minimap_.status(),minimap_.count(),minimap_.ringStatus(),minimap_.ringSelections(),minimap_.ringHidden(),minimap_.fillHidden(),minimap_.fillCalls());log_(line);
            std::snprintf(line,sizeof(line),"Compass messages: observer=%u waiting=%u updates=%u icon-alpha=%u",messages_.active()?1u:0u,message.waiting?1u:0u,messages_.updates(),message.alpha(now));log_(line);
            unsigned focusedSprite=999,focusedObjective=0;for(const auto& item:placed)if(item.marker.handle==focused_){focusedSprite=item.marker.sprite;focusedObjective=item.marker.missionDestination?1u:0u;}
            std::snprintf(line,sizeof(line),"Compass objective: mission=%u social=%u target=%u focused-sprite=%u focused-objective=%u brief-chars=%u",mission?1u:0u,social_?1u:0u,missionTarget.Get(),focusedSprite,focusedObjective,static_cast<unsigned>(missionBrief.size()));log_(line);
            std::snprintf(line,sizeof(line),"Compass bound objective: focus-chars=%u target-chars=%u",static_cast<unsigned>(awareness_.briefFor(focused_).size()),static_cast<unsigned>(awareness_.briefFor(missionTarget.Get()).size()));log_(line);
            std::snprintf(line,sizeof(line),"Compass filters: display=%u alpha=%u position=%u range=%u icons=%u fallback-icons=%u",displayFiltered,alphaFiltered,invalidPosition,rangeFiltered,textureIcons,fallbackIcons);log_(line);}logAt_=now;}
    }
};
