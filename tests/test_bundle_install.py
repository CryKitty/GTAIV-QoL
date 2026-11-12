import base64
import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest import mock
import zlib

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('bundle_helper',ROOT/'tools/steam-deck/enable-qol-logs.py')
helper=importlib.util.module_from_spec(spec);spec.loader.exec_module(helper)


def files():
    return {name:{'data':base64.b64encode(raw).decode(),'sha256':hashlib.sha256(raw).hexdigest()}
            for name,raw in {'GTAIV-QoL.asi':b'new main','GTAIV-QoL-Pause.asi':b'new combined pause and RoadNav',
                'GTAIV-QoL.ini':b'[DefaultMapZoom]\nEnabled = 1\nZoomPercent = 300\n[RoadNav]\nEnabled = 1\nDestinationColours = 1\n[VoiceVolume]\nEnabled = 1\nGameplayPercent = 100\nCutscenePercent = 150\n'}.items()}


def bridge():
    return {'data':base64.b64encode(zlib.compress(b'new ff menu bridge')).decode(),
            'sha256':hashlib.sha256(b'new ff menu bridge').hexdigest(),
            'supported_sha256':[hashlib.sha256(b'original ff').hexdigest()], 'notice':'GPL/source notice'}


class BundleInstallTests(unittest.TestCase):
    def prepare(self,root):
        game=Path(root);(game/'plugins').mkdir();(game/'scripts').mkdir()
        (game/'plugins/GTAIV.EFLC.FusionFix.asi').write_bytes(b'original ff')
        (game/'plugins/RoadNav.asi').write_bytes(b'standalone RoadNav')
        (game/'RoadNav.asi').write_bytes(b'duplicate standalone RoadNav')
        (game/'GTAIV-QoL.asi').write_bytes(b'old main')
        (game/'GTAIV-QoL.ini').write_bytes(b'[DefaultMapZoom]\nEnabled = 0\nZoomPercent = 300\n[RoadNav]\nEnabled = 0\nDestinationColours = 0\n[VoiceVolume]\nCutscenePercent = 225\n')
        return game

    def test_install_combines_roadnav_preserves_settings_and_updates_owned_bridge(self):
        with tempfile.TemporaryDirectory() as tmp, mock.patch.object(helper,'FUSIONFIX_BRIDGE',bridge()):
            game=self.prepare(tmp)
            helper.install_bundle(game,files())
            self.assertFalse((game/'RoadNav.asi').exists());self.assertFalse((game/'plugins/RoadNav.asi').exists())
            self.assertEqual((game/'plugins/GTAIV.EFLC.FusionFix.asi').read_bytes(),b'new ff menu bridge')
            ini=(game/'GTAIV-QoL.ini').read_text();self.assertIn('CutscenePercent = 100',ini);self.assertIn('[RoadNav]\nEnabled = 0',ini)
            self.assertIn('ZoomPercent = 300',ini);self.assertIn('DestinationColours = 0',ini)
            self.assertTrue(list((game/'plugins').glob('RoadNav.asi.before-qol-bundle-*')))
            helper.install_bundle(game,files()) # accepts the bridge installed by this bundle
            self.assertIn('CutscenePercent = 100',(game/'GTAIV-QoL.ini').read_text())

    def test_unsupported_fusionfix_changes_nothing(self):
        with tempfile.TemporaryDirectory() as tmp, mock.patch.object(helper,'FUSIONFIX_BRIDGE',bridge()):
            game=self.prepare(tmp);(game/'plugins/GTAIV.EFLC.FusionFix.asi').write_bytes(b'custom ff')
            with self.assertRaisesRegex(RuntimeError,'not supported'):helper.install_bundle(game,files())
            self.assertEqual((game/'GTAIV-QoL.asi').read_bytes(),b'old main')
            self.assertEqual((game/'plugins/RoadNav.asi').read_bytes(),b'standalone RoadNav')
            self.assertFalse(list(game.rglob('*.before-qol-bundle-*')))

    def test_failed_write_restores_fusionfix_roadnav_ini_and_mods(self):
        with tempfile.TemporaryDirectory() as tmp, mock.patch.object(helper,'FUSIONFIX_BRIDGE',bridge()):
            game=self.prepare(tmp);original={p:p.read_bytes() for p in game.rglob('*') if p.is_file()}
            replace=Path.replace
            def fail_at_bridge(path,target):
                if str(target).endswith('FusionFix.asi'):raise OSError('fixture write failure')
                return replace(path,target)
            with mock.patch.object(Path,'replace',fail_at_bridge):
                with self.assertRaisesRegex(RuntimeError,'were restored'):helper.install_bundle(game,files())
            for path,data in original.items():self.assertEqual(path.read_bytes(),data)
            self.assertFalse((game/'GTAIV-QoL-Pause.asi').exists())

    def test_gain_preferences_reject_bad_values_and_preserve_valid_zero(self):
        fresh=base64.b64decode(files()['GTAIV-QoL.ini']['data'])
        result=helper.merge_preferences(b'[VoiceVolume]\nGameplayPercent=0\nCutscenePercent=999\n',fresh).decode()
        self.assertIn('GameplayPercent = 0',result);self.assertIn('CutscenePercent = 100',result)
        self.assertEqual(helper.merge_preferences(b'broken ini',fresh),fresh)


if __name__=='__main__':unittest.main()
