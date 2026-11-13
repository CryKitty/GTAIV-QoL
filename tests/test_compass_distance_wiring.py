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

    def test_original_objective_uses_persistent_deduplication(self):
        source = (Path(__file__).resolve().parents[1] / 'vendor/roadnav/src/compass_hud.hpp').read_text()
        self.assertIn('pinnedObjective=objective;', source)
        self.assertIn('pinnedObjective&&marker.handle==missionTarget.Get()', source)
        self.assertIn('compass::hoverReadout(marker.handle,havePinned?(pinned.Get()==compassWaypoint_.Get()?compassWaypointSource_:pinned.Get()):0,centered,holding,', source)

    def test_compass_pin_keeps_source_icon_and_name(self):
        source = (Path(__file__).resolve().parents[1] / 'vendor/roadnav/src/compass_hud.hpp').read_text()
        self.assertIn('blip.Get()==compassWaypoint_.Get()||!DoesBlipExist(blip)', source)
        self.assertIn('const bool selected=blip.Get()==compassWaypointSource_', source)
        self.assertIn('auto label=awareness_.briefFor(labelTarget)', source)
        self.assertIn('(mid,y,"STRING",labelTarget)', source)
