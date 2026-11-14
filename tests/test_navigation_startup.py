"""Guard the ordering that keeps native observers out of ASI load time."""
from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[1]
class NavigationStartupTests(unittest.TestCase):
    def test_loader_detection_avoids_stack_walk_under_dllmain(self):
        source=(ROOT/'tools/patch-fusionfix-menu.py').read_text()
        replacement=source.split("export bool IsUALPresent()",2)[-1].split("common_source[end:]",1)[0]
        self.assertIn('GetModuleHandleW(name)',replacement)
        self.assertIn('IsModuleUAL(module)',replacement)
        self.assertNotIn('stacktrace::current()',replacement)
        self.assertIn('dinput8.dll',replacement)
        self.assertIn('dsound.dll',replacement)
        self.assertIn('if (!IsSupportedGameProcess()) return;',source)

    def test_observers_wait_for_game_loop_but_not_player_creation(self):
        awareness=(ROOT/'vendor/roadnav/src/navigation_awareness.hpp').read_text()
        startup=awareness.split('void start(void (*log)(const char*)){',1)[1].split('}',1)[0]
        self.assertNotIn('MH_',startup)
        self.assertNotIn('GetNativeAddress',startup)
        hud=(ROOT/'vendor/roadnav/src/compass_hud.hpp').read_text().split('    void tick(){',1)[1]
        activation=hud.index('awareness_.activate()')
        self.assertLess(activation,hud.index('GetPlayerChar(player,&ped)'))
        self.assertLess(activation,hud.index('!IsScreenFadedIn()'))
        self.assertEqual(hud.count('awareness_.activate()'),1)
        source=(ROOT/'vendor/roadnav/src/compass_hud.hpp').read_text()
        camera=source.split('void drivingCameraHeightTick(bool stopping=false){',1)[1].split('void clearCompassWaypoint',1)[0]
        self.assertIn('resolveHeightNatives();',camera)
        start=source.split('    void start(){',1)[1].split('    void requestNavigationRoute',1)[0]
        self.assertNotIn('resolveHeightNatives();',start)
    def test_new_runtime_work_is_after_world_gate_and_startup_is_native_free(self):
        source=(ROOT/'vendor/roadnav/src/roadnav.cpp').read_text()
        start=source.split('    void OnStart() override{',1)[1].split('    void OnKill()',1)[0]
        self.assertNotIn('compass_.start()',start)
        self.assertNotIn('UpdateSession(true)',start)
        tick=source.split('    void RunTick() override{',1)[1]
        gate=tick.index('worldGate_.allow')
        self.assertLess(gate,tick.index('compass_.start()'))
        self.assertLess(gate,tick.index('attachRenderer(renderNow)'))
        self.assertLess(gate,tick.index('compass_.tick()'))
        bootstrap=tick[:gate]
        for forbidden in ('GetNativeAddress','resolveHeightNatives','prune(','GetScriptDrawCam','UpdateSession(true)'):
            self.assertNotIn(forbidden,bootstrap)
        hud=(ROOT/'vendor/roadnav/src/compass_hud.hpp').read_text()
        camera=hud.split('void drivingCameraHeightTick(bool stopping=false){',1)[1].split('void clearCompassWaypoint',1)[0]
        self.assertLess(camera.index('state.gate(1)==qolcamera::HeightGate::Ready'),camera.index('resolveHeightNatives();'))
        scheduler=(ROOT/'src/ingame_updates.hpp').read_text().split('inline void Tick(',1)[1]
        self.assertLess(scheduler.index('if(!worldReady.load())return'),scheduler.index('AcquireSRWLockExclusive'))

    def test_camera_entry_readiness_precedes_each_query_and_write(self):
        source=(ROOT/'vendor/roadnav/src/compass_hud.hpp').read_text()
        camera=source.split('void drivingCameraHeightTick(bool stopping=false){',1)[1].split('void clearCompassWaypoint',1)[0]
        self.assertLess(camera.index('heightEntry_.seated('),camera.index('GetGameCamChild('))
        self.assertLess(camera.index('DoesCamExist(gameplay)'),camera.index('heightEntry_.observed('))
        self.assertLess(camera.index('heightEntry_.observed('),camera.index('resolveHeightNatives();'))
        self.assertNotIn('IsCamInterpolating()',camera)
        self.assertIn('NATIVE_IS_SPECIFIC_CAM_INTERPOLATING',source)
        self.assertIn('cameraInterpolating<HeightQuery>(heightInterpolation_,gameplay)',camera)
        self.assertIn('qolcamera::cameraHandle(scripted.Get())',camera)
        self.assertIn('qolcamera::applySafeHeight',camera)
        for operation in ('Child','ChildExists','Interpolation','Script','ScriptExists','ScriptActive','ScriptPropagate','OffsetRead','OffsetWrite'):
            self.assertIn('Stage::'+operation+'Begin',camera)
            self.assertIn('Stage::'+operation+'End',camera)
        self.assertLess(camera.index('if(stopping)'),camera.index('GetPlayerChar('))

if __name__=='__main__':unittest.main()
