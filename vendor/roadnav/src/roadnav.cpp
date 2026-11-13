// GTA IV RoadNav: 1.2.0.59 route-reader proof of concept.
// Based on the game-memory layout documented by garuspl/gta4-gps-hud.
//
// Native RoadNav renderer with optional owned chase-route blips.
// Diagnostic build: verifies executable layout and GPS memory addresses.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <cmath>
#include <utility>
#include <atomic>

#include "route_geometry.hpp"
#include "marker_geometry.hpp"
#include "turn_renderer.hpp"
#include "road_line_renderer.hpp"
#include "gps_route_reader.hpp"
#include "road_surface.hpp"
#include "../sdk/ScriptHookManager.h"
#include "../sdk/ScriptThread.h"
#include "../sdk/Scripting.h"

#include "build_info.hpp"
#include "../../../src/qol_settings_client.hpp"
#include "compass_hud.hpp"
#include "../../../src/police_escape.hpp"
#include "../../../src/escape_gps_request.hpp"
#include "../../../src/primary_route_replan.hpp"
#include "../../../src/passive_crash_capture.hpp"

namespace {

constexpr std::uintptr_t preferredBase = 0x00D90000u;
constexpr std::uintptr_t slotsVa = 0x01B332C0u;
constexpr std::uintptr_t routeSizeFunctionVa = 0x012C6480u;
constexpr std::uintptr_t routeSizeGlobalVa = 0x01B34DD0u;

// The upstream mod's addresses are relative to its 0x00D90000 reference base,
// not GTAIV.exe's PE ImageBase. Preserve negative relocation adjustments.
std::uintptr_t adjustedAddress(std::uintptr_t address, std::int64_t delta) {
    return static_cast<std::uintptr_t>(
        static_cast<std::int64_t>(address) + delta
    );
}


constexpr DWORD pollingMs = 500;

volatile LONG shuttingDown = 0;

using Route=roadnav::NativeGpsRoute;

// Read memory without directly dereferencing unverified addresses.
bool readMemory(
    std::uintptr_t addr,
    void* destination,
    std::size_t bytes
) {
    SIZE_T copied = 0;

    return ReadProcessMemory(
        GetCurrentProcess(),
        reinterpret_cast<const void*>(addr),
        destination,
        bytes,
        &copied
    ) != 0 && copied == bytes;
}

// Find GTAIV.exe's directory.
std::string executableFolder() {
    char path[MAX_PATH]{};

    DWORD n = GetModuleFileNameA(
        nullptr,
        path,
        MAX_PATH
    );

    if (n == 0 || n >= MAX_PATH) {
        return ".\\";
    }

    std::string out(path);

    const auto pos = out.find_last_of("\\/");

    return pos == std::string::npos
        ? ".\\"
        : out.substr(0, pos + 1);
}

// Write a line to RoadNav.log.
void writeLine(FILE* file, const char* message) {
    if (!file) {
        return;
    }

    std::fprintf(file, "%s\n", message);
    std::fflush(file);
}

// Inspect already loaded scripting bridges. Read-only; never load a DLL or call natives.
void logNativeBridge(FILE* log) {
    const char* dlls[] = {
        "ScriptHook.dll", "ScriptHookIV.dll", "LuaModLoader.asi", "IVSDKDotNet.asi"
    };
    const char* names[] = {
        "scriptRegister", "scriptUnregister", "nativeInit", "nativePush", "nativeCall"
    };
    writeLine(log, "Native bridge probe (loaded modules only):");
    for (const char* dll : dlls) {
        HMODULE mod = GetModuleHandleA(dll);
        char line[256];
        std::snprintf(line, sizeof(line), "  %s: %s", dll, mod ? "loaded" : "not loaded");
        writeLine(log, line);
        if (!mod) continue;
        for (const char* name : names) {
            const FARPROC fn = GetProcAddress(mod, name);
            std::snprintf(line, sizeof(line), "    export %s: %s", name, fn ? "present" : "absent");
            writeLine(log, line);
        }
    }
    writeLine(log, "Probe complete; no game native functions were called.");
}

// Print the executable's actual memory layout.
void logExecutableInfo(
    FILE* log,
    std::uintptr_t actual,
    std::int64_t delta
) {
    IMAGE_DOS_HEADER dos{};

    if (!readMemory(actual, &dos, sizeof(dos))) {
        writeLine(log, "PE diagnostic: could not read DOS header.");
        return;
    }

    if (dos.e_magic != IMAGE_DOS_SIGNATURE) {
        writeLine(log, "PE diagnostic: invalid DOS signature.");
        return;
    }

    if (dos.e_lfanew < 0 || dos.e_lfanew > 0x100000) {
        writeLine(log, "PE diagnostic: invalid NT header offset.");
        return;
    }

    IMAGE_NT_HEADERS32 nt{};

    if (!readMemory(
        actual + static_cast<std::uintptr_t>(dos.e_lfanew),
        &nt,
        sizeof(nt)
    )) {
        writeLine(log, "PE diagnostic: could not read NT headers.");
        return;
    }

    if (nt.Signature != IMAGE_NT_SIGNATURE) {
        writeLine(log, "PE diagnostic: invalid NT signature.");
        return;
    }

    if (nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
        writeLine(log, "PE diagnostic: executable is not PE32.");
        return;
    }

    char info[256];

    std::snprintf(
        info,
        sizeof(info),
        "PE preferred base: 0x%08X | Image size: 0x%08X | Image end: 0x%08X",
        static_cast<unsigned>(nt.OptionalHeader.ImageBase),
        static_cast<unsigned>(nt.OptionalHeader.SizeOfImage),
        static_cast<unsigned>(actual + nt.OptionalHeader.SizeOfImage)
    );

    writeLine(log, info);

    const std::uintptr_t imageEnd =
        actual + nt.OptionalHeader.SizeOfImage;

    if (
        nt.OptionalHeader.SizeOfImage == 0 ||
        imageEnd < actual
    ) {
        writeLine(log, "PE diagnostic: invalid image size.");
        return;
    }

    // Check whether our expected GPS addresses fall inside the image.
    const std::uintptr_t addresses[] = {
        adjustedAddress(routeSizeFunctionVa, delta),
        adjustedAddress(routeSizeGlobalVa, delta),
        adjustedAddress(slotsVa, delta)
    };

    const char* names[] = {
        "GPS function",
        "GPS size global",
        "GPS route slots"
    };

    for (int i = 0; i < 3; ++i) {
        const auto addr = addresses[i];

        const bool inside =
            addr >= actual &&
            addr < imageEnd;

        std::snprintf(
            info,
            sizeof(info),
            "%s address 0x%08X: %s executable image",
            names[i],
            static_cast<unsigned>(addr),
            inside ? "inside" : "outside"
        );

        writeLine(log, info);
    }
}

// Inspect the six bytes at the expected GPS function.
void logGpsFunctionBytes(
    FILE* log,
    std::int64_t delta
) {
    const auto address = adjustedAddress(routeSizeFunctionVa, delta);

    std::array<std::uint8_t, 6> probe{};

    if (!readMemory(
        address,
        probe.data(),
        probe.size()
    )) {
        writeLine(
            log,
            "Could not read expected GPS function address."
        );
        return;
    }

    char info[192];

    std::snprintf(
        info,
        sizeof(info),
        "GPS function at 0x%08X: %02X %02X %02X %02X %02X %02X",
        static_cast<unsigned>(address),
        static_cast<unsigned>(probe[0]),
        static_cast<unsigned>(probe[1]),
        static_cast<unsigned>(probe[2]),
        static_cast<unsigned>(probe[3]),
        static_cast<unsigned>(probe[4]),
        static_cast<unsigned>(probe[5])
    );

    writeLine(log, info);
}

// Verify the executable before reading GPS route memory.
bool supportedVersion(std::int64_t delta) {
    auto* const module =
        reinterpret_cast<const std::uint8_t*>(
            GetModuleHandleA(nullptr)
        );

    if (!module) {
        return false;
    }

    IMAGE_DOS_HEADER dos{};

    if (!readMemory(
        reinterpret_cast<std::uintptr_t>(module),
        &dos,
        sizeof(dos)
    )) {
        return false;
    }

    if (
        dos.e_magic != IMAGE_DOS_SIGNATURE ||
        dos.e_lfanew < 0 ||
        dos.e_lfanew > 0x100000
    ) {
        return false;
    }

    IMAGE_NT_HEADERS32 nt{};

    if (!readMemory(
        reinterpret_cast<std::uintptr_t>(module) +
            static_cast<std::uintptr_t>(dos.e_lfanew),
        &nt,
        sizeof(nt)
    )) {
        return false;
    }

    if (
        nt.Signature != IMAGE_NT_SIGNATURE ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC
    ) {
        return false;
    }

    // Expected instruction:
    // A1 <4-byte address> C3
    //
    // MOV EAX, [routeSizeGlobal]
    // RET

    std::array<std::uint8_t, 6> bytes{};

    if (!readMemory(
        adjustedAddress(routeSizeFunctionVa, delta),
        bytes.data(),
        bytes.size()
    )) {
        return false;
    }

    std::uint32_t referencedAddress{};

    std::memcpy(
        &referencedAddress,
        bytes.data() + 1,
        sizeof(referencedAddress)
    );

    const auto expected =
        static_cast<std::uint32_t>(
            adjustedAddress(routeSizeGlobalVa, delta)
        );

    return (
        bytes[0] == 0xA1 &&
        bytes[5] == 0xC3 &&
        referencedAddress == expected
    );
}

// Read GPS route points from the game.
bool readRoute(std::int64_t delta,Route& result,const roadnav::Vec3* expectedEnd=nullptr,const roadnav::Vec3* start=nullptr){
    return roadnav::readGpsRoute(adjustedAddress(slotsVa,delta),readMemory,result,expectedEnd,start);
}

// Background diagnostic thread.
DWORD WINAPI worker(LPVOID) {

    const auto folder = executableFolder();
    if(!GetPrivateProfileIntA("Diagnostics","LogToFile",0,(folder+"GTAIV-QoL.ini").c_str()))return 0;

    FILE* log = std::fopen(
        (folder + "RoadNav.log").c_str(),
        "w"
    );

    writeLine(
        log,
        "RoadNav v0.7 SDK dispatcher linked turns and destination | build=" ROADNAV_BUILD_ID " | compiled=" __DATE__ " " __TIME__
    );

    auto* const module = GetModuleHandleA(nullptr);

    if (!module) {
        writeLine(log, "Failed to obtain executable module.");

        if (log) {
            std::fclose(log);
        }

        return 0;
    }

    const auto actual =
        reinterpret_cast<std::uintptr_t>(module);

    char baseInfo[128];

    std::snprintf(
        baseInfo,
        sizeof(baseInfo),
        "Actual image base: 0x%08X",
        static_cast<unsigned>(actual)
    );

    writeLine(log, baseInfo);

    // The reference address is above GTAIV.exe's actual PE image base.
    // Use a signed adjustment; negative values are expected.
    const std::int64_t delta =
        static_cast<std::int64_t>(actual) -
        static_cast<std::int64_t>(preferredBase);

    // Inspect the correctly adjusted GPS addresses before touching route data.
    logExecutableInfo(log, actual, delta);

    // Inspect the expected function before attempting GPS reads.
    logGpsFunctionBytes(log, delta);

    if (!supportedVersion(delta)) {

        writeLine(
            log,
            "Version/signature mismatch. GPS route reader disabled."
        );

        if (log) {
            std::fclose(log);
        }

        return 0;
    }

    writeLine(
        log,
        "Signature matched: route reader active. Set a GPS waypoint and drive."
    );

    // Give other ASI modules a moment to initialize before examining exports.
    Sleep(2000);
    logNativeBridge(log);

    int previousSlot = -99;
    int previousState = -99;
    int previousCount = -99;

    DWORD lastSnapshot = 0;

    while (
        InterlockedCompareExchange(
            &shuttingDown,
            0,
            0
        ) == 0
    ) {

        Route r;

        const bool active = readRoute(delta, r);

        const int slot = active ? r.slot : -1;
        const int state = active ? r.state : 0;

        const int count = active
            ? static_cast<int>(r.points.size())
            : 0;

        const DWORD now = GetTickCount();

        if (
            slot != previousSlot ||
            state != previousState ||
            count != previousCount ||
            (active && (now - lastSnapshot) > 5000u)
        ) {

            if (log) {

                if (active) {

                    const float length =
                        roadnav::routeLength2D(r.points);

                    const auto samples =
                        roadnav::makeSamples(
                            r.points,
                            9.0f,
                            60.0f
                        );

                    std::fprintf(
                        log,
                        "route slot=%d state=%d points=%d length2D=%.1fm arrowCandidates=%u\n",
                        slot,
                        state,
                        count,
                        length,
                        static_cast<unsigned>(samples.size())
                    );

                    // Log the first two GPS coordinates.
                    for (
                        std::size_t i = 0;
                        i < r.points.size() && i < 2;
                        ++i
                    ) {
                        std::fprintf(
                            log,
                            "  point[%u]: %.2f %.2f %.2f\n",
                            static_cast<unsigned>(i),
                            r.points[i].x,
                            r.points[i].y,
                            r.points[i].z
                        );
                    }

                    // Log the first candidate arrow position.
                    if (!samples.empty()) {

                        const auto& s = samples.front();

                        std::fprintf(
                            log,
                            "  candidate[0]: %.2f %.2f %.2f direction=%.2f %.2f\n",
                            s.position.x,
                            s.position.y,
                            s.position.z,
                            s.direction.x,
                            s.direction.y
                        );
                    }

                } else {

                    std::fprintf(
                        log,
                        "route inactive\n"
                    );
                }

                std::fflush(log);
            }

            lastSnapshot = now;
            previousSlot = slot;
            previousState = state;
            previousCount = count;
        }

        Sleep(pollingMs);
    }

    writeLine(log, "Stopping.");

    if (log) {
        std::fclose(log);
    }

    return 0;
}


// Natives and camera sampling stay on the SDK-owned game thread; rendering uses
// only a locked snapshot and never calls game scripting natives from D3D callbacks.
std::atomic<bool> navigationDiagnostics{false};
void turnRendererEvent(const char* message){
    if(!navigationDiagnostics)return;
    FILE* file=std::fopen((executableFolder()+"RoadNav-arrow.log").c_str(),"a");
    if(file){std::fprintf(file,"%s\n",message);std::fclose(file);}
}
roadnav::TurnRenderer gTurnRenderer{turnRendererEvent};
roadnav::RoadLineRenderer gRoadLineRenderer{turnRendererEvent};
roadnav::RoadLineRenderer gEscapeLineRenderer{turnRendererEvent};
compass::NoticeRenderer gCompassNoticeRenderer;
class RoadNavScript final : public ScriptThread {
    CompassHud compass_{turnRendererEvent,&gCompassNoticeRenderer};
    DWORD previousTick_=0, lastServiceTry_=0, lastCueLog_=0;
    bool tickLogged_=false, attached_=false, wasVisible_=false;
    roadnav::NavigationCues cues_{};
    roadnav::RoadLine line_{},escapeLine_{};
    Scripting::Blip primaryRouteBlip_{},forwardRouteHelper_{};DWORD primaryWarmAt_=0;
    roadnav::PrimaryWrongWay primaryWrongWay_{};
    Route forwardRoute_{};roadnav::Vec3 forwardTarget_{},forwardHeading_{},forwardGoal_{};
    DWORD forwardRequestedAt_=0,forwardRestoreAt_=0;bool forwardWaiting_=false,primaryReplanned_=false;
    Route recentRoute_{};DWORD recentRouteAt_=0;unsigned recentTarget_=0,arrivalTarget_=0;bool arrivalArmed_=false;roadnav::Vec3 arrivalPoint_{};DWORD arrivalSeen_=0;
    awareness::RouteKind routeKind_=awareness::RouteKind::Blue;
    roadnav::Vec3 cachedEnd_{},cachedWaypoint_{};
    DWORD cachedAt_=0;
    bool haveArrival_=false;
    unsigned lastCueCount_=0;
    bool lastDestination_=false;
    Scripting::Blip chaseRoute_{};
    unsigned chaseTarget_=0;
    bool escapeActive_=false,escapeAnchored_=false;
    Route escapeShown_{},escapeBest_{};
    std::vector<policeescape::Candidate> escapeOptions_;
    std::size_t escapeOption_=0;bool escapePlanning_=false;
    float escapeCommittedRisk_=0;
    policeescape::ReplanState escapeReplan_{};
    roadnav::RoadLineViewBend viewBend_{};
    int loggedLineMode_=-1;
    const char* escapeSuppressed_=nullptr;DWORD escapeSuppressedAt_=0;
    float escapeBestScore_=0;roadnav::Vec3 escapeBestPoint_{};
    DWORD escapeRequestedAt_=0;
    unsigned escapeStars_=0;roadnav::Vec3 escapeCenter_{};
    roadnav::Vec3 chasePoint_{};
    DWORD chaseRefreshAt_=0;
    bool chaseEnabled_=true,guidanceEnemy_=true;
    DWORD noRouteSince_=0,missionRetryAt_=0;unsigned missionRetryTarget_=0;
    void clearChase(){
        if(chaseRoute_.IsValid()&&Scripting::DoesBlipExist(chaseRoute_)){compass_.requestNavigationRoute(chaseRoute_,false);Scripting::RemoveBlip(chaseRoute_);event("Chase guidance: temporary route removed");}
        if(escapeActive_&&primaryRouteBlip_.IsValid()&&Scripting::DoesBlipExist(primaryRouteBlip_))compass_.requestNavigationRoute(primaryRouteBlip_,true);
        escapeLine_={};gEscapeLineRenderer.clear();
        escapeShown_={};escapeBest_={};escapeOptions_.clear();escapePlanning_=false;escapeReplan_={};
        chaseRoute_={};chaseTarget_=0;escapeActive_=false;compass_.setRouteHelper(0);
        escapeAnchored_=false;escapeStars_=0;escapeSuppressed_=nullptr;escapeSuppressedAt_=0;
    }
    struct EscapeGps {
        CompassHud& compass;
        bool exists(Scripting::Blip blip){return blip.IsValid()&&Scripting::DoesBlipExist(blip);}
        bool same(Scripting::Blip a,Scripting::Blip b){return a.Get()==b.Get();}
        void route(Scripting::Blip blip,bool enabled){compass.requestNavigationRoute(blip,enabled);}
        void remove(Scripting::Blip blip){Scripting::RemoveBlip(blip);}
        Scripting::Blip add(roadnav::Vec3 point){Scripting::Blip blip{};Scripting::AddBlipForCoord(point.x,point.y,point.z,&blip);return blip;}
        bool move(Scripting::Blip blip,roadnav::Vec3 point){
            using Native=void(__cdecl*)(GameTypes::scrNativeCallContext*);
            using Lookup=void*(__cdecl*)(const char*);
            static Native update=nullptr;static bool tried=false;
            if(!tried){tried=true;const auto module=GetModuleHandleA("ScriptHook.dll");
                const auto lookup=module?reinterpret_cast<Lookup>(GetProcAddress(module,"?GetNativeAddress@Game@@SAPAXPBD@Z")):nullptr;
                update=lookup?reinterpret_cast<Native>(lookup("SET_BLIP_COORDINATES")):nullptr;
            }
            if(!update)return false;
            NativeContext context;context.Push(blip);context.Push(point.x);context.Push(point.y);context.Push(point.z);update(&context);
            Scripting::Vector3 actual{};Scripting::GetBlipCoords(blip,&actual);
            return std::fabs(actual.X-point.x)<2&&std::fabs(actual.Y-point.y)<2&&std::fabs(actual.Z-point.z)<2;
        }
        void configure(Scripting::Blip blip){
            compass.setRouteHelper(blip.Get());Scripting::ChangeBlipAlpha(blip,255);Scripting::ChangeBlipColour(blip,9);
            Scripting::ChangeBlipNameFromAscii(blip,"Escape route");
        }
    };
    struct ForwardGps:EscapeGps {
        explicit ForwardGps(CompassHud& hud):EscapeGps{hud}{}
        void configure(Scripting::Blip blip){
            compass.setForwardRouteHelper(blip.Get());Scripting::ChangeBlipAlpha(blip,255);
            Scripting::ChangeBlipColour(blip,7);Scripting::ChangeBlipNameFromAscii(blip,"Route turn");
        }
    };
    void finishForwardRoute(DWORD now,bool retain=false){
        const bool owned=forwardRouteHelper_.IsValid();ForwardGps api{compass_};
        if(owned){
            policeescape::restorePrimaryGps(forwardRouteHelper_,primaryRouteBlip_,api);
            if(api.exists(forwardRouteHelper_))api.remove(forwardRouteHelper_);
            forwardRestoreAt_=now;
        }
        forwardRouteHelper_={};if(!retain)forwardRoute_={};forwardWaiting_=false;compass_.setForwardRouteHelper(0);
    }
    void updatePrimaryReplan(DWORD now,bool eligible,roadnav::Vec3 player,roadnav::Vec3 heading,
                             roadnav::Vec3 velocity,roadnav::Vec3 goal,std::int64_t delta){
        ForwardGps api{compass_};
        if(!eligible){finishForwardRoute(now);primaryWrongWay_.cancel();return;}
        if(forwardRouteHelper_.IsValid()){
            if(!api.exists(forwardRouteHelper_)||!roadnav::samePrimaryGoal(goal,forwardGoal_)||
               now-forwardRequestedAt_>15000||roadnav::reachedForwardNode(player,forwardTarget_,forwardHeading_)){
                finishForwardRoute(now,roadnav::samePrimaryGoal(goal,forwardGoal_));event("Road line replan: original destination restored; last route retained");return;
            }
            if(forwardWaiting_){
                Route candidate;
                if(now-forwardRequestedAt_>=200&&readRoute(delta,candidate,&forwardTarget_)&&
                   roadnav::forwardGpsRoute(candidate.points,player,heading,forwardTarget_)){
                    forwardRoute_=std::move(candidate);forwardWaiting_=false;
                    event("Road line replan: forward native road leg committed; original waypoint unchanged");
                }else if(now-forwardRequestedAt_>=3000){finishForwardRoute(now,true);event("Road line replan: no forward native path; original route retained");}
            }
            return;
        }
        if(forwardRestoreAt_){
            if(now-forwardRestoreAt_>=10000&&api.exists(primaryRouteBlip_)){
                api.route(primaryRouteBlip_,false);api.route(primaryRouteBlip_,true);forwardRestoreAt_=now;
                event("Road line replan: retry original GPS; last valid line retained");
            }
            return;
        }
        if(!primaryWrongWay_.update(now,primaryRouteBlip_.Get(),
            roadnav::movingAgainstRoute(recentRoute_.points,player,heading,velocity),goal))return;
        const auto snap=[](roadnav::Vec3 probe,unsigned n,roadnav::Vec3& node){float angle=0;
            return Scripting::GetNthClosestCarNodeWithHeading(probe.x,probe.y,probe.z,n,&node.x,&node.y,&node.z,&angle)!=0;};
        if(!roadnav::forwardRoadTarget(player,heading,motionSpeed_,snap,forwardTarget_)){
            event("Road line replan: no forward road node; original route retained, retry backed off");return;
        }
        auto helper=policeescape::requestGps(Scripting::Blip{},primaryRouteBlip_,forwardTarget_,api);
        if(!api.exists(helper)){event("Road line replan: request unavailable; original route retained");return;}
        forwardRouteHelper_=helper;forwardHeading_=heading;forwardGoal_=goal;
        forwardRequestedAt_=now;forwardWaiting_=true;primaryReplanned_=true;
        event("Road line replan: sustained moving wrong-way; requesting forward road leg, retaining line");
    }
    void restorePrimaryGps(){EscapeGps api{compass_};policeescape::restorePrimaryGps(chaseRoute_,primaryRouteBlip_,api);}
    bool requestEscape(roadnav::Vec3 point){
        EscapeGps api{compass_};const unsigned previous=chaseRoute_.Get();
        auto next=policeescape::requestGps(chaseRoute_,primaryRouteBlip_,point,api);
        if(!api.exists(next)){chaseRoute_={};return false;}
        chaseRoute_=next;chaseTarget_=0;escapeActive_=true;guidanceEnemy_=false;
        chasePoint_=point;escapeRequestedAt_=GetTickCount();
        char line[160];std::snprintf(line,sizeof(line),"Police escape: GPS handoff helper=%u reused=%u primary-suspended=%u target=%.1f,%.1f,%.1f",next.Get(),next.Get()==previous?1u:0u,api.exists(primaryRouteBlip_)?1u:0u,point.x,point.y,point.z);event(line);
        return true;
    }
    bool updateEscape(Scripting::Ped ped,roadnav::Vec3 player,bool existingRoute,const Route& native,roadnav::Vec3 travel){
        using namespace Scripting;
        const auto index=ConvertIntToPlayerIndex(GetPlayerId());u32 stars=0;StoreWantedLevel(index,&stars);
        if(!stars){if(escapeActive_||escapeReplan_.observed){event("Police escape: dropped; wanted level cleared");clearChase();}escapeAnchored_=false;escapeStars_=0;return false;}
        if(stars>6)return true; // A bad sample must not discard committed guidance.
        if(primaryWarmAt_&&GetTickCount()-primaryWarmAt_<1500)return true;
        primaryWarmAt_=0;
        float radius=0;roadnav::Vec3 center{};
        const auto signals=wantednative::sample(index,stars);
        const bool inSight=signals.known?!signals.grey:escapeReplan_.inSight;
        const bool refound=signals.known&&inSight&&(!escapeReplan_.observed||!escapeReplan_.inSight);
        const bool live=compass_.policeCircle(index,stars,center,radius);
        if(live){escapeCenter_=center;escapeAnchored_=true;}
        else if(!escapeAnchored_||escapeStars_!=stars||refound){escapeCenter_=player;escapeAnchored_=true;}
        const bool estimated=radius==0;if(estimated)radius=policeescape::estimatedRadius(stars);
        escapeStars_=stars;const DWORD now=GetTickCount();
        const bool searching=signals.known?signals.flashing:NativeInvoke::Invoke<NATIVE_WANTED_STARS_ARE_FLASHING,b8>()!=0;
        if(const auto reason=escapeReplan_.observe(stars,searching,escapeCenter_,radius,inSight,travel)){
            escapePlanning_=false;escapeBest_={};escapeOptions_.clear();
            char text[160];std::snprintf(text,sizeof(text),"Police escape: recalc queued; reason=%s retaining=%u",reason,escapeShown_.points.empty()?0u:1u);event(text);
        }
        const auto police=compass_.knownPolice(player);
        if(escapePlanning_){
            // Only complete requests whose GPS endpoint matches. The buffer can
            // still contain the previous request for several ticks.
            const bool ready=now-escapeRequestedAt_>=200&&existingRoute&&policeescape::matches(native.points,chasePoint_);
            if(!ready&&now-escapeRequestedAt_<policeescape::routeWait(static_cast<unsigned>(escapeShown_.points.size())))return true;
            if(!ready){char text[180];std::snprintf(text,sizeof(text),"Police escape: GPS request timed out; elapsed=%lu slot=%d state=%d points=%u endpoint-error=%.1fm retaining=%u",now-escapeRequestedAt_,native.slot,native.state,static_cast<unsigned>(native.points.size()),policeescape::endpointError(native.points,chasePoint_),escapeShown_.points.empty()?0u:1u);event(text);}
            if(ready&&policeescape::exits(native.points,escapeCenter_,radius)&&policeescape::followsHeading(native.points,player,escapeReplan_.heading)){
                const float score=policeescape::pathScore(native.points,player,police);
                if(score<escapeBestScore_){escapeBestScore_=score;escapeBest_=native;escapeBestPoint_=chasePoint_;}
                if(policeescape::needsForwardRoute(escapeShown_.points,player,motionSpeed_)||!policeescape::exits(escapeShown_.points,escapeCenter_,radius)){
                    escapeShown_=native;
                    event("Police escape: usable replacement published immediately while candidate comparison continues");
                }
            }
            // A police sighting can move the search area while GPS computes.
            // Do not spend the whole candidate cycle on obsolete inside points.
            if(!policeescape::outside(chasePoint_,escapeCenter_,radius)){
                escapePlanning_=false;escapeReplan_.pending=true;chaseRefreshAt_=0;
                event("Police escape: search boundary moved; restarting obsolete requests");return true;
            }
            ++escapeOption_;
            if(escapeOption_<escapeOptions_.size()){
                if(requestEscape(escapeOptions_[escapeOption_].point))return true;
            }
            escapePlanning_=false;chaseRefreshAt_=now;
            if(policeescape::exits(escapeBest_.points,escapeCenter_,radius)){
                escapeReplan_.committed();escapeShown_=escapeBest_;escapeCommittedRisk_=policeescape::pathRisk(escapeShown_.points,player,police);
                if(roadnav::distance2D(chasePoint_,escapeBestPoint_)>10&&!requestEscape(escapeBestPoint_)){
                    escapeReplan_.failed(now);event("Police escape: committed helper unavailable; retry backed off, last route retained");
                }
                char text[160];std::snprintf(text,sizeof(text),"Police escape: route committed; known cops=%u score=%.1f outside-margin=%.1fm",static_cast<unsigned>(police.size()),escapeBestScore_,roadnav::distance2D(escapeShown_.points.back(),escapeCenter_)-radius);event(text);
            }else {
                escapeReplan_.failed(now);
                event("Police escape: all candidates failed; retry backed off, last route retained");
            }
            return true;
        }
        if(!escapeReplan_.pending&&escapeActive_&&existingRoute&&policeescape::matches(native.points,chasePoint_)&&policeescape::exits(native.points,escapeCenter_,radius)&&!policeescape::onPath(escapeShown_.points,player))escapeShown_=native;
        const bool helperValid=chaseRoute_.IsValid()&&DoesBlipExist(chaseRoute_);
        const bool validTarget=escapeActive_&&helperValid&&policeescape::exits(escapeShown_.points,escapeCenter_,radius)&&policeescape::remaining(escapeShown_.points,player)>policeescape::refreshLead(motionSpeed_);
        const bool onRoute=policeescape::onPath(escapeShown_.points,player);
        if(!escapeReplan_.pending&&!policeescape::replanDue(validTarget,onRoute,now-chaseRefreshAt_,escapeCommittedRisk_,policeescape::pathRisk(escapeShown_.points,player,police)))return true;
        if(!escapeReplan_.ready(now))return true;
        const bool pursuitReset=escapeReplan_.pending;
        if(!pursuitReset)escapeReplan_.heading=travel;
        const float headingLength=std::hypot(escapeReplan_.heading.x,escapeReplan_.heading.y);
        if(!std::isfinite(headingLength)||headingLength<.5f){escapeReplan_.failed(now);event("Police escape: heading unavailable; retaining route");return true;}
        escapeReplan_.begin(now);
        event(validTarget?(onRoute?"Police escape: recalc attempted; refresh or pursuit change":"Police escape: recalc attempted; left committed path"):"Police escape: recalc attempted; destination invalid or running out");
        const float heading=std::atan2(escapeReplan_.heading.y,escapeReplan_.heading.x);
        const auto snap=[](roadnav::Vec3 sample,roadnav::Vec3& node){return GetClosestCarNode(sample.x,sample.y,sample.z,&node.x,&node.y,&node.z)!=0;};
        const auto cost=[](roadnav::Vec3 a,roadnav::Vec3 b){return NativeInvoke::Invoke<NATIVE_CALCULATE_TRAVEL_DISTANCE_BETWEEN_NODES,float>(a.x,a.y,a.z,b.x,b.y,b.z);};
        escapeOptions_=policeescape::candidates(player,escapeCenter_,radius,heading,snap,cost,police,true);
        if(escapeOptions_.size()>4)escapeOptions_.resize(4);
        if(escapeOptions_.empty()){escapeReplan_.failed(now);event("Police escape: no replacement reachable; retry backed off, keeping displayed route");return true;}
        // A small hysteresis credit keeps an adequate current route stable.
        escapeBest_={};escapeBestScore_=std::numeric_limits<float>::max();
        if(!pursuitReset&&validTarget&&onRoute&&!escapeShown_.points.empty()&&policeescape::followsHeading(escapeShown_.points,player,escapeReplan_.heading)){
            escapeBest_=escapeShown_;escapeBestPoint_=escapeShown_.points.back();
            escapeBestScore_=std::max(0.0f,policeescape::pathScore(escapeShown_.points,player,police)*.82f-40);
        }
        escapeOption_=0;escapePlanning_=requestEscape(escapeOptions_.front().point);
        if(!escapePlanning_){escapeReplan_.failed(now);event("Police escape: helper request failed; retry backed off, last route retained");}
        char text[180];std::snprintf(text,sizeof(text),"Police escape: background planning; known cops=%u radius=%.1f (%s) retaining=%u",static_cast<unsigned>(police.size()),radius,estimated?"estimated":"game table",escapeShown_.points.empty()?0u:1u);event(text);
        return true;
    }
    void updateChase(Scripting::Ped ped,roadnav::Vec3 player,bool waypoint,bool existingRoute,const roadnav::Vec3* endpoint){
        auto mission=compass_.navigationMissionTarget();
        const bool owns=chaseRoute_.IsValid()&&Scripting::DoesBlipExist(chaseRoute_);
        const DWORD tick=GetTickCount();
        if(existingRoute||waypoint)noRouteSince_=0;else if(!noRouteSince_)noRouteSince_=tick;
        if(waypoint){if(owns){clearChase();auto manual=compass_.navigationWaypoint();if(manual.IsValid())Scripting::SetRoute(manual,true);}return;}
        const bool movingMission=mission.IsValid()&&(Scripting::GetBlipInfoIdType(mission)==Scripting::BLIP_TYPE_CAR||Scripting::GetBlipInfoIdType(mission)==Scripting::BLIP_TYPE_CHAR);
        if(mission.IsValid()&&!movingMission){
            roadnav::Vec3 missionPoint{};
            if(!compass_.navigationTargetPosition(mission,missionPoint))return;
            // Restore the mission-owned route itself. A coordinate helper can
            // replace native GPS ownership and must never stand in for it.
            bool restored=false;if(owns){clearChase();restored=true;}
            const bool matches=existingRoute&&endpoint&&roadnav::distance2D(*endpoint,missionPoint)<65;
            if(matches&&!restored){missionRetryTarget_=0;return;}
            if(!restored&&(compass_.missionRouteAge(mission.Get())<12000||(missionRetryTarget_==mission.Get()&&!awareness::routeRequestDue(tick,missionRetryAt_))))return;
            Scripting::SetRoute(mission,true);missionRetryAt_=tick;missionRetryTarget_=mission.Get();
            event("Mission guidance: requested GPS on original mission marker; no helper created");
            return;
        }
        missionRetryTarget_=0;
        // A residual native GPS buffer is not an authoritative destination.
        // Manual waypoints and live fixed mission goals were handled above.
        const bool chaseEnabled=compass::chaseEnabled(chaseEnabled_,qolclient::Enabled(QOL_GPS_LINE,true),qolclient::Enabled(QOL_LINE_ENEMY,true));
        if(!compass::allowChase(chaseEnabled||mission.IsValid(),waypoint,owns,mission.IsValid()&&!movingMission)){clearChase();return;}
        auto target=movingMission?mission:compass_.chaseTarget(player,ped,chaseTarget_);
        if(!mission.IsValid()&&!chaseEnabled){clearChase();return;}
        guidanceEnemy_=true;
        if(!target.IsValid()){clearChase();return;}
        guidanceEnemy_=compass_.navigationTargetEnemy(target);
        roadnav::Vec3 targetPoint{};if(!compass_.navigationChasePosition(target,targetPoint)){clearChase();return;}
        const DWORD now=GetTickCount();
        if(owns&&target.Get()==chaseTarget_){
            if(now-chaseRefreshAt_<200||roadnav::distance2D(targetPoint,chasePoint_)<1)return;
            EscapeGps api{compass_};
            if(api.move(chaseRoute_,targetPoint)){
                Scripting::SetRoute(chaseRoute_,true);chasePoint_=targetPoint;chaseRefreshAt_=now;return;
            }
            // Older hosts without SET_BLIP_COORDINATES use a replacement helper.
            // The last fitted line/recent route remain visible during GPS refresh.
        }
        clearChase();
        using namespace Scripting;
        // Coordinate blips have separate ownership; never request an entity blip
        // that the engine might reuse from the mission's original marker.
        AddBlipForCoord(targetPoint.x,targetPoint.y,targetPoint.z,&chaseRoute_);
        if(chaseRoute_.Get()==target.Get()){chaseRoute_={};event("Chase guidance: refused reused mission blip");return;}
        if(chaseRoute_.IsValid()&&DoesBlipExist(chaseRoute_)){
            compass_.setRouteHelper(chaseRoute_.Get());ChangeBlipAlpha(chaseRoute_,255);ChangeBlipColour(chaseRoute_,guidanceEnemy_?9:5);ChangeBlipNameFromAscii(chaseRoute_,"Chase route");SetRoute(chaseRoute_,true);chaseTarget_=target.Get();chasePoint_=targetPoint;chaseRefreshAt_=now;
            char line[128];std::snprintf(line,sizeof(line),"Automatic guidance: %s blip=%u route=%u",guidanceEnemy_?"enemy":"mission",chaseTarget_,chaseRoute_.Get());event(line);
        }
    }
    awareness::RouteKind loggedKind_=awareness::RouteKind::Blue;int loggedColours_=-1;
    DWORD forkLogAt_=0;unsigned loggedLineTypes_=~0u;
    std::vector<roadnav::Vec3> forkHints(const std::vector<roadnav::Vec3>& points,roadnav::Vec3 player,float speed){
        using namespace roadnav;std::vector<Vec3> hints;if(points.size()<3)return hints;
        const float anchor=nearestDistanceAlong(points,player),end=routeLength2D(points);float along=0;unsigned checked=0,nativeNodes=0,rejoined=0,crossings=0;
        for(std::size_t i=1;i+1<points.size();++i){
            along+=distance2D(points[i-1],points[i]);if(along-anchor<0)continue;if(along-anchor>turnLookAhead(speed))break;
            const auto before=sampleAt(points,std::max(0.0f,along-12)).position,after=sampleAt(points,std::min(end,along+12)).position;
            Vec3 in{points[i].x-before.x,points[i].y-before.y,0},out{after.x-points[i].x,after.y-points[i].y,0};
            const float length=std::hypot(in.x,in.y);if(length<2||std::hypot(out.x,out.y)<2)continue;
            const float angle=std::fabs(std::atan2(in.x*out.y-in.y*out.x,in.x*out.x+in.y*out.y)*57.2957795f);
            if(angle<.75f||angle>=40)continue;
            if(!persistentFork(points,along,before)){++rejoined;continue;}
            if(++checked>6)break;
            bool intersection=false;
            for(const float side:{-1.0f,1.0f}){
                const Vec3 probe{points[i].x+side*in.y/length*22,points[i].y-side*in.x/length*22,points[i].z};
                for(unsigned n=1;n<=3;++n){Vec3 node{};float heading=0;
                    if(!Scripting::GetNthClosestCarNodeWithHeading(probe.x,probe.y,probe.z,n,&node.x,&node.y,&node.z,&heading))continue;
                    ++nativeNodes;
                    const auto onRoute=sampleAt(points,nearestDistanceAlong(points,node)).position;
                    if(distance2D(node,onRoute)<4&&std::fabs(node.z-onRoute.z)<3)continue;
                    if(crossStreetArm(before,points[i],node,heading)){intersection=true;break;}
                }
                if(intersection)break;
            }
            if(intersection){++crossings;continue;}
            // Different baselines prevent near toll lanes or densely sampled
            // road nodes from monopolizing a single closest-node query.
            bool accepted=false;
            for(const float horizon:{45.0f,80.0f,120.0f}){
                const Vec3 straight{points[i].x+in.x/length*horizon,points[i].y+in.y/length*horizon,points[i].z};
                for(unsigned n=1;n<=6;++n){Vec3 node{};float heading=0;
                    if(!Scripting::GetNthClosestCarNodeWithHeading(straight.x,straight.y,straight.z,n,&node.x,&node.y,&node.z,&heading))continue;
                    ++nativeNodes;
                    const float forward=((node.x-points[i].x)*in.x+(node.y-points[i].y)*in.y)/length;
                    const auto branch=sampleAt(points,std::clamp(along+forward,0.0f,end)).position;
                    if(forkContinuation(before,points[i],branch,node,heading)){hints.push_back(points[i]);accepted=true;break;}
                }
                if(accepted)break;
            }
        }
        const DWORD now=GetTickCount();if((checked||rejoined)&&(!forkLogAt_||now-forkLogAt_>=10000)){
            char line[180];std::snprintf(line,sizeof(line),"Fork scan: persistent=%u transient-lanes=%u intersections=%u road-nodes=%u accepted=%u",checked,rejoined,crossings,nativeNodes,static_cast<unsigned>(hints.size()));event(line);forkLogAt_=now;
        }
        return hints;
    }
    roadnav::Vec3 previousPlayer_{};
    DWORD motionAt_=0;
    float motionSpeed_=0;
    void resetMotion(){motionAt_=0;motionSpeed_=0;primaryWrongWay_.cancel();finishForwardRoute(GetTickCount());forwardRestoreAt_=0;}

