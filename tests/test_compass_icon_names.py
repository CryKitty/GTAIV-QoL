from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[1]
class IconNames(unittest.TestCase):
    def test_production_resolution_and_draw_evidence(self):
        code=(ROOT/'vendor/roadnav/src/compass_hud.hpp').read_text()
        self.assertIn('compassicons::valid(text)',code)
        self.assertIn('compassicons::resolve(marker.sprite,names_[marker.sprite])',code)
        self.assertIn('GetTexture(dictionary_,iconName.c_str())',code)
        self.assertIn('Compass glyph draw: t=',code)
        self.assertIn('Draw::Pending',code)
if __name__=='__main__':unittest.main()
