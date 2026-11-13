import importlib.util,json,tempfile,unittest,sys
from pathlib import Path
from unittest.mock import patch,Mock
ROOT=Path(__file__).resolve().parents[1]
s=importlib.util.spec_from_file_location('game_updates',ROOT/'tools/steam-deck/qol-updater.py');u=importlib.util.module_from_spec(s);s.loader.exec_module(u)
class InGameUpdates(unittest.TestCase):
 def fixture(self,folder):
  game=Path(folder)/'Game with spaces';game.mkdir();(game/'GTAIV.exe').write_bytes(b'fixture')
  (game/'GTAIV-QoL.ini').write_text('[Updates]\nCheckForUpdates=1\n')
  state=Path(folder)/'state';state.mkdir();config=Path(folder)/'config.json';config.write_text(json.dumps({'game':str(game),'channel':'beta'}))
  return game,state,config
 def test_staging_does_not_apply_while_game_runs_and_exit_revalidates(self):
  with tempfile.TemporaryDirectory() as folder:
   game,state,config=self.fixture(folder)
   candidate={'channel':'beta','digest':'a'*64,'route':'/fixture'};manifest={'sha':'b'*40};files={'fixture':b'new'}
   with patch.object(u,'ROOT',state),patch.object(u,'CONFIG',config),patch.object(u,'beta_candidate',return_value=candidate),patch.object(u,'request',return_value=b'ZIP'),patch.object(u,'validate_archive',return_value=(manifest,files)) as verify,patch.object(u,'apply_update') as apply:
    self.assertTrue(u.stage_game_update(game));apply.assert_not_called()
    with patch.object(u,'game_running',return_value=True):self.assertFalse(u.apply_game_update(game))
    apply.assert_not_called()
    with patch.object(u,'game_running',return_value=False):self.assertTrue(u.apply_game_update(game))
    apply.assert_called_once_with(manifest,files,'beta');self.assertEqual(verify.call_count,2)
    self.assertFalse((u.pending_directory(game)/'candidate.json').exists())
 def test_opt_out_cancels_pending_apply_and_off_never_checks_network(self):
  with tempfile.TemporaryDirectory() as folder:
   game,state,config=self.fixture(folder)
   with patch.object(u,'ROOT',state),patch.object(u,'CONFIG',config),patch.object(u,'request') as fetch,patch.object(u,'apply_update') as apply,patch.object(u,'game_running',return_value=False):
    stage=u.pending_directory(game);u.atomic(stage/'candidate.json',b'{}');u.atomic(stage/'payload.zip',b'ZIP')
    (game/'GTAIV-QoL.ini').write_text('[Updates]\nCheckForUpdates=0\n')
    self.assertFalse(u.stage_game_update(game));self.assertFalse(u.apply_game_update(game))
    fetch.assert_not_called();apply.assert_not_called();self.assertFalse((stage/'candidate.json').exists())
 def test_tampered_stage_is_rejected_and_retained_without_apply(self):
  with tempfile.TemporaryDirectory() as folder:
   game,state,config=self.fixture(folder)
   with patch.object(u,'ROOT',state),patch.object(u,'CONFIG',config),patch.object(u,'game_running',return_value=False),patch.object(u,'apply_update') as apply:
    stage=u.pending_directory(game);u.atomic(stage/'candidate.json',json.dumps({'digest':'0'*64,'channel':'beta'}).encode());u.atomic(stage/'payload.zip',b'bad')
    with self.assertRaises(RuntimeError):u.apply_game_update(game)
    apply.assert_not_called();self.assertTrue((stage/'payload.zip').exists())
 @unittest.skipIf(sys.platform=="win32","Deck systemd worker is Linux-only")
 def test_worker_is_installed_in_game_and_systemd_handles_spaces(self):
  with tempfile.TemporaryDirectory() as folder:
   game,state,config=self.fixture(folder)
   with patch.object(Path,'home',return_value=Path(folder)),patch.object(u.subprocess,'run',return_value=Mock(returncode=0)) as run:
    self.assertTrue(u.install_game_updater(game,b'verified source'))
    self.assertEqual((game/'GTAIV-QoL-Updater.py').read_bytes(),b'verified source')
    unit=(Path(folder)/'.config/systemd/user/gtaiv-qol-updates.service').read_text()
    self.assertIn('"'+str(game/'GTAIV-QoL-Updater.py')+'" --watch-game "'+str(game)+'"',unit)
    self.assertEqual(run.call_count,2)
 @unittest.skipIf(sys.platform=="win32","Deck user-service lock uses fcntl")
 def test_one_request_is_consumed_and_no_release_does_not_apply(self):
  with tempfile.TemporaryDirectory() as folder:
   game,state,config=self.fixture(folder);request=game/'GTAIV-QoL-update-request.txt';request.write_text('once')
   with patch.object(u,'ROOT',state),patch.object(u,'CONFIG',config),patch.object(u,'beta_candidate',return_value=None) as select,patch.object(u,'game_running',return_value=True),patch.object(u,'apply_update') as apply:
    self.assertFalse(u.game_update_tick(game));self.assertFalse(u.game_update_tick(game))
    select.assert_called_once();apply.assert_not_called();self.assertFalse(request.exists())
 def test_venue_runtime_cannot_call_the_unverified_accessor(self):
  source=(ROOT/'vendor/roadnav/src/compass_hud.hpp').read_text()
  body=source.split('void readDateVenues(DWORD now){',1)[1].split('\n    }',1)[0]
  self.assertNotIn('GetProcAddress',body);self.assertNotIn('address(',body)
  self.assertIn('dateTableValid_=false',body)

 def test_manual_update_since_staging_cancels_old_candidate(self):
  with tempfile.TemporaryDirectory() as folder:
   game,state,config=self.fixture(folder)
   with patch.object(u,'ROOT',state),patch.object(u,'CONFIG',config),patch.object(u,'game_running',return_value=False),patch.object(u,'apply_update') as apply:
    stage=u.pending_directory(game)
    u.atomic(stage/'candidate.json',json.dumps({'channel':'beta','installed_sha':'a'*40}).encode());u.atomic(stage/'payload.zip',b'old')
    (state/'installed.json').write_text(json.dumps({'sha':'b'*40}))
    self.assertFalse(u.apply_game_update(game));apply.assert_not_called()
    self.assertFalse((stage/'candidate.json').exists())
 def test_old_installer_schema_can_deliver_verified_worker_in_the_same_transaction(self):
  import base64,hashlib
  spec=importlib.util.spec_from_file_location('legacy_bridge',ROOT/'tools/steam-deck/enable-qol-logs.py');helper=importlib.util.module_from_spec(spec);spec.loader.exec_module(helper)
  source=b"def install_game_updater(game,source):\n (game/'worker-enabled').write_text('ready')\n"
  helper.INGAME_UPDATER={'data':base64.b64encode(source).decode(),'sha256':hashlib.sha256(source).hexdigest()}
  bundle={name:{'data':base64.b64encode(raw).decode(),'sha256':hashlib.sha256(raw).hexdigest()} for name,raw in {'GTAIV-QoL.asi':b'main','GTAIV-QoL-Pause.asi':b'pause','GTAIV-QoL.ini':b'[Updates]\nCheckForUpdates=0\n'}.items()}
  with tempfile.TemporaryDirectory() as folder:
   game=Path(folder);helper.install_bundle(game,bundle)
   self.assertEqual((game/'GTAIV-QoL-Updater.py').read_bytes(),source)
   self.assertEqual((game/'worker-enabled').read_text(),'ready')
   self.assertIn(b'CheckForUpdates=0',(game/'GTAIV-QoL.ini').read_bytes())
