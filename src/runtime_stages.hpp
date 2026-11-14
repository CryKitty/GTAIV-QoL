#pragma once
#include <array>
namespace qolsafety {
// Fixed, public operation names only. Eight pairs per operation/session bound
// logging without losing the first vehicle entry / first GPS handoff evidence.
enum class Stage:unsigned {
    ChildBegin,ChildEnd,ChildExistsBegin,ChildExistsEnd,
    InterpolationBegin,InterpolationEnd,ScriptBegin,ScriptEnd,
    ScriptExistsBegin,ScriptExistsEnd,ScriptActiveBegin,ScriptActiveEnd,
    ScriptPropagateBegin,ScriptPropagateEnd,OffsetReadBegin,OffsetReadEnd,
    OffsetWriteBegin,OffsetWriteEnd,CandidatesBegin,CandidatesEnd,
    HandoffBegin,HandoffEnd,RouteOffBegin,RouteOffEnd,RouteOnBegin,RouteOnEnd,
    AddBegin,AddEnd,MoveBegin,MoveEnd,RemoveBegin,RemoveEnd,
    ConfigureBegin,ConfigureEnd,ProbesReady,ScriptClassifyBegin,ScriptClassifyEnd,Count
};
inline const char* stageName(unsigned stage){
    constexpr const char* names[]={"camera child begin","camera child finish","camera child exists begin","camera child exists finish",
        "camera specific interpolation begin","camera specific interpolation finish","camera script draw begin","camera script draw finish",
        "camera script exists begin","camera script exists finish","camera script active begin","camera script active finish",
        "camera script propagating begin","camera script propagating finish","camera offset read begin","camera offset read finish",
        "camera offset write begin","camera offset write finish","escape candidates begin","escape candidates finish",
        "escape handoff begin","escape handoff finish","GPS route off begin","GPS route off finish","GPS route on begin","GPS route on finish",
        "GPS add begin","GPS add finish","GPS move begin","GPS move finish","GPS remove begin","GPS remove finish",
        "GPS configure begin","GPS configure finish","camera probes ready","camera script handle classify begin","camera script handle classify finish"};
    static_assert(sizeof(names)/sizeof(names[0])==static_cast<unsigned>(Stage::Count));
    return stage<static_cast<unsigned>(Stage::Count)?names[stage]:nullptr;
}
struct StageBudget {
    std::array<unsigned,static_cast<unsigned>(Stage::Count)> counts{};
    bool claim(Stage stage){const auto n=static_cast<unsigned>(stage);if(n>=counts.size()||counts[n]>=8)return false;++counts[n];return true;}
    void reset(){counts={};}
};
}
