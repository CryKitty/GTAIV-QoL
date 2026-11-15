"""Windows platform fixtures; frozen no-Python/Tk/COM proof also runs in CI."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import types
import unittest
from unittest.mock import patch
import zipfile
ROOT=Path(__file__).resolve().parents[1]
def load(name,path):
    spec=importlib.util.spec_from_file_location(name,ROOT/path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
adapter=load('win_adapter','tools/windows/platform_adapter.py')
dep=load('win_dependencies','tools/steam-deck/dependency_setup.py')
package=load('win_package','tools/steam-deck/package-nexus.py')
fixture=load('windows_payload_fixture','tests/test_nexus_release.py')

class WindowsInstaller(unittest.TestCase):
    def test_registry_steam_and_vdf_libraries_without_prefix(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);steam=root/'Steam path';library=root/'Second library'
            (steam/'steamapps').mkdir(parents=True);(library/'steamapps/common/GTAIV/GTAIV').mkdir(parents=True)
            (steam/'steamapps/libraryfolders.vdf').write_text('"libraryfolders" { "1" { "path" "'+library.as_posix()+'" } }')
            (library/'steamapps/appmanifest_12210.acf').write_text('"AppState" { "installdir" "GTAIV" }')
            game=library/'steamapps/common/GTAIV/GTAIV';(game/'GTAIV.exe').write_bytes(b'fixture')
            class Key:
                def __enter__(self):return self
                def __exit__(self,*a):pass
            registry=types.SimpleNamespace(HKEY_CURRENT_USER=1,HKEY_LOCAL_MACHINE=2,OpenKey=lambda *a:Key(),QueryValueEx=lambda *a:(str(steam),1))
            roots=adapter.steam_roots(registry,{})
            self.assertEqual(roots,[steam.resolve()])
            with patch.object(dep,'steam_roots',return_value=roots),patch.object(dep,'validate_game',side_effect=lambda p:p.parent if p.is_file() else (_ for _ in ()).throw(dep.SetupError('missing'))),patch.object(dep,'prefix_candidates',side_effect=AssertionError('No Proton')):
                self.assertEqual(dep.discover_games(),[(game/'GTAIV.exe').resolve()])
    def test_windows_runtime_is_native_and_has_no_wine_stub_veto(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);(root/'SysWOW64').mkdir()
            with patch.object(dep.sys,'platform','win32'),patch.dict(os.environ,WINDIR=str(root)),patch.object(dep,'PE32',return_value=types.SimpleNamespace(is_dll=True,data=b'wine builtin dll')):
                self.assertTrue(dep.check_runtime(root)[0])
    def test_native_runtime_command_does_not_invoke_proton(self):
        with patch.object(adapter.subprocess,'run',return_value=types.SimpleNamespace(returncode=3010)) as run:
            self.assertEqual(adapter.native_runtime(['verified-runtime.exe','/quiet']).returncode,3010)
        self.assertEqual(run.call_args.args[0],['verified-runtime.exe','/quiet'])
        self.assertEqual(run.call_args.kwargs['creationflags'],adapter.NO_WINDOW)
    def test_shortcut_uses_frozen_updater_not_python(self):
        with patch.dict(os.environ,WINDIR='C:/Windows'),patch.object(adapter.subprocess,'run') as run:
            adapter.create_shortcut(Path("C:/User's files/Update-GTAIV-QoL.exe"),Path('C:/Desktop/Update GTA IV QoL.lnk'))
        args=run.call_args.args[0];self.assertIn('CreateShortcut',args[-1]);self.assertIn('--update',args[-1]);self.assertNotIn('python',args[-1].lower());self.assertIn("User''s",args[-1])
    def test_windows_zip_keeps_payload_byte_identical(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);(root/'build').mkdir();folder=root/'tools/steam-deck';folder.mkdir(parents=True)
            for name in ('README.md','CHANGELOG.md','VERSION','LICENSE','docs/BUNDLE_INVENTORY.md','docs/KNOWN_ISSUES.md','tools/steam-deck/qol-updater.py'):
                target=root/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes((ROOT/name).read_bytes())
            data,_=fixture.payload();payload=root/'original.zip';payload.write_bytes(data)
            exe=root/'Install-GTAIV-QoL.exe';exe.write_bytes(b'MZfrozen-fixture');exe.with_name('WINDOWS-RUNTIME-LICENSES.txt').write_text('licence fixture')
            result=package.package_windows(root,exe,payload)
            self.assertEqual(result.name,'GTAIV-QoL-1.0.0-Windows.zip')
            with zipfile.ZipFile(result) as z:
                self.assertEqual(z.read('GTAIV-QoL-update.zip'),data)
                info=json.loads(z.read('release-info.json'));self.assertEqual(info['payload_sha256'],hashlib.sha256(data).hexdigest())
                self.assertIn(b'No Python',z.read('START-HERE.txt'));self.assertNotIn('Install-GTAIV-QoL.desktop',z.namelist())
    def test_delivery_cannot_change_deck_payload_or_version(self):
        self.assertEqual((ROOT/'VERSION').read_text().strip(),'1.0.0')
        workflow=(ROOT/'.github/workflows/build.yml').read_text()
        self.assertIn('steps.payload.outputs.publish',workflow)
        policy=load('payload_policy','tools/windows/payload_policy.py')
        self.assertFalse(policy.publish_payload(['tools/windows/bootstrap.py','CHANGELOG.md','.github/workflows/build.yml']))
        self.assertTrue(policy.publish_payload(['tools/windows/bootstrap.py','src/main.cpp']))
        self.assertTrue(policy.publish_payload(['VERSION']))
        builder=(ROOT/'tools/windows/build_installer.py').read_text()
        self.assertIn('132c19079346dea95bed66135347308d9403d9631cc3cb43be7e0c9c01daf9a3',builder)
