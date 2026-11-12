import hashlib
import importlib.util
import io
from pathlib import Path
import tempfile
import types
import unittest
from unittest.mock import patch
import zipfile

ROOT=Path(__file__).resolve().parents[1]
def module(name,path):
    spec=importlib.util.spec_from_file_location(name,ROOT/path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
setup=module('guided','tools/steam-deck/guided-setup.py')
dep=module('dependencies','tools/steam-deck/dependency_setup.py')
helper=module('fresh_helper','tools/steam-deck/enable-qol-logs.py')

def archive(files):
    out=io.BytesIO()
    with zipfile.ZipFile(out,'w') as z:
        for name,data in files.items():z.writestr(name,data)
    return out.getvalue()

class GuidedTests(unittest.TestCase):
    def test_discovers_game_in_sd_card_steam_library(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);steam=root/'Steam';sd=root/'SD card library';(steam/'steamapps').mkdir(parents=True)
            (steam/'steamapps/libraryfolders.vdf').write_text('"libraryfolders" { "1" { "path" "'+sd.as_posix()+'" } }')
            (sd/'steamapps/common/My GTA IV/GTAIV').mkdir(parents=True)
            (sd/'steamapps/appmanifest_12210.acf').write_text('"AppState" { "installdir" "My GTA IV" }')
            exe=sd/'steamapps/common/My GTA IV/GTAIV/GTAIV.exe';exe.write_bytes(b'fixture')
            def validate(path):
                if not path.is_file():raise dep.SetupError('missing')
                return path.parent
            with patch.object(dep,'steam_roots',return_value=[steam]),patch.object(dep,'validate_game',side_effect=validate):
                self.assertEqual(dep.discover_games(),[exe.resolve()])

    def test_ce_download_is_found_and_only_expected_asi_imported(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);payload=archive({'package/aCompleteEditionHook.asi':b'ce','other.exe':b'not installed'})
            (root/'GTAIV Complete Edition Fix 0_4.zip').write_bytes(payload)
            with patch.object(dep,'downloads_dirs',return_value=[root]),patch.object(dep,'check_dependency',return_value=(False,'')),patch.object(dep,'install_dependency_bytes',return_value='installed') as install:
                self.assertIn('installed',dep.import_ce_download(root));install.assert_called_once_with(root,'ce',b'ce')
            self.assertFalse((root/'other.exe').exists())
    def test_fusionfix_preserves_existing_wrapper_and_ini(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);(root/'d3d9.dll').write_bytes(b'personal wrapper');(root/'plugins').mkdir();(root/'plugins/GTAIV.EFLC.FusionFix.ini').write_bytes(b'preferences')
            data=archive({'d3d9.dll':b'author wrapper','plugins/GTAIV.EFLC.FusionFix.ini':b'defaults','plugins/GTAIV.EFLC.FusionFix.asi':b'ff','update/common/data/frontend_pc.dat':b'assets'})
            with patch.dict(setup.FUSIONFIX,archive_sha256=hashlib.sha256(data).hexdigest()):setup.install_fusionfix(root,dep,data)
            self.assertEqual((root/'d3d9.dll').read_bytes(),b'personal wrapper');self.assertEqual((root/'plugins/GTAIV.EFLC.FusionFix.ini').read_bytes(),b'preferences');self.assertEqual((root/'update/common/data/frontend_pc.dat').read_bytes(),b'assets')
    def test_fusionfix_rejects_unsafe_archive_before_writes(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);data=archive({'plugins/GTAIV.EFLC.FusionFix.asi':b'ff','../escape':b'bad'})
            with patch.dict(setup.FUSIONFIX,archive_sha256=hashlib.sha256(data).hexdigest()):
                with self.assertRaises(dep.SetupError):setup.install_fusionfix(root,dep,data)
            self.assertFalse((root/'plugins/GTAIV.EFLC.FusionFix.asi').exists())
    def test_fusionfix_keeps_existing_custom_asset_and_places_missing_host(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);(root/'update').mkdir();(root/'update/other.dat').write_bytes(b'personal');data=archive({'plugins/GTAIV.EFLC.FusionFix.asi':b'ff','update/other.dat':b'author'})
            with patch.dict(setup.FUSIONFIX,archive_sha256=hashlib.sha256(data).hexdigest()):
                setup.install_fusionfix(root,dep,data)
            self.assertEqual((root/'update/other.dat').read_bytes(),b'personal');self.assertEqual((root/'plugins/GTAIV.EFLC.FusionFix.asi').read_bytes(),b'ff')
    def test_fresh_install_does_not_require_uploader_credentials(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);game=root/'game';game.mkdir();(game/'GTAIV.exe').write_bytes(b'exe');config=root/'.config/gtaiv-qol-updater/config.json';config.parent.mkdir(parents=True);config.write_text(__import__('json').dumps({'game':str(game)}))
            fcntl=types.SimpleNamespace(LOCK_EX=1,LOCK_NB=2,flock=lambda *a:None)
            with patch('pathlib.Path.home',return_value=root),patch.dict('sys.modules',{'fcntl':fcntl}),patch.object(helper,'BUNDLE',{'fixture':True}),patch.object(helper,'install_bundle') as install,patch.object(helper,'dialog'):
                helper.main();install.assert_called_once_with(game,{'fixture':True})
            self.assertFalse((root/'.config/roadnav-uploader/config.json').exists())
