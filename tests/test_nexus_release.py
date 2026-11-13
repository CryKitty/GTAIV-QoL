import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import types
import unittest
from unittest.mock import patch
import zipfile

ROOT=Path(__file__).resolve().parents[1]
def load(name,path):
    spec=importlib.util.spec_from_file_location(name,ROOT/path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
pack=load('nexus_package','tools/steam-deck/package-nexus.py')
guided=load('nexus_setup','tools/steam-deck/guided-setup.py')
launcher=load('nexus_launcher','tools/steam-deck/package-guided-setup.py')
tag=load('beta_tag','tools/check-beta-tag.py')

def payload():
    files={name:raw for name,raw in [('GTAIV-QoL.asi',b'main'),('GTAIV-QoL-Pause.asi',b'pause'),
        ('GTAIV-QoL.ini',(ROOT/'GTAIV-QoL.ini.example').read_bytes()),
        ('install-helper.py',(ROOT/'tools/steam-deck/enable-qol-logs.py').read_bytes()),
        ('qol-updater.py',(ROOT/'tools/steam-deck/qol-updater.py').read_bytes())]}
    manifest={'schema':1,'repository':'CryKitty/GTAIV-QoL','sha':'a'*40,'branch':'feature/beta-review','run_id':7,
        'release_version':'0.1.0-beta.1','files':{name:hashlib.sha256(data).hexdigest() for name,data in files.items()}}
    out=io.BytesIO()
    with zipfile.ZipFile(out,'w',zipfile.ZIP_DEFLATED) as archive:
        for name,data in files.items():archive.writestr(name,data)
        archive.writestr('manifest.json',json.dumps(manifest))
    data=out.getvalue();candidate={'channel':'bundle','sha':'a'*40,'run_id':7,'branch':manifest['branch'],'digest':hashlib.sha256(data).hexdigest()}
    return data,candidate

class NexusReleaseTests(unittest.TestCase):
    def test_archive_carries_both_asis_all_components_and_guided_payload(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);(root/'build').mkdir();folder=root/'tools/steam-deck';folder.mkdir(parents=True)
            data,_=payload();(root/'build/GTAIV-QoL-update.zip').write_bytes(data)
            for name in ('VERSION','README.md','CHANGELOG.md','LICENSE','docs/BUNDLE_INVENTORY.md','docs/KNOWN_ISSUES.md'):
                dest=root/name;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes((ROOT/name).read_bytes())
            (root/'build/FusionFix-GPL-3.0.txt').write_text('fixture existing third-party notice')
            for name in ('package-guided-setup.py','qol-updater.py','guided-setup.py','dependency_setup.py','progress_ui.py'):
                (folder/name).write_bytes((ROOT/'tools/steam-deck'/name).read_bytes())
            result=pack.package(root)
            with zipfile.ZipFile(result) as archive:
                self.assertIsNone(archive.testzip());self.assertEqual(archive.read('GTAIV-QoL-update.zip'),data)
                self.assertEqual((archive.getinfo('Install-GTAIV-QoL.desktop').external_attr>>16)&0o777,0o755)
                self.assertEqual(len(json.loads(archive.read('release-info.json'))['components']),7)
                self.assertIn(b'Avary Cooney',archive.read('LICENSE'))
                self.assertIn(b'Map Autozoom',archive.read('BUNDLE-INVENTORY.md'))
                with zipfile.ZipFile(io.BytesIO(archive.read('GTAIV-QoL-update.zip'))) as update:
                    self.assertEqual(update.read('GTAIV-QoL.asi'),b'main');self.assertEqual(update.read('GTAIV-QoL-Pause.asi'),b'pause')

    def test_bundled_guide_installs_without_private_repository_or_upload_credentials(self):
        for channel in (None,'dev','beta'):
            with self.subTest(channel=channel),tempfile.TemporaryDirectory() as tmp:
                root=Path(tmp);game=root/'game';game.mkdir();(game/'GTAIV.exe').write_bytes(b'exe')
                data,candidate=payload();path=root/'GTAIV-QoL-update.zip';path.write_bytes(data)
                updater=load('offline_update','tools/steam-deck/qol-updater.py')
                updater.CONFIG=root/'.config/gtaiv-qol-updater/config.json';updater.ROOT=root/'.local/share/gtaiv-qol-updater'
                if channel:
                    updater.CONFIG.parent.mkdir(parents=True);updater.CONFIG.write_text(json.dumps({'channel':channel,'game':str(game)}))
                    updater.ROOT.mkdir(parents=True);(updater.ROOT/'installed.json').write_text(json.dumps({'sha':'b'*40,'version':'0.1.0-beta.1','files':{}}))
                    (updater.ROOT/'qol-updater.py').write_text("BOOTSTRAP_REVISION=4\n# legacy updater\n")
                dep=types.SimpleNamespace(SetupError=RuntimeError,GUIDE_PROTOCOL=2,install_dependency_bytes=lambda *a:None,run_in_prefix=lambda *a:None,REQUIREMENTS={},URLS={},EXTRA_CHECKS={},EXTRA_DOWNLOADERS={},EXTRA_IMPORTERS={},inspect_setup=lambda *a:{'all':(True,'fixture dependencies ready')})
                def main():dep.install_mod(game,b'');return 0
                dep.main=main;dep.prefix_candidates=lambda *a:[];dep.existing_prefix=lambda *a:None
                fcntl=types.SimpleNamespace(LOCK_EX=1,LOCK_NB=2,flock=lambda *a:None)
                with patch.dict(sys.modules,fcntl=fcntl),patch('pathlib.Path.home',return_value=root),patch.object(guided,'load_source',side_effect=[dep,updater]),patch.object(guided,'install_fusionfix'),patch.object(guided,'check_fusionfix',return_value=(True,'fixture host')),patch.object(updater,'bundle_files_intact',return_value=True),patch.object(updater,'make_progress',return_value=None),patch.object(updater,'shortcut'),patch.object(updater.subprocess,'run',return_value=types.SimpleNamespace(returncode=0)),patch.object(updater,'beta_candidate',return_value=None),patch.object(updater,'request',side_effect=AssertionError('must not access private repository')):
                    self.assertEqual(guided.run('','',(path,candidate)),0)
                self.assertEqual((game/'GTAIV-QoL.asi').read_bytes(),b'main');self.assertEqual((game/'GTAIV-QoL-Pause.asi').read_bytes(),b'pause')
                self.assertEqual(json.loads(updater.CONFIG.read_text())['channel'],channel or 'beta')
                self.assertEqual(json.loads((updater.ROOT/'installed.json').read_text())['sha'],'a'*40)
                self.assertEqual(json.loads((updater.ROOT/'installed.json').read_text())['branch'],'main')
                self.assertTrue(updater.updater_files_intact(json.loads((updater.ROOT/'installed.json').read_text())))
                self.assertFalse((root/'.config/roadnav-uploader/config.json').exists())

    def test_local_payload_rejects_tampering_and_wrong_build_identity(self):
        updater=load('bundle_verify','tools/steam-deck/qol-updater.py');data,candidate=payload()
        for changed,raw in ((dict(candidate,digest='0'*64),data),(dict(candidate,sha='b'*40),data),
                            (dict(candidate,run_id=8),data),(dict(candidate,branch='main'),data),(candidate,data+b'tamper')):
            with self.assertRaises(RuntimeError):updater.validate_archive(raw,changed)

    def test_short_bundled_launcher_supports_spaces_and_file_uris(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);data,_=payload();(root/'GTAIV-QoL-update.zip').write_bytes(data)
            (root/'dependency_setup.py').write_text('# fixture');(root/'qol-updater.py').write_text('# fixture')
            (root/'guided-setup.py').write_text("from pathlib import Path\nimport hashlib\ndef run(a,b,local):\n p,c=local\n assert p.read_bytes() and hashlib.sha256(p.read_bytes()).hexdigest()==c['digest']\n print('BUNDLE_OK')\n return 0\nif __name__=='__main__': pass\n")
            desktop=root/'Install beta with spaces.desktop';desktop.write_text(launcher.launcher(root,data))
            command=next(line[5:] for line in desktop.read_text().splitlines() if line.startswith('Exec='));self.assertLess(len(command),1024)
            for location in (str(desktop),desktop.as_uri()):
                args=shlex.split(command);args[0]=sys.executable;args[-1]=location
                result=subprocess.run(args,capture_output=True,text=True,check=True);self.assertEqual(result.stdout.strip(),'BUNDLE_OK')

    def test_beta_tag_requires_matching_version_and_canonical_ancestry(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            def git(*args):subprocess.run(['git',*args],cwd=root,check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
            git('init');(root/'VERSION').write_text('0.1.0-beta.1\n');git('add','VERSION');git('-c','user.name=Fixture','-c','user.email=fixture@example.invalid','commit','-m','fixture')
            git('update-ref','refs/remotes/origin/main','HEAD');tag.check(root,'v0.1.0-beta.1')
            with self.assertRaises(RuntimeError):tag.check(root,'v1.0.0')
            (root/'other').write_text('not canonical');git('add','other');git('-c','user.name=Fixture','-c','user.email=fixture@example.invalid','commit','-m','unmerged')
            with self.assertRaises(subprocess.CalledProcessError):tag.check(root,'v0.1.0-beta.1')
