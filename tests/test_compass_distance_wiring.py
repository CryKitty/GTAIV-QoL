"""Keep every actual compass distance drawing site on the shared miles formatter."""
from pathlib import Path
import unittest


class CompassDistanceWiring(unittest.TestCase):
    def test_all_readout_sites_use_miles(self):
        source = (Path(__file__).resolve().parents[1] / 'vendor/roadnav/src/compass_hud.hpp').read_text()
        self.assertIn('compass::waypointMiles(roadnav::distance2D(playerPoint,pinnedPoint))', source)
        self.assertIn('compass::waypointMiles(marker.distance)', source)
        self.assertNotIn('metric_', source)
        self.assertNotIn('3.2808399', source)
        self.assertNotIn('"ft"', source)
        self.assertNotIn('"m"', source)
