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

    def test_both_persistent_paths_reserve_full_hover_stack(self):
        source = (Path(__file__).resolve().parents[1] / 'vendor/roadnav/src/compass_hud.hpp').read_text()
        self.assertEqual(source.count('textStack.finishPersistent(y)'), 2)
        self.assertIn('const float hoverLabelY=textStack.hoverNameY()', source)
        self.assertIn('float distanceY=textStack.hoverDistanceY()', source)
        self.assertNotIn('distanceY=havePinned?', source)

    def test_native_visibility_is_not_bypassed_by_people_outing_or_selection(self):
        source = (Path(__file__).resolve().parents[1] / 'vendor/roadnav/src/compass_hud.hpp').read_text()
        self.assertIn('nativeRadarMarkerVisible(display,alpha,IsBlipShortRange(blip)!=0,distance,range,sprite)', source)
        self.assertNotIn('&&!outingVenue&&!person', source)
        self.assertNotIn('outingDisplay(display,outingVenue)', source)

    def test_production_road_paths_have_no_lateral_offset_stage(self):
        source = (Path(__file__).resolve().parents[1] / 'vendor/roadnav/src/roadnav.cpp').read_text()
        self.assertNotIn('offsetRoadLine(', source)
        self.assertNotIn('offsetDrivingSide(', source)
        self.assertNotIn('RoadLaneOffset', source)
