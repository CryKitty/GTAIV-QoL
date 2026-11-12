#pragma once
// Pure, host-independent pause request state machine.
// Hooking the game cutscene clock/audio is intentionally NOT implemented here.
namespace cutscenepause {
enum class Phase { Idle, Playing, Paused };
struct Transition {
    Phase phase = Phase::Idle;
    bool requestPause = false;
    bool requestResume = false;
    void Observe(bool inCutscene, bool togglePressed) {
        requestPause = requestResume = false;
        if (!inCutscene) { phase = Phase::Idle; return; }
        if (phase == Phase::Idle) { phase = Phase::Playing; return; }
        if (!togglePressed) return;
        if (phase == Phase::Playing) { requestPause = true; phase = Phase::Paused; }
        else { requestResume = true; phase = Phase::Playing; }
    }
    void RejectTransition() {
        if (requestPause) phase = Phase::Playing;
        else if (requestResume) phase = Phase::Paused;
        requestPause = requestResume = false;
    }
};
}
