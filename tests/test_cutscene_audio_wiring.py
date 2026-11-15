"""Protect runtime output evidence and teardown wiring, beyond pure gain math."""
from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[1]
class CutsceneAudioWiring(unittest.TestCase):
    def test_runtime_reconciles_and_reports_scene_edges(self):
        source=(ROOT/'src/cutscene_pause.cpp').read_text()
        tick=source.split('void RunTick() override{',1)[1]
        self.assertIn('const auto proof=cutsceneoutput::Apply(multiplier)',tick)
        self.assertIn('const auto ds=directaudio::ApplyVolume()',tick)
        self.assertIn('"engaged"',tick)
        self.assertIn('"restored"',tick)
        self.assertIn('ds-headroom-limited=%u',tick)
        stop=source.split('void OnKill() override{',1)[1].split('void RunScript()',1)[0]
        self.assertIn('cutsceneoutput::Apply(1)',stop)
        self.assertIn('event=restored reason=script-stop',stop)
        output=(ROOT/'src/cutscene_output.hpp').read_text()
        self.assertNotIn('if(previous==multiplier)return',output)
        self.assertIn('property-only (not audibility proof)',output)
        pcm=(ROOT/'src/directsound_pcm.hpp').read_text()
        for field in ['input-peak=', 'output-peak=', 'input-rms=', 'output-rms=', 'cursor-advanced=', 'lock-hr=', 'unlock-hr=']:
            self.assertIn(field,pcm)
        self.assertIn('return false;',pcm)
        self.assertIn('raw-after=',output)
if __name__=='__main__':unittest.main()
