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
if __name__=='__main__':unittest.main()
