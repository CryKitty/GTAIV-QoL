"""Protect the narrow HUD-mask boundary; Windows tests exercise native calls."""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]

class CompassHudRestore(unittest.TestCase):
    def test_native_lifetime_and_visibility_callbacks_are_not_hooked(self):
        layout = (ROOT / 'vendor/roadnav/src/minimap_layout.hpp').read_text()
        ring = (ROOT / 'vendor/roadnav/src/minimap_ring.hpp').read_text()
        self.assertNotIn('WriteProcessMemory', layout)
        self.assertIn('queried)!=self->component()', layout)
        self.assertNotIn('radarfill::resolve', ring)
        self.assertIn('resolved{{frames[0][0],frames[1][0],0}}', ring)
        self.assertIn('return native;', layout)
        self.assertIn('ring_.enable(frontend_,log,testImage)', layout)
        self.assertIn('ring_.release()', layout)
        self.assertIn('return ring_.selections()', layout)
        # Preserve the observer's native forwarding contract; no synthetic help
        # re-print/clear is a substitute for allowing native cleanup to run.
        observer = (ROOT / 'vendor/roadnav/src/phone_notifications.hpp').read_text()
        self.assertNotIn('PRINT_HELP', observer)
        self.assertNotIn('CLEAR_HELP', observer)

    def test_pause_setting_dispatch_does_not_wait_for_script_tick(self):
        settings = (ROOT / 'src/qol_settings.hpp').read_text()
        layout = (ROOT / 'vendor/roadnav/src/minimap_layout.hpp').read_text()
        bridge = (ROOT / 'vendor/roadnav/src/roadnav.cpp').read_text()
        self.assertIn('id==QOL_COMPASS', settings)
        self.assertIn('QoLCompass_SetEnabled', settings)
        self.assertIn('mask-after=%u', settings)
        self.assertIn('Compass mask transition: event=%lu', layout)
        self.assertIn('if(!enabled_)release();', layout)
        self.assertIn('if(!enabled_)return;', layout)
        exported = bridge.split('unsigned __cdecl QoLCompass_SetEnabled', 1)[1]
        self.assertNotIn('Scripting::', exported)
        self.assertIn('compassPreference(enabled!=0,event)', exported)

    def test_presentation_is_prepared_before_first_runtime_tick(self):
        code=(ROOT/'vendor/roadnav/src/roadnav.cpp').read_text()
        init=code.split('if(!worldGate_.allow(world,[&]{',1)[1].split('}))return;',1)[0]
        self.assertLess(init.index('preparePresentation()'),init.index('qolclient::LoadStage(2)'))
        layout=(ROOT/'vendor/roadnav/src/minimap_layout.hpp').read_text()
        self.assertIn('phase=mask-ready geometry-writes=0',layout)
        self.assertIn('phase=first-radar-query',layout)

if __name__ == '__main__':
    unittest.main()
