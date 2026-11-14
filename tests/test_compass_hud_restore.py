"""Protect the narrow HUD-mask boundary; Windows tests exercise native calls."""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]

class CompassHudRestore(unittest.TestCase):
    def test_native_lifetime_and_visibility_callbacks_are_not_hooked(self):
        layout = (ROOT / 'vendor/roadnav/src/minimap_layout.hpp').read_text()
        ring = (ROOT / 'vendor/roadnav/src/minimap_ring.hpp').read_text()
        self.assertNotIn('MH_CreateHook', layout)
        self.assertNotIn('radarfill::resolve', ring)
        self.assertIn('resolved{{frames[0][0],frames[1][0],0}}', ring)
        self.assertIn('if(installed_)recoverCurrent();', layout)
        # Preserve the observer's native forwarding contract; no synthetic help
        # re-print/clear is a substitute for allowing native cleanup to run.
        observer = (ROOT / 'vendor/roadnav/src/phone_notifications.hpp').read_text()
        self.assertNotIn('PRINT_HELP', observer)
        self.assertNotIn('CLEAR_HELP', observer)

if __name__ == '__main__':
    unittest.main()
