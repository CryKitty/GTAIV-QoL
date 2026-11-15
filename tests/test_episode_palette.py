from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[1]
class EpisodePalette(unittest.TestCase):
    def test_episode_detection_is_inside_world_initialization(self):
        code=(ROOT/'vendor/roadnav/src/roadnav.cpp').read_text()
        init=code.split('if(!worldGate_.allow(world,[&]{',1)[1].split('}))return;',1)[0]
        self.assertIn('Scripting::GetCurrentEpisode()',init)
        self.assertIn('Mission palette: episode=',init)
        for path in ['road_line_renderer.hpp','turn_renderer.hpp']:
            self.assertIn('awareness::routeColour(', (ROOT/'vendor/roadnav/src'/path).read_text())
if __name__=='__main__':unittest.main()
