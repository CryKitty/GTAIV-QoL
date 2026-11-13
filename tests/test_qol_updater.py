import importlib.util
from pathlib import Path
import unittest
import hashlib
import io
import json
import zipfile
import tempfile
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('qol_updater',ROOT/'tools/steam-deck/qol-updater.py')
u=importlib.util.module_from_spec(spec);spec.loader.exec_module(u)
SHA='a'*40

def payload():
    files={name:(name+' payload').encode() for name in u.FILES}
    manifest={'schema':1,'repository':u.REPO,'sha':SHA,'branch':u.BRANCH,'run_id':7,'files':{name:hashlib.sha256(raw).hexdigest() for name,raw in files.items()}}
    return files,manifest

def archive(files,manifest,extra=None):
    out=io.BytesIO()
    with zipfile.ZipFile(out,'w') as z:
        for name,raw in files.items():z.writestr(name,raw)
        z.writestr('manifest.json',json.dumps(manifest))
        if extra:z.writestr(*extra)
    data=out.getvalue()
    return data,{'channel':'dev','sha':SHA,'run_id':7,'digest':hashlib.sha256(data).hexdigest()}

class UpdaterTests(unittest.TestCase):
    def test_selects_only_successful_same_repo_branch_payload(self):
        def run(identifier,**changes):
            result={'id':identifier,'run_number':identifier,'head_sha':SHA,'status':'completed','conclusion':'success','head_branch':u.BRANCH,'event':'push','head_repository':{'full_name':u.REPO}};result.update(changes);return result
        runs=[run(1,status='in_progress'),run(2,conclusion='failure'),run(3,event='pull_request'),run(4,head_repository={'full_name':'someone/fork'}),run(5,head_branch='feature/cutscene-pause'),run(6),run(7)]
        requests=[]
        def fetch(token,route):
            requests.append(route)
            if '/workflows/' in route:return {'workflow_runs':runs}
            if '/runs/6/' in route:return {'artifacts':[{'name':u.ARTIFACT,'expired':True}]}
            return {'artifacts':[{'id':17,'name':u.ARTIFACT,'expired':False,'digest':'sha256:'+'b'*64,'workflow_run':{'head_sha':SHA,'head_branch':u.BRANCH}}]}
        result=u.dev_candidate('fake',fetch)
        self.assertEqual(result['run_id'],7)
        self.assertEqual(result['route'],'/actions/artifacts/17/zip')
        self.assertFalse(any('/runs/'+str(i)+'/' in r for i in range(1,6) for r in requests))

    def test_release_mode_excludes_prereleases_and_drafts(self):
        asset={'id':9,'name':u.ASSET,'digest':'sha256:'+'b'*64}
        releases=[{'draft':True,'assets':[asset]},{'prerelease':True,'assets':[asset]},{'draft':False,'prerelease':False,'tag_name':'v1.0','assets':[asset]}]
        result=u.release_candidate('',lambda token,route:releases)
        self.assertEqual(result['label'],'release v1.0')

    def test_archive_identity_and_file_integrity(self):
        files,manifest=payload();data,candidate=archive(files,manifest)
        parsed,contents=u.validate_archive(data,candidate)
        self.assertEqual(contents,files);self.assertEqual(parsed['sha'],SHA)
        bad=dict(candidate,digest='0'*64)
        with self.assertRaises(RuntimeError):u.validate_archive(data,bad)
        bad=dict(candidate,sha='c'*40)
        with self.assertRaises(RuntimeError):u.validate_archive(data,bad)
        bad=dict(candidate,run_id=8)
        with self.assertRaises(RuntimeError):u.validate_archive(data,bad)
        files['GTAIV-QoL.asi']=b'tampered'
        data,candidate=archive(files,manifest)
        with self.assertRaises(RuntimeError):u.validate_archive(data,candidate)

    def test_archive_rejects_unexpected_paths_and_duplicate_names(self):
        files,manifest=payload()
        for extra in (('../outside',b'bad'),('GTAIV-QoL.asi',b'duplicate')):
            data,candidate=archive(files,manifest,extra)
            with self.assertRaises(RuntimeError):u.validate_archive(data,candidate)

    def test_apply_records_state_only_after_success_and_refreshes_updater(self):
        files,manifest=payload()
        files['install-helper.py']=b"def main():\n assert len(BUNDLE)==3\n assert INSTALL_BUILD=='aaaaaaaaaaaa'\n"
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            with patch.object(u,'ROOT',root),patch.object(u,'shortcut') as link:
                u.apply_update(manifest,files,'dev')
                self.assertEqual((root/'qol-updater.py').read_bytes(),files['qol-updater.py'])
                self.assertEqual(json.loads((root/'installed.json').read_text())['sha'],SHA)
                link.assert_called_once()
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);(root/'qol-updater.py').write_bytes(b'previous')
            files['install-helper.py']=b"def main():\n raise RuntimeError('fixture install failure')\n"
            with patch.object(u,'ROOT',root),patch.object(u,'shortcut'):
                with self.assertRaises(RuntimeError):u.apply_update(manifest,files,'dev')
            self.assertFalse((root/'installed.json').exists())
            self.assertEqual((root/'qol-updater.py').read_bytes(),b'previous')

    def test_same_sha_repairs_stale_uploader_and_ingame_check_stages_repair(self):
        import sys
        from unittest.mock import Mock
        from test_upload_recovery import DeliveryTests
        fixture=DeliveryTests()
        files,manifest=payload()
        files['install-helper.py']=(ROOT/'tools/steam-deck/enable-qol-logs.py').read_bytes()
        files['qol-updater.py']=(ROOT/'tools/steam-deck/qol-updater.py').read_bytes()
        files['GTAIV-QoL.ini']=b'[Updates]\nCheckForUpdates=1\n'
        manifest['files']={name:hashlib.sha256(raw).hexdigest() for name,raw in files.items()}
        data,candidate=archive(files,manifest);candidate.update(route='/fixture',label='fixture',size=len(data))
        with tempfile.TemporaryDirectory() as tmp:
            home=Path(tmp);game,uploader,config,standalone,queued=fixture.installed(home)
            auth=config.read_bytes();state=home/'.local/share/gtaiv-qol-updater';state.mkdir()
            (state/'qol-updater.py').write_bytes(files['qol-updater.py'])
            saved=dict(manifest,repository=u.REPO,branch=u.BRANCH,channel='dev')
            (state/'installed.json').write_text(json.dumps(saved))
            phases=[]
            with patch.object(Path,'home',return_value=home),patch.dict(sys.modules,{'fcntl':Mock(LOCK_EX=2,LOCK_NB=4)}),patch.object(u,'ROOT',state),patch.object(u,'CONFIG',standalone),patch.object(u,'shortcut'),patch.object(u,'bundle_files_intact',return_value=True),patch.object(u,'dev_candidate',return_value=candidate),patch.object(u,'request',return_value=data),patch.object(u.subprocess,'run'),patch.object(u,'progress',side_effect=lambda n,phase,**kw:phases.append(phase)):
                self.assertTrue(u.updater_files_intact(saved))
                self.assertFalse(u.uploader_files_intact(files))
                u.perform_update() # Actual manifest verification, installer and uploader replacement.
                self.assertIn('Update complete — ready to play',phases);self.assertNotIn('Already up to date',phases)
                self.assertTrue(u.uploader_files_intact(files));self.assertTrue(u.uploader_files_intact())
                self.assertEqual(config.read_bytes(),auth);self.assertEqual(queued[0].read_bytes(),queued[1])
                phases.clear();u.perform_update();self.assertIn('Already up to date',phases)
                uploader.write_text(fixture.legacy,encoding='utf-8')
                # Same SHA in the in-game worker must stage, not short-circuit.
                self.assertTrue(u.stage_game_update(game))
                with patch.object(u,'game_running',return_value=True):self.assertFalse(u.apply_game_update(game))
                self.assertFalse(u.uploader_files_intact(files))
                with patch.object(u,'game_running',return_value=False):self.assertTrue(u.apply_game_update(game))
                self.assertTrue(u.uploader_files_intact(files));self.assertFalse(u.stage_game_update(game))
                self.assertEqual(config.read_bytes(),auth);self.assertEqual(queued[0].read_bytes(),queued[1])

    def test_redirect_never_forwards_private_token(self):
        import urllib.error
        requests=[]
        class Opener:
            def open(self,req,timeout):
                requests.append(req)
                raise urllib.error.HTTPError(req.full_url,302,'Found',{'Location':'https://example.blob.core.windows.net/signed'},None)
        def download(req,timeout):
            self.assertNotIn('Authorization',req.headers)
            return io.BytesIO(b'zip fixture')
        with patch.object(u.urllib.request,'build_opener',return_value=Opener()),patch.object(u.urllib.request,'urlopen',side_effect=download):
            self.assertEqual(u.request('unit-test-token','/actions/artifacts/1/zip',True),b'zip fixture')
        self.assertEqual(requests[0].headers['Authorization'],'Bearer unit-test-token')
        self.assertEqual(requests[0].headers['Accept'],'application/vnd.github+json')

    def test_release_download_uses_binary_accept(self):
        class Opener:
            def open(self,req,timeout):
                assert req.headers['Accept']=='application/octet-stream'
                return io.BytesIO(b'release zip')
        with patch.object(u.urllib.request,'build_opener',return_value=Opener()):
            self.assertEqual(u.request('test-token','/releases/assets/1',True),b'release zip')

    def test_authorization_checks_repository_root(self):
        config={}
        class Opener:
            def open(self,req,timeout):
                self_url=req.full_url
                assert self_url==u.API
                return io.BytesIO(b'{"private":true}')
        with patch.object(u,'dialog',side_effect=['','test-token']),patch.object(u.subprocess,'Popen'),patch.object(u.urllib.request,'build_opener',return_value=Opener()),patch.object(u,'atomic') as save:
            self.assertEqual(u.authorize(config),'test-token')
            save.assert_called_once()
        for route in ('https://example.com','//example.com'):
            with self.assertRaisesRegex(RuntimeError,'Invalid update request'):u.request('test-token',route)

    def test_no_credential_in_packaged_source(self):
        for path in ('tools/steam-deck/qol-updater.py','tools/steam-deck/package-updater.py'):
            source=(ROOT/path).read_text()
            self.assertNotIn('github_pat_',source);self.assertNotIn('ghp_',source)

if __name__=='__main__':unittest.main()
