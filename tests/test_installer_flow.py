"""Exercise GUI decisions, validated placement and updater handoff without a game/network."""
import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import tempfile
import types
import unittest
from unittest.mock import patch,Mock
import zipfile

ROOT=Path(__file__).resolve().parents[1]
def load(name,file):
    spec=importlib.util.spec_from_file_location(name,ROOT/'tools/steam-deck'/file)
    mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod);return mod
def archive(files):
    out=io.BytesIO()
    with zipfile.ZipFile(out,'w') as z:
        for name,data in files.items():z.writestr(name,data)
    return out.getvalue()
class UI:
    def __init__(self,actions):self.actions=iter(actions);self.screens=[];self.phases=[]
    def choice(self,title,message,choices):
        self.screens.append((title,message,choices));action=next(self.actions)
        assert action in dict(choices),(action,choices)
        return action
    def progress(self,phase):self.phases.append(phase)

class InstallerFlowTests(unittest.TestCase):
    def test_gui_links_retry_imports_all_downloads_before_install(self):
        dep=load('flow_dep','dependency_setup.py');guide=load('flow_guide','guided-setup.py')
        with tempfile.TemporaryDirectory() as tmp,contextlib.ExitStack() as stack:
            root=Path(tmp);game=root/'GTA IV with spaces';game.mkdir();downloads=root/'Downloads';downloads.mkdir()
            prefix=root/'compatdata/12210/pfx';(prefix/'drive_c/windows').mkdir(parents=True)
            binaries={'loader':b'checked loader','hook':b'checked native hook','ce':b'checked CE patch'}
            for key in ('loader','hook'):
                spec=dict(dep.AUTO_DEPENDENCIES[key],file_sha256=hashlib.sha256(binaries[key]).hexdigest())
                dep.AUTO_DEPENDENCIES[key]=spec
                name='Ultimate-ASI-Loader (1).zip' if key=='loader' else 'scripthookdotnet_v1.7.1.7b.zip'
                (downloads/name).write_bytes(archive({spec['name']:binaries[key],'do-not-install.exe':b'ignored'}))
            (downloads/'GTAIV_Complete_Edition_Fix_0_4.zip').write_bytes(archive({'aCompleteEditionHook.asi':binaries['ce']}))
            (downloads/'Ultimate-ASI-Loader.zip.part').write_bytes(b'incomplete')
            assets=('update/common/data/frontend_pc.dat','update/common/shaders/fixture.fxc')
            host=b'supported FF host';ff=archive({'plugins/GTAIV.EFLC.FusionFix.asi':host,'plugins/GTAIV.EFLC.FusionFix.ini':b'defaults',assets[0]:b'frontend',assets[1]:b'shader','d3d9.dll':b'optional wrapper'})
            (downloads/'GTAIV.EFLC.FusionFix.zip').write_bytes(ff)
            guide.FUSIONFIX_ASSETS=assets;guide.FUSIONFIX['archive_sha256']=hashlib.sha256(ff).hexdigest()
            bridge={'supported_sha256':[hashlib.sha256(host).hexdigest()]}
            checks=lambda folder,key:( (Path(folder)/{'loader':'dinput8.dll','hook':'ScriptHook.dll','ce':'aCompleteEditionHook.asi'}[key]).is_file(),key+' check')
            for obj,name,value in [(guide,'game_lock',lambda *a:contextlib.nullcontext()),(dep,'select_game',lambda *a,**k:game),
                (dep,'prefix_candidates',lambda *a:[prefix]),(dep,'check_game_version',lambda *a:(True,'supported game')),
                (dep,'check_dependency',checks),(dep,'validate_game',lambda *a:game),(dep,'validate_dependency_bytes',lambda *a:None),
                (dep,'check_runtime',lambda *a:(True,'runtime ready')),(dep,'check_loader_override',lambda *a:(True,'native override ready')),
                (dep,'downloads_dirs',lambda:[downloads])]:stack.enter_context(patch.object(obj,name,value))
            stack.enter_context(patch('sys.platform','linux'))
            opened=stack.enter_context(patch.object(dep,'open_url'))
            guide.configure_dependencies(dep,bridge)
            installed=[]
            def install(folder,data,pfx):
                self.assertTrue(all(ok for ok,_ in dep.inspect_setup(folder,pfx).values()))
                installed.append(folder)
            dep.install_mod=install
            ui=UI(['next','help:loader','retry','help:hook','retry','retry','help:fusionfix','retry','install','done'])
            dep.wizard(ui)
            self.assertEqual(installed,[game]);self.assertEqual(opened.call_count,3)
            self.assertIn(guide.FUSIONFIX['url'].split('/download/')[0]+'/tag/v5.1.2',[c.args[0] for c in opened.call_args_list])
            for key,data in binaries.items():self.assertEqual((game/{'loader':'dinput8.dll','hook':'ScriptHook.dll','ce':'aCompleteEditionHook.asi'}[key]).read_bytes(),data)
            self.assertTrue((game/assets[1]).is_file());self.assertFalse((game/'d3d9.dll').exists());self.assertFalse((game/'do-not-install.exe').exists())
            self.assertTrue((downloads/'GTAIV.EFLC.FusionFix.zip').exists())
            self.assertTrue(any(title=='Ready to install' for title,_,_ in ui.screens))
            self.assertEqual(next(choices for title,_,choices in ui.screens if title=='Ready to install'),[('install','Install GTA IV QoL')])
            self.assertTrue(all('help:hook' not in dict(choices) for title,_,choices in ui.screens if title=='Set up ASI loader'))

    def test_existing_fusionfix_host_does_not_skip_missing_assets(self):
        dep=load('ff_dep','dependency_setup.py');guide=load('ff_guide','guided-setup.py')
        with tempfile.TemporaryDirectory() as tmp:
            game=Path(tmp);(game/'plugins').mkdir();host=b'existing supported host'
            (game/'plugins/GTAIV.EFLC.FusionFix.asi').write_bytes(host)
            (game/'plugins/GTAIV.EFLC.FusionFix.ini').write_bytes(b'user settings')
            guide.FUSIONFIX_ASSETS=('update/one.dat','update/two.dat');(game/'update').mkdir();(game/'update/one.dat').write_bytes(b'custom data')
            bridge={'supported_sha256':[hashlib.sha256(host).hexdigest()]}
            raw=archive({'plugins/GTAIV.EFLC.FusionFix.asi':b'author host','plugins/GTAIV.EFLC.FusionFix.ini':b'default','update/one.dat':b'author data','update/two.dat':b'missing asset','vulkan.dll':b'not required'})
            guide.FUSIONFIX['archive_sha256']=hashlib.sha256(raw).hexdigest()
            self.assertFalse(guide.check_fusionfix(game,dep,bridge)[0])
            guide.install_fusionfix(game,dep,raw,bridge)
            self.assertTrue(guide.check_fusionfix(game,dep,bridge)[0])
            self.assertEqual((game/'plugins/GTAIV.EFLC.FusionFix.asi').read_bytes(),host)
            self.assertEqual((game/'update/one.dat').read_bytes(),b'custom data');self.assertFalse((game/'vulkan.dll').exists())
            self.assertEqual((game/'plugins/GTAIV.EFLC.FusionFix.ini').read_bytes(),b'user settings')

    def test_bad_download_is_not_placed_or_executed(self):
        dep=load('reject_dep','dependency_setup.py')
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);game=root/'game';game.mkdir();downloads=root/'Downloads';downloads.mkdir()
            (downloads/'Ultimate-ASI-Loader.zip').write_bytes(archive({'dinput8.dll':b'wrong bytes'}))
            with patch.object(dep,'downloads_dirs',return_value=[downloads]),patch.object(dep,'check_dependency',return_value=(False,'missing')):
                with self.assertRaises(dep.SetupError):dep.import_dependency_download(game,'loader')
            self.assertFalse((game/'dinput8.dll').exists())
            with patch.object(dep,'check_runtime',return_value=(False,'missing')),patch.object(dep,'run_in_prefix') as run:
                with self.assertRaises(dep.SetupError):dep.install_runtime(game,None,None,b'not Microsoft')
                run.assert_not_called()

    def test_runtime_installs_verified_bytes_and_rechecks_selected_prefix(self):
        dep=load('runtime_dep','dependency_setup.py');raw=b'verified installer fixture';dep.RUNTIME['archive_sha256']=hashlib.sha256(raw).hexdigest()
        with tempfile.TemporaryDirectory() as tmp,patch('sys.platform','linux'):
            root=Path(tmp);game=root/'game';game.mkdir();prefix=root/'Steam/steamapps/compatdata/12210/pfx';(prefix/'drive_c/windows').mkdir(parents=True)
            steam=root/'Steam';tool=steam/'steamapps/common/Proton test/proton';tool.parent.mkdir(parents=True);tool.write_text('# fixture');tool.chmod(0o755)
            (tool.parent/'compatibilitytool.vdf').write_text('"compatibilitytools" { "compat_tools" { "proton_test" { "install_path" "." } } }')
            (steam/'config').mkdir();(steam/'config/config.vdf').write_text('"InstallConfigStore" { "Software" { "Valve" { "Steam" { "CompatToolMapping" { "12210" { "name" "proton_test" } } } } } }')
            ui=UI([])
            with patch.dict('os.environ',XDG_CACHE_HOME=str(root/'cache')),patch.object(dep,'steam_roots',return_value=[steam]),patch.object(dep,'check_runtime',side_effect=[(False,'missing'),(True,'ready')]),patch.object(dep.subprocess,'run',return_value=types.SimpleNamespace(returncode=0,stdout=b'')) as run:
                self.assertIn('re-checked',dep.install_runtime(game,prefix,ui,raw))
                args=run.call_args.args[0];environment=run.call_args.kwargs['env']
                self.assertEqual(args[:2],[str(tool.resolve()),'run']);self.assertEqual(args[-3:],['/install','/quiet','/norestart'])
                self.assertEqual(environment['STEAM_COMPAT_DATA_PATH'],str(prefix.parent.resolve()))
                self.assertEqual(Path(args[2]).read_bytes(),raw);self.assertTrue(ui.phases)

    def test_runtime_recheck_failure_does_not_claim_ready(self):
        dep=load('runtime_fail','dependency_setup.py');raw=b'checked';dep.RUNTIME['archive_sha256']=hashlib.sha256(raw).hexdigest()
        with tempfile.TemporaryDirectory() as tmp,patch.dict('os.environ',XDG_CACHE_HOME=tmp),patch.object(dep,'check_runtime',return_value=(False,'still missing')),patch.object(dep,'run_in_prefix'):
            with self.assertRaisesRegex(dep.SetupError,'re-check failed'):dep.install_runtime(Path(tmp),None,None,raw)

    def test_proton_does_not_create_or_modify_a_different_prefix(self):
        dep=load('wrong_prefix','dependency_setup.py')
        with tempfile.TemporaryDirectory() as tmp:
            wrong=Path(tmp)/'other-wine-prefix';(wrong/'drive_c/windows').mkdir(parents=True)
            with self.assertRaisesRegex(dep.SetupError,'other prefix'):dep.proton_command(wrong,None)
            self.assertFalse((Path(tmp)/'pfx').exists())

    def test_loader_override_is_prefix_scoped_and_read_back(self):
        dep=load('override_dep','dependency_setup.py')
        with tempfile.TemporaryDirectory() as tmp,patch('sys.platform','linux'),patch.object(dep,'check_dependency',return_value=(True,'ASI loader detected: dinput8.dll')):
            prefix=Path(tmp)/'pfx';prefix.mkdir();registry=prefix/'user.reg'
            registry.write_text('[Software\\\\Wine\\\\DllOverrides] 1\n"dinput8"="native,builtin"\n')
            self.assertTrue(dep.check_loader_override(Path(tmp),prefix)[0])
            registry.write_text('[Software\\\\Wine\\\\DllOverrides] 1\n"dinput8"="builtin"\n')
            with patch.object(dep,'run_in_prefix',return_value=types.SimpleNamespace(stdout=b'dinput8 REG_SZ native,builtin')) as run:
                self.assertIn('verified',dep.install_loader_override(Path(tmp),prefix,None))
                self.assertEqual(run.call_count,2);self.assertEqual(run.call_args_list[0].args[0],prefix)
                self.assertIn('native,builtin',run.call_args_list[0].args[2]);self.assertTrue(dep.check_loader_override(Path(tmp),prefix)[0])

    def test_repeat_installer_launch_hands_off_to_integrated_updater(self):
        guide=load('handoff_guide','guided-setup.py');updater=load('handoff_updater','qol-updater.py')
        dep=types.SimpleNamespace(SetupError=RuntimeError,main=Mock(),prefix_candidates=lambda *a:[],existing_prefix=lambda *a:None,inspect_setup=lambda *a:{'all':(True,'fixture ready')})
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);updater.ROOT=root/'installed';updater.ROOT.mkdir();updater.CONFIG=root/'config.json'
            updater.CONFIG.write_text(json.dumps({'game':str(root/'game'),'channel':'beta'}))
            (updater.ROOT/'installed.json').write_text(json.dumps({'sha':'a'*40,'version':'0.1.0-beta.1','files':{}}))
            bundle=root/'bundle.zip';bundle.write_bytes(b'checked fixture')
            manifest={'sha':'a'*40,'release_version':'0.1.0-beta.1'}
            with patch.object(guide,'load_source',side_effect=[dep,updater]),patch.object(guide,'check_fusionfix',return_value=(True,'fixture FF ready')),patch.object(guide,'payload_bridge',return_value={}),patch.object(updater,'make_progress',return_value=None),patch.object(updater,'validate_archive',return_value=(manifest,{})),patch.object(updater,'bundle_files_intact',return_value=True),patch.object(updater,'updater_files_intact',return_value=True),patch.object(updater,'main',return_value=0) as update:
                self.assertEqual(guide.run('','',(bundle,{})),0);update.assert_called_once();dep.main.assert_not_called()

    def test_gui_updater_handoff_normalizes_success(self):
        updater=load('window_updater','qol-updater.py')
        with patch.object(updater,'perform_update',return_value=None):self.assertEqual(updater.main(window=object()),0)

    def test_public_beta_selects_prereleases_without_private_token(self):
        updater=load('beta_select','qol-updater.py');seen=[]
        def release(version,draft=False):return {'tag_name':'v'+version,'draft':draft,'prerelease':True,'assets':[{'id':7,'name':updater.ASSET,'digest':'sha256:'+'a'*64}]}
        def fetch(token,route):seen.append((token,route));return [release('0.1.0-beta.2'),release('0.1.0-beta.10'),release('0.2.0-beta.1',True)]
        candidate=updater.beta_candidate('fixture private token',fetch)
        self.assertEqual(candidate['version'],'0.1.0-beta.10');self.assertEqual(seen[0][0],'')
        self.assertIsNone(updater.beta_candidate(fetch=lambda *a:[]))
        with self.assertRaises(updater.PublicBetaUnavailable):updater.beta_candidate(fetch=Mock(side_effect=updater.AuthError('private archive')))

    def test_beta_checks_never_prompt_for_private_authorization(self):
        updater=load('beta_auth','qol-updater.py')
        lock=types.SimpleNamespace(LOCK_EX=1,LOCK_NB=2,flock=lambda *a:None)
        with tempfile.TemporaryDirectory() as tmp,patch.dict('sys.modules',fcntl=lock):
            root=Path(tmp);updater.ROOT=root/'updater';updater.CONFIG=root/'config.json';updater.CONFIG.write_text(json.dumps({'channel':'beta','token':'fixture private token'}))
            with patch.object(updater,'beta_candidate',side_effect=updater.PublicBetaUnavailable('not published')),patch.object(updater,'authorize') as auth:
                with self.assertRaises(updater.PublicBetaUnavailable):updater.perform_update()
                auth.assert_not_called()

    def test_published_beta_download_verifies_identity_and_uses_no_credentials(self):
        updater=load('beta_download','qol-updater.py')
        files={name:name.encode() for name in updater.FILES}
        manifest={'schema':1,'repository':updater.REPO,'branch':updater.BRANCH,'sha':'b'*40,'run_id':7,'release_version':'0.1.0-beta.2','files':{name:hashlib.sha256(raw).hexdigest() for name,raw in files.items()}}
        data=archive(dict(files,**{'manifest.json':json.dumps(manifest).encode()}));digest=hashlib.sha256(data).hexdigest();seen=[]
        def request(token,route,binary=False):
            seen.append((token,route))
            if binary:return data
            return [{'tag_name':'v0.1.0-beta.2','draft':False,'prerelease':True,'assets':[{'id':3,'name':updater.ASSET,'digest':'sha256:'+digest}]}]
        lock=types.SimpleNamespace(LOCK_EX=1,LOCK_NB=2,flock=lambda *a:None)
        with tempfile.TemporaryDirectory() as tmp,patch.dict('sys.modules',fcntl=lock):
            updater.ROOT=Path(tmp)/'updates';updater.CONFIG=Path(tmp)/'config.json';updater.CONFIG.write_text(json.dumps({'channel':'beta','token':'fixture-private-token'}))
            with patch.object(updater,'request',side_effect=request),patch.object(updater,'apply_update') as install,patch.object(updater,'authorize') as auth:
                updater.perform_update();install.assert_called_once_with(manifest,files,'beta');auth.assert_not_called()
            self.assertEqual([token for token,_ in seen],['',''])
        candidate={'channel':'beta','version':'0.1.0-beta.3','digest':digest}
        with self.assertRaisesRegex(RuntimeError,'version/branch'):updater.validate_archive(data,candidate)

    def test_installed_newer_beta_is_not_downgraded(self):
        updater=load('beta_no_downgrade','qol-updater.py');lock=types.SimpleNamespace(LOCK_EX=1,LOCK_NB=2,flock=lambda *a:None)
        with tempfile.TemporaryDirectory() as tmp,patch.dict('sys.modules',fcntl=lock):
            updater.ROOT=Path(tmp)/'updates';updater.ROOT.mkdir();updater.CONFIG=Path(tmp)/'config.json'
            updater.CONFIG.write_text(json.dumps({'channel':'beta','game':str(Path(tmp)/'game')}))
            (updater.ROOT/'installed.json').write_text(json.dumps({'version':'0.1.0-beta.10'}))
            with patch.object(updater,'beta_candidate',return_value={'version':'0.1.0-beta.2'}),patch.object(updater,'bundle_files_intact',return_value=True),patch.object(updater,'request') as request:
                updater.perform_update();request.assert_not_called()
