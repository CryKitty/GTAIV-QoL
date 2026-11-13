import importlib.util
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('package_updater',ROOT/'tools/steam-deck/package-updater.py')
p=importlib.util.module_from_spec(spec);spec.loader.exec_module(p)
class PackageUpdaterTests(unittest.TestCase):
    def test_bootstrap_keeps_installed_updater_and_short_command(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);desktop=root/'Update test.desktop'
            env=dict(os.environ,HOME=str(root),USERPROFILE=str(root))
            installed=root/'.local/share/gtaiv-qol-updater/qol-updater.py'
            installed.parent.mkdir(parents=True)
            installed.write_text("BOOTSTRAP_REVISION=2\nraise RuntimeError('old broken updater')\n")
            for source in (b"BOOTSTRAP_REVISION=5\nprint('BOOTSTRAP_OK')\n",b"print('MUST_NOT_OVERWRITE')\n"):
                desktop.write_text(p.launcher(source))
                command=next(line[5:] for line in desktop.read_text().splitlines() if line.startswith('Exec='))
                self.assertLess(len(command),1024)
                args=shlex.split(command);args[0]=sys.executable;args[-1]=desktop.as_uri()
                result=subprocess.run(args,capture_output=True,text=True,env=env,check=True)
                self.assertEqual(result.stdout.strip(),'BOOTSTRAP_OK')
            self.assertTrue((root/'.local/share/gtaiv-qol-updater/qol-updater.py').exists())

    def test_bootstrap_upgrades_revision4_but_preserves_future_revision(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);desktop=root/'Update new repo.desktop'
            env=dict(os.environ,HOME=str(root),USERPROFILE=str(root))
            installed=root/'.local/share/gtaiv-qol-updater/qol-updater.py';installed.parent.mkdir(parents=True)
            desktop.write_text(p.launcher(b"BOOTSTRAP_REVISION=5\nprint('MAIN_UPDATER')\n"))
            command=next(line[5:] for line in desktop.read_text().splitlines() if line.startswith('Exec='))
            args=shlex.split(command);args[0]=sys.executable;args[-1]=str(desktop)
            for revision,expected in [(4,'MAIN_UPDATER'),(6,'FUTURE_UPDATER')]:
                installed.write_text("BOOTSTRAP_REVISION="+str(revision)+"\nprint('FUTURE_UPDATER')\n")
                result=subprocess.run(args,capture_output=True,text=True,env=env,check=True)
                self.assertEqual(result.stdout.strip(),expected)

    def test_packaged_window_preserves_utf8_on_windows_build_hosts(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            (root/'qol-updater.py').write_text("PROGRESS_UI = ''\n# Ready — play\n",encoding='utf-8')
            ui="# − What changed • Ready — play\n"
            (root/'progress_ui.py').write_text(ui,encoding='utf-8')
            original=Path.read_text
            def windows_read(path,*args,**kwargs):
                return original(path,*args,**({'encoding':'cp1252'}|kwargs))
            with patch.object(Path,'read_text',windows_read):source=p.render_updater(root).decode('utf-8')
            import ast
            value=ast.literal_eval(ast.parse(source).body[0].value)
            self.assertEqual(value,ui)
            self.assertIn('Ready — play',source)

    def test_packaged_worker_carries_build_identity(self):
        root=ROOT/'tools/steam-deck'
        with patch.dict(os.environ,{'GITHUB_SHA':'a'*40,'GITHUB_RUN_NUMBER':'47'}):
            source=p.render_updater(root).decode('utf-8')
        import ast
        values={n.targets[0].id:ast.literal_eval(n.value) for n in ast.parse(source).body
                if isinstance(n,ast.Assign) and isinstance(n.targets[0],ast.Name)
                and n.targets[0].id in ('BUILD_SHA','BUILD_NUMBER')}
        self.assertEqual(values,{'BUILD_SHA':'a'*40,'BUILD_NUMBER':'47'})

    def test_packages_flat_versioned_payload(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);(root/'tools/steam-deck').mkdir(parents=True);(root/'build').mkdir()
            for name in ('GTAIV-QoL.asi','GTAIV-QoL-Pause.asi'):(root/'build'/name).write_bytes(name.encode())
            (root/'GTAIV-QoL.ini.example').write_text('ZoomPercent=300')
            (root/'CHANGELOG.md').write_text('Update — compass',encoding='utf-8')
            for name in ('enable-qol-logs.py','qol-updater.py'):(root/'tools/steam-deck'/name).write_text("UPDATE_SOURCE={'repository':'CryKitty/GTAIV-QoL','branch':'main'}\n")
            old=p.__file__;p.__file__=str(root/'tools/steam-deck/package-updater.py')
            try:
                with patch.dict(os.environ,{'GITHUB_SHA':'a'*40,'GITHUB_REF_NAME':'main','GITHUB_RUN_ID':'7'}):p.main()
            finally:p.__file__=old
            files={x.name for x in (root/'build/update-payload').iterdir()}
            self.assertEqual(files,{'manifest.json','GTAIV-QoL.asi','GTAIV-QoL-Pause.asi','GTAIV-QoL.ini','install-helper.py','qol-updater.py'})
            manifest=json.loads((root/'build/update-payload/manifest.json').read_text())
            self.assertEqual(manifest['sha'],'a'*40);self.assertEqual(manifest['run_id'],7)
            self.assertEqual(manifest['changelog'],'Update — compass')
            self.assertTrue((root/'tools/steam-deck/dist/Update-GTAIV-QoL.desktop').exists())
if __name__=='__main__':unittest.main()