    void reportEscapeDisplay(const char* reason,DWORD now){
        if(!reason){
            if(escapeSuppressed_)event("Police escape display: resumed; committed surface submitted independently of primary route");
            escapeSuppressed_=nullptr;return;
        }
        if(escapeSuppressed_&&!std::strcmp(escapeSuppressed_,reason)&&now-escapeSuppressedAt_<5000)return;
        escapeSuppressed_=reason;escapeSuppressedAt_=now;
        char text[230];std::snprintf(text,sizeof(text),"Police escape suppressed: reason=%s primary-points=%u escape-snapshot=%u surface=%u planning=%u helper=%u",reason,static_cast<unsigned>(line_.points.size()),static_cast<unsigned>(escapeShown_.points.size()),static_cast<unsigned>(escapeLine_.points.size()),escapePlanning_?1u:0u,chaseRoute_.Get());event(text);
    }
    void event(const char* message) {turnRendererEvent(message);}
    void hide(bool retainPrimary=false){if(!retainPrimary||!escapeReplan_.observed){recentRoute_={};recentRouteAt_=0;line_={};escapeLine_={};}resetMotion();cues_={};gTurnRenderer.clear();gRoadLineRenderer.clear();gEscapeLineRenderer.clear();if(wasVisible_)event("Turn cue hidden");wasVisible_=false;}
    void attachRenderer(DWORD now){
        if(attached_ || (lastServiceTry_ && now-lastServiceTry_<2000))return;
        lastServiceTry_=now;
        using Request=IService* (__cdecl*)(const char*);
        auto module=GetModuleHandleA("ScriptHook.dll");
        auto request=module?reinterpret_cast<Request>(GetProcAddress(module,"?RequestService@ScriptHookManager@@SAPAVIService@@PBD@Z")):nullptr;
        auto service=request?static_cast<ID3DHookService*>(request("D3DHook")):nullptr;
        if(!service){event("D3DHook service unavailable; turn renderer will retry");return;}
        // Reacquire the live service after a save load; remove a prior registration
        // before adding so the same renderer cannot be registered twice.
        service->RemoveDeviceHook(&gTurnRenderer);
        service->AddDeviceHook(&gTurnRenderer);
        service->RemoveDeviceHook(&gRoadLineRenderer);service->AddDeviceHook(&gRoadLineRenderer);
        service->RemoveDeviceHook(&gEscapeLineRenderer);service->AddDeviceHook(&gEscapeLineRenderer);
        service->RemoveDeviceHook(&gCompassNoticeRenderer);service->AddDeviceHook(&gCompassNoticeRenderer);
        attached_=true;event("Turn chevron renderer attached to D3DHook");
    }
protected:
    void OnStart() override{
        const auto ini=executableFolder()+"GTAIV-QoL.ini";
        navigationDiagnostics=GetPrivateProfileIntA("Diagnostics","LogToFile",0,ini.c_str())!=0;
        loggedLineMode_=-1;
        viewBend_.strength=std::clamp(qolclient::Get(QOL_GPS_VIEW_BEND,100),0,100)/100.0f;
        viewBend_.maxDegrees=static_cast<float>(std::clamp(static_cast<int>(GetPrivateProfileIntA("RoadGPSLine","ViewBendMaxDegrees",75,ini.c_str())),0,85));
        viewBend_.response=std::clamp(static_cast<int>(GetPrivateProfileIntA("RoadGPSLine","ViewBendResponsePercent",100,ini.c_str())),25,400)/100.0f;
        {char settings[240];std::snprintf(settings,sizeof(settings),"Road GPS Line: view-bend strength=%d%% max-degrees=%d response=%d%% (%s; primary and escape ribbons, not turn cues)",
            static_cast<int>(std::lround(viewBend_.strength*100)),static_cast<int>(viewBend_.maxDegrees),static_cast<int>(std::lround(viewBend_.response*100)),
            viewBend_.strength>0&&viewBend_.maxDegrees>0?"enabled":"off; original banking");event(settings);}
        passivecrash::Install(executableFolder().c_str());
        compass_.start();
        compass_.configureTaxi(static_cast<float>(GetPrivateProfileIntA("Camera","TaxiRearFOV",70,(executableFolder()+"GTAIV-QoL.ini").c_str())));
        clearChase();chaseEnabled_=GetPrivateProfileIntA("RoadNav","ChaseGuidance",1,(executableFolder()+"GTAIV-QoL.ini").c_str())!=0;
        previousTick_=lastServiceTry_=lastCueLog_=0;primaryReplanned_=false;primaryWrongWay_={};
        tickLogged_=attached_=wasVisible_=haveArrival_=false;lastCueCount_=0;lastDestination_=false;hide();
        if(navigationDiagnostics){char text[180];std::snprintf(text,sizeof(text),"RoadNav | build=%s | compiled=%s %s",ROADNAV_BUILD_ID,__DATE__,__TIME__);event(text);}
    }
    void OnKill() override{finishForwardRoute(GetTickCount());clearChase();compass_.stop();hide();haveArrival_=false;attached_=false;event("ScriptHook script stopped");}
    void RunScript() override{
        using Dispatcher=void (__thiscall*)(ScriptThread*);
        auto module=GetModuleHandleA("ScriptHook.dll");
        auto dispatch=module?reinterpret_cast<Dispatcher>(GetProcAddress(module,"?RunScript@ScriptThread@@MAEXXZ")):nullptr;
        if(!dispatch){event("ScriptHook default RunScript export missing; turn script disabled");return;}
        event("Delegating to installed ScriptHook default RunScript dispatcher");dispatch(this);
    }
    void RunTick() override{
        const DWORD renderNow=GetTickCount();attachRenderer(renderNow);
        using namespace Scripting;
        compass_.tick();
        const bool arrows=qolclient::Enabled(QOL_ROADNAV),lineEnabled=qolclient::Enabled(QOL_GPS_LINE,true);
        viewBend_.strength=std::clamp(qolclient::Get(QOL_GPS_VIEW_BEND,100),0,100)/100.0f;
        if(!arrows){cues_={};gTurnRenderer.clear();}
        const int lineMode=lineEnabled?(viewBend_.strength>0?2:1):0;
        if(lineMode!=loggedLineMode_){char status[180];std::snprintf(status,sizeof(status),"Road GPS Line: pause-menu mode=%d (%s); shared primary/escape visibility gate=%s",lineMode,lineMode==2?"Twisted":lineMode==1?"Flat":"Off",lineEnabled?"enabled":"disabled");event(status);loggedLineMode_=lineMode;}
        if(!lineEnabled){primaryWrongWay_={};finishForwardRoute(GetTickCount());forwardRestoreAt_=0;line_={};escapeLine_={};gRoadLineRenderer.clear();gEscapeLineRenderer.clear();}
        if(!arrows&&!lineEnabled){if(escapeReplan_.observed)event("Police escape: dropped; navigation disabled by user");clearChase();hide();haveArrival_=false;previousTick_=0;return;}
        const DWORD now=GetTickCount();
        if(!tickLogged_){event("ScriptHook RunTick entered");tickLogged_=true;}
        attachRenderer(now);
        if(Scripting::IsPauseMenuActive() || !Scripting::IsScreenFadedIn()){hide(true);return;}
        Scripting::Ped ped{};
        Scripting::GetPlayerChar(Scripting::ConvertIntToPlayerIndex(Scripting::GetPlayerId()),&ped);
        if(!ped.IsValid()){hide(true);return;}
        if(!Scripting::IsCharInAnyCar(ped)){
            roadnav::Vec3 position{};Scripting::GetCharCoordinates(ped,&position.x,&position.y,&position.z);
            if(arrivalArmed_&&GetTickCount()-arrivalSeen_<=5000&&roadnav::distance2D(position,arrivalPoint_)<12&&std::fabs(position.z-arrivalPoint_.z)<8){arrivalArmed_=false;compass_.notifyArrival(GetTickCount(),arrivalTarget_);}
            u32 stars=0;StoreWantedLevel(ConvertIntToPlayerIndex(GetPlayerId()),&stars);
            if(!stars){if(escapeReplan_.observed)event("Police escape: dropped; wanted level cleared on foot");clearChase();}
            hide(true);return;
        }
        const auto playerIndex=Scripting::ConvertIntToPlayerIndex(Scripting::GetPlayerId());
        const bool cutscene=NativeInvoke::Invoke<Scripting::NATIVE_HAS_CUTSCENE_LOADED,b8>()&&!NativeInvoke::Invoke<Scripting::NATIVE_HAS_CUTSCENE_FINISHED,b8>();
        if(cutscene||!Scripting::IsPlayerControlOn(playerIndex)){hide(true);return;}
        roadnav::Vec3 player{};Scripting::GetCharCoordinates(ped,&player.x,&player.y,&player.z);
        if(!previousTick_ || now-previousTick_>=200){
            previousTick_=now;
            auto module=reinterpret_cast<std::uintptr_t>(GetModuleHandleA(nullptr));
            const auto delta=static_cast<std::int64_t>(module)-static_cast<std::int64_t>(preferredBase);
            if(!supportedVersion(delta)){haveArrival_=false;hide();return;}
            auto blip=compass_.navigationWaypoint();
            const bool waypoint=blip.IsValid() && Scripting::DoesBlipExist(blip);
            Scripting::Vector3 waypointPosition{};
            if(waypoint)Scripting::GetBlipCoords(blip,&waypointPosition);
            const roadnav::Vec3 waypointPoint{waypointPosition.X,waypointPosition.Y,waypointPosition.Z};
            if(!waypoint)haveArrival_=false;
            else if(haveArrival_&&roadnav::distance2D(waypointPoint,cachedWaypoint_)>2.0f){haveArrival_=false;recentRoute_={};recentRouteAt_=0;}
            roadnav::Vec3 velocity{};
            const DWORD elapsed=motionAt_?now-motionAt_:0;
            if(elapsed && elapsed<=1000 && roadnav::distance2D(player,previousPlayer_)<30){
                const float seconds=static_cast<float>(elapsed)*.001f;
                velocity={(player.x-previousPlayer_.x)/seconds,(player.y-previousPlayer_.y)/seconds,0};
            }
            motionSpeed_=std::hypot(velocity.x,velocity.y);
            previousPlayer_=player;motionAt_=now;
            auto missionDestination=compass_.navigationMissionTarget();
            auto primary=waypoint?blip:missionDestination;
            roadnav::Vec3 primaryPoint{};
            const bool primaryLocated=primary.IsValid()&&compass_.navigationTargetPosition(primary,primaryPoint);
            const bool primaryChanged=primary.Get()!=primaryRouteBlip_.Get();
            if(primaryChanged)finishForwardRoute(now);
            primaryRouteBlip_=primary;
            if(primaryChanged){forwardRestoreAt_=0;primaryWrongWay_={};primaryReplanned_=false;
                recentRoute_={};recentRouteAt_=0;line_={};
                if(primaryLocated){
                    // Let the new player/mission request acquire GPS first.
                    // The already committed escape route remains on screen.
                    if(escapeActive_){escapePlanning_=false;escapeReplan_.pending=true;event("Police escape: recalc queued; primary GPS changed, last route retained");}
                    primaryWarmAt_=now;if(escapeActive_)restorePrimaryGps();else SetRoute(primary,true);
                }else primaryWarmAt_=0;
            }
            Route route;
            const bool readPrimary=readRoute(delta,route,primaryLocated?&primaryPoint:chaseTarget_&&!escapeActive_?&chasePoint_:nullptr,primaryReplanned_&&!escapeActive_?&player:nullptr);
            bool freshRoute=readPrimary&&!(primaryChanged&&!primaryLocated)&&(!escapeActive_||primaryLocated)&&
                (!chaseTarget_||escapeActive_||primaryLocated||roadnav::routeDestinationMatches(route.points,chasePoint_))&&
                (!primaryLocated||(!route.points.empty()&&roadnav::distance2D(route.points.back(),primaryPoint)<=65));
            // An ahead-leg job borrows the buffer, never the primary snapshot
            // used by turn cues or restoration, even for nearby endpoints.
            if(forwardRouteHelper_.IsValid()||(forwardRestoreAt_&&
               (now-forwardRestoreAt_<200||!roadnav::primaryOriginMatches(route.points,player))))freshRoute=false;
            if(freshRoute&&primaryWarmAt_)primaryWarmAt_=0;
            unsigned routeTarget=primaryLocated?primary.Get():(escapeActive_?0:chaseTarget_);
            // Capture the primary path before the escape planner borrows GTA's
            // single GPS buffer. Never accept a candidate as the primary path.
            if(freshRoute){recentRoute_=route;recentRouteAt_=now;recentTarget_=routeTarget;}
            u32 replanStars=0;StoreWantedLevel(playerIndex,&replanStars);
            Vehicle replanVehicle{};GetCarCharIsUsing(ped,&replanVehicle);
            const bool haveReplanVehicle=replanVehicle.IsValid()&&DoesVehicleExist(replanVehicle);
            float headingDegrees=0;if(haveReplanVehicle)GetCarHeading(replanVehicle,&headingDegrees);
            const float headingRadians=(headingDegrees+90)*.01745329252f;
            const roadnav::Vec3 travelHeading{std::cos(headingRadians),std::sin(headingRadians),0};
            updatePrimaryReplan(now,lineEnabled&&haveReplanVehicle&&primaryLocated&&(waypoint||GetBlipInfoIdType(primary)==BLIP_TYPE_COORD)&&!escapeActive_&&!replanStars,
                                player,travelHeading,velocity,primaryPoint,delta);
            if(forwardRestoreAt_&&freshRoute&&now-forwardRestoreAt_>=200){forwardRestoreAt_=0;forwardRoute_={};}
            Route escapeNative;
            const bool existingEscape=readRoute(delta,escapeNative,escapeActive_?&chasePoint_:nullptr);
            const bool wasPlanning=escapePlanning_;
            if(!updateEscape(ped,player,existingEscape,escapeNative,haveReplanVehicle?(motionSpeed_>=2.5f?velocity:travelHeading):roadnav::Vec3{})&&!forwardRouteHelper_.IsValid())
                updateChase(ped,player,waypoint,readPrimary,route.points.empty()?nullptr:&route.points.back());
            if(escapeActive_&&!escapePlanning_&&primaryLocated&&(wasPlanning||primaryChanged))restorePrimaryGps();
            if(chaseTarget_&&!escapeActive_&&!primaryLocated&&routeTarget!=chaseTarget_){
                // The old unowned GPS buffer cannot become the new enemy route.
                freshRoute=false;routeTarget=chaseTarget_;recentRoute_={};recentRouteAt_=0;line_={};
            }
            // An escape candidate must never displace either displayed route.
            // Keep the primary path throughout planning, without its normal
            // two-second cache expiry; target changes still invalidate it.
            bool routeAvailable=freshRoute;
            if(!freshRoute&&routeTarget==recentTarget_&&!recentRoute_.points.empty()&&
               (escapeActive_||primaryWrongWay_.retaining(now)||forwardRouteHelper_.IsValid()||forwardRestoreAt_||roadnav::keepRoadLineRoute(recentRoute_.points,player,chaseTarget_&&!escapeActive_?0:now-recentRouteAt_))){
                route=recentRoute_;routeAvailable=true;
            }
            if(lineEnabled&&now-lastCueLog_>5000){char status[180];std::snprintf(status,sizeof(status),"Road line routes: primary=%u points=%u escape=%u planning=%u escape-points=%u",routeTarget,static_cast<unsigned>(route.points.size()),escapeActive_?1u:0u,escapePlanning_?1u:0u,static_cast<unsigned>(escapeShown_.points.size()));event(status);}
            auto arrivalBlip=waypoint?blip:missionDestination;
            roadnav::Vec3 destination{};
            const bool fixedDestination=arrivalBlip.IsValid()&&(waypoint||Scripting::GetBlipInfoIdType(arrivalBlip)==Scripting::BLIP_TYPE_COORD)&&compass_.navigationTargetPosition(arrivalBlip,destination);
            if(fixedDestination){
                if(arrivalTarget_!=arrivalBlip.Get()||roadnav::distance2D(destination,arrivalPoint_)>5){arrivalTarget_=arrivalBlip.Get();arrivalArmed_=true;}
                arrivalPoint_=destination;arrivalSeen_=now;
            }
            // Keep the last endpoint briefly if the script removes its marker
            // just before arrival. Check the reachable road end as well as the pin.
            if(arrivalArmed_&&now-arrivalSeen_<=5000){
                const bool nearPin=roadnav::distance2D(player,arrivalPoint_)<12&&std::fabs(player.z-arrivalPoint_.z)<8;
                const auto& lastPath=routeAvailable?route.points:recentRoute_.points;
                const bool nearRoadEnd=!lastPath.empty()&&roadnav::distance2D(lastPath.back(),arrivalPoint_)<50&&roadnav::distance2D(player,lastPath.back())<12&&std::fabs(player.z-lastPath.back().z)<5;
                if(nearPin||nearRoadEnd){arrivalArmed_=false;compass_.notifyArrival(now,arrivalTarget_);event("Road line: destination reached");}
            }
            if(routeAvailable){
                if(arrows&&(freshRoute||forwardRouteHelper_.IsValid()||forwardRestoreAt_))cues_=roadnav::routeCues(route.points,player,motionSpeed_,forkHints(route.points,player,motionSpeed_));else cues_={};
                if(lineEnabled){
                    const bool tracked=chaseTarget_&&chaseRoute_.IsValid()&&!escapeActive_&&!waypoint;
                    auto nextLine=roadnav::roadLine(forwardRoute_.points.size()>1?forwardRoute_.points:route.points,player,motionSpeed_,tracked);
                    const auto probe=[](float x,float y,float z){float ground=-10000;
                        NativeInvoke::Invoke<Scripting::NATIVE_GET_GROUND_Z_FOR_3D_COORD,Scripting::ScriptVoid>(x,y,z,&ground);return ground;};
                    const auto stats=roadnav::fitRoadSurface(nextLine,player,line_,probe);
                    roadnav::stabilizeRoadSurface(nextLine,line_);
                    roadnav::Vec3 exact{};
                    if(primaryLocated)roadnav::pinDestination(nextLine,primaryPoint);
                    else if(tracked&&compass_.navigationChasePosition(chaseTarget_,exact))roadnav::pinDestination(nextLine,exact);
                    if(now-lastCueLog_>5000){char status[160];std::snprintf(status,sizeof(status),"Road surface: sampled=%u reused=%u missing=%u points=%u",stats.sampled,stats.reused,stats.missing,static_cast<unsigned>(nextLine.points.size()));event(status);}
                    if(nextLine.visible()||(!tracked&&!primaryWrongWay_.retaining(now)&&!forwardRouteHelper_.IsValid()&&!forwardRestoreAt_))line_=std::move(nextLine);
                }
                if(!route.points.empty())routeKind_=awareness::activeRoute(compass_.routeKind(route.points.back()),chaseRoute_.IsValid()&&guidanceEnemy_,waypoint);
                if(waypoint && !route.points.empty()){
                    cachedEnd_=route.points.back();cachedWaypoint_=waypointPoint;cachedAt_=now;haveArrival_=true;
                }
            }else{
                line_={};
                cues_={};
                // GTA can clear its route buffer shortly before arrival. Retain
                // the last road endpoint briefly only for the same live waypoint.
                if(arrows && haveArrival_ && waypoint && now-cachedAt_<=8000 && roadnav::distance2D(player,cachedEnd_)<=30)
                    cues_.destination={true,cachedEnd_,roadnav::distance2D(player,cachedEnd_)};
            }

            if(lineEnabled&&escapeReplan_.observed&&escapeShown_.points.size()>1){
                auto nextEscape=roadnav::roadLine(escapeShown_.points,player,motionSpeed_);
                const auto probe=[](float x,float y,float z){float ground=-10000;
                    NativeInvoke::Invoke<Scripting::NATIVE_GET_GROUND_Z_FOR_3D_COORD,Scripting::ScriptVoid>(x,y,z,&ground);return ground;};
                const auto surface=roadnav::fitRoadSurface(nextEscape,player,escapeLine_,probe,&line_);
                roadnav::stabilizeRoadSurface(nextEscape,escapeLine_);
                if(escapePlanning_)nextEscape.endsAtDestination=false;
                // A brief collision-streaming miss must not erase the entire
                // previously sampled road. Keep the last good surface during retries,
                // even after the player leaves its path or reaches its endpoint.
                const bool retainSurface=!nextEscape.visible()&&escapeLine_.visible()&&
                    escapeShown_.points.size()>1;
                if(!retainSurface)escapeLine_=std::move(nextEscape);
                if(now-lastCueLog_>5000){char status[200];std::snprintf(status,sizeof(status),"Police escape surface: sampled=%u reused=%u missing=%u points=%u retained=%u remaining=%.1fm",surface.sampled,surface.reused,surface.missing,static_cast<unsigned>(escapeLine_.points.size()),retainSurface?1u:0u,policeescape::remaining(escapeShown_.points,player));event(status);}
            }else{escapeLine_={};gEscapeLineRenderer.clear();}
            if(escapeReplan_.observed){
                const char* suppressed=!lineEnabled?"RoadGPSLine disabled":escapeShown_.points.size()<2?"no committed escape snapshot":!escapeLine_.visible()?"no validated escape surface":nullptr;
                reportEscapeDisplay(suppressed,now);
            }
            if(arrows&&escapeActive_&&!routeAvailable){
                cues_=roadnav::routeCues(escapeShown_.points,player,motionSpeed_,forkHints(escapeShown_.points,player,motionSpeed_));
                routeKind_=awareness::RouteKind::Escape;
            }

            const auto snapGround=[](roadnav::Vec3& position){
                float ground=position.z;
                Scripting::GetGroundZFor3DCoord(position.x,position.y,position.z+2,&ground);
                if(std::isfinite(ground)&&std::fabs(ground-position.z)<2)position.z=ground;
            };
            for(unsigned i=0;i<cues_.count;++i){
                auto& cue=cues_.turns[i];snapGround(cue.position);
                if(cue.followsRoute){
                    if(cue.fork){snapGround(cue.markers[0].position);for(auto& marker:cue.markers)marker.position.z=cue.markers[0].position.z;}
                    else for(auto& marker:cue.markers)snapGround(marker.position);
                }
            }
            if(cues_.destination.visible){
                roadnav::Vec3 exact{};
                if(primaryLocated)roadnav::pinDestination(cues_,primaryPoint);
                else if(chaseTarget_&&!escapeActive_&&compass_.navigationChasePosition(chaseTarget_,exact))roadnav::pinDestination(cues_,exact);
                else snapGround(cues_.destination.position);
            }
            if((!wasVisible_&&cues_.visible()) || cues_.count!=lastCueCount_ || cues_.destination.visible!=lastDestination_ || now-lastCueLog_>5000){
                char line[192];std::snprintf(line,sizeof(line),"Navigation cues: turns=%u destination=%s speed=%.1fm/s preview=%.1fm",cues_.count,cues_.destination.visible?"shown":"hidden",motionSpeed_,cues_.lookAhead);event(line);
                for(unsigned i=0;i<cues_.count;++i){
                    const auto& turn=cues_.turns[i];
                    std::snprintf(line,sizeof(line),"  turn[%u]: %s angle=%.1f ahead=%.1fm junction=%.1f %.1f %.1f",i,turn.fork?"fork":(turn.kind==roadnav::CueKind::UTurn?"U-turn":(turn.angleDegrees<0?"right":"left")),turn.angleDegrees,turn.distanceAhead,turn.position.x,turn.position.y,turn.position.z);event(line);
                }
                if(cues_.destination.visible){const auto& d=cues_.destination;std::snprintf(line,sizeof(line),"  destination: ahead=%.1fm endpoint=%.1f %.1f %.1f",d.distanceAhead,d.position.x,d.position.y,d.position.z);event(line);}
                lastCueLog_=now;
            }
            lastCueCount_=cues_.count;lastDestination_=cues_.destination.visible;wasVisible_=cues_.visible();
        }
        Scripting::Camera camera{};Scripting::GetRootCam(&camera);
        if(!camera.IsValid())Scripting::GetGameCamChild(&camera);
        if(!camera.IsValid()){hide();return;}
        roadnav::CameraFrame frame{};
        Scripting::GetCamPos(camera,&frame.position.x,&frame.position.y,&frame.position.z);
        Scripting::GetCamRot(camera,&frame.rotation.x,&frame.rotation.y,&frame.rotation.z);
        Scripting::GetCamFov(camera,&frame.fov);
        const bool colours=qolclient::Enabled(QOL_ROUTE_COLOURS);
        if(loggedKind_!=routeKind_||loggedColours_!=static_cast<int>(colours)){
            char line[120];std::snprintf(line,sizeof(line),"RoadNav route colour: type=%u destination-colours=%u",static_cast<unsigned>(routeKind_),colours?1u:0u);event(line);
            loggedKind_=routeKind_;loggedColours_=colours?1:0;
        }
        if(arrows)gTurnRenderer.submit({cues_,frame,now,player,motionSpeed_,routeKind_,colours});
        if(lineEnabled){float nearClip=.15f,farClip=1500;
            NativeInvoke::Invoke<Scripting::NATIVE_GET_CAM_NEAR_CLIP,Scripting::ScriptVoid>(camera,&nearClip);
            NativeInvoke::Invoke<Scripting::NATIVE_GET_CAM_FAR_CLIP,Scripting::ScriptVoid>(camera,&farClip);
            if(!std::isfinite(nearClip)||nearClip<.01f||nearClip>5)nearClip=.15f;
            if(!std::isfinite(farClip)||farClip<=nearClip||farClip>20000)farClip=1500;
            const awareness::LineTypes types{qolclient::Enabled(QOL_LINE_ESCAPE,true),qolclient::Enabled(QOL_LINE_MISSION,true),
                qolclient::Enabled(QOL_LINE_FRIEND,true),qolclient::Enabled(QOL_LINE_ENEMY,true),qolclient::Enabled(QOL_LINE_CUSTOM,true)};
            const unsigned typeMask=(types.escape?1u:0u)|(types.mission?2u:0u)|(types.friendLine?4u:0u)|(types.enemy?8u:0u)|(types.custom?16u:0u);
            if(typeMask!=loggedLineTypes_){char state[160];std::snprintf(state,sizeof(state),"Road line type switches: escape=%u mission=%u friend=%u enemy=%u custom=%u",types.escape?1u:0u,types.mission?1u:0u,types.friendLine?1u:0u,types.enemy?1u:0u,types.custom?1u:0u);event(state);loggedLineTypes_=typeMask;}
            const bool primaryShown=types.visible(lineEnabled,routeKind_);
            auto displayedLine=line_;
            if(chaseTarget_&&!escapeActive_){roadnav::Vec3 live{};
                if(compass_.navigationChasePosition(chaseTarget_,live)){
                    roadnav::trackLineEndpoint(displayedLine,live);roadnav::pinDestination(displayedLine,live);
                }
            }
            if(primaryShown)gRoadLineRenderer.submit({displayedLine,frame,now,player,routeKind_,colours,nearClip,farClip,1,viewBend_});else gRoadLineRenderer.clear();
            // A narrower red/blue escape ribbon leaves the primary route visible
            // on shared roads, then separates naturally at the escape turn.
            if(types.visible(lineEnabled,awareness::RouteKind::Escape))gEscapeLineRenderer.submit({escapeLine_,frame,now,player,awareness::RouteKind::Escape,true,nearClip,farClip,!primaryShown||line_.points.empty()?1.0f:.48f,viewBend_});else gEscapeLineRenderer.clear();
        }
    }
};

static RoadNavScript gRoadNavScript;

} // namespace

// Bundle adaptation: the companion ASI owns initialization and lifetime.
// No second RoadNav DLL is loaded; the installer backs up standalone copies.
extern "C" void InitializeBundledRoadNav(HMODULE module) {
    static bool initialized=false;if(initialized)return;initialized=true;
    using Register=void (__cdecl*)(ScriptThread*,HMODULE);
    auto hook=GetModuleHandleA("ScriptHook.dll");
    auto reg=hook?reinterpret_cast<Register>(GetProcAddress(hook,"?RegisterThread@ScriptHookManager@@SAXPAVScriptThread@@PAUHINSTANCE__@@@Z")):nullptr;
    if(!reg)return;
    reg(&gRoadNavScript,module);
    HANDLE thread=CreateThread(nullptr,0,worker,nullptr,0,nullptr);
    if(thread)CloseHandle(thread);
}
