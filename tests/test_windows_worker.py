"""Same shared worker state machine, adapted Windows backend; no live GitHub."""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import types
import unittest
from unittest.mock import Mock,patch
ROOT=Path(__file__).resolve().parents[1]
def load(name,path):
    spec=importlib.util.spec_from_file_location(name,ROOT/path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
adapter=load('worker_adapter','tools/windows/platform_adapter.py')

class WindowsWorker(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name);self.game=self.root/'Game with spaces';self.game.mkdir()
        (self.game/'GTAIV.exe').write_bytes(b'fixture')
        self.request=self.game/'GTAIV-QoL-update-request.txt'
        (self.game/'GTAIV-QoL.ini').write_text('[Updates]\nCheckForUpdates=1\n')
        self.u=load('worker_fixture','tools/steam-deck/qol-updater.py')
        self.env=patch.dict(os.environ,LOCALAPPDATA=str(self.root));self.env.start();self.addCleanup(self.env.stop)
        adapter.adapt(self.u,self.root/'installer.exe',Mock())
        self.u.ROOT.mkdir();self.u.CONFIG.write_text(json.dumps({'game':str(self.game),'channel':'beta'}))
        self.modules=patch.dict(sys.modules);self.modules.start();self.addCleanup(self.modules.stop)
        if sys.platform=='win32':adapter.install_lock_api()
        self.running=patch.object(self.u,'game_running',return_value=True);self.live=self.running.start();self.addCleanup(self.running.stop)
        self.started=patch.object(self.u,'game_start_time',return_value=time.time()-30);self.start=self.started.start();self.addCleanup(self.started.stop)
        self.candidate={'channel':'beta','digest':'a'*64,'route':'/fixture'}
        for name,value in [('beta_candidate',self.candidate),('request',b'ZIP'),('validate_archive',({'sha':'b'*40},{'file':b'new'}))]:
            p=patch.object(self.u,name,return_value=value);p.start();self.addCleanup(p.stop)
        p=patch.object(self.u,'apply_update');self.apply=p.start();self.addCleanup(p.stop)
    def code(self):return json.loads((self.game/'GTAIV-QoL-update-status.json').read_text())['code']
    def tick(self):return adapter.windows_tick(self.u,self.game)
    def stage(self):self.request.write_text('check');self.assertFalse(self.tick());self.assertEqual(self.code(),'staged')
    def test_checking_committed_before_request_consumed_and_stages_during_play(self):
        original=self.u.update_status
        def status(game,code,*args):
            if code=='checking':self.assertTrue(self.request.exists() or self.code()=='checking')
            return original(game,code,*args)
        with patch.object(self.u,'update_status',side_effect=status):self.stage()
        self.apply.assert_not_called();self.assertFalse(self.request.exists())
        self.assertTrue((self.u.pending_directory(self.game)/'candidate.json').exists())
        self.tick();self.apply.assert_not_called()
        self.live.return_value=False;self.assertTrue(self.tick());self.apply.assert_called_once()
        self.assertEqual(self.u.validate_archive.call_count,2);self.assertEqual(self.code(),'updated')
    def test_off_cancels_while_playing_and_removes_requests(self):
        self.stage();(self.game/'GTAIV-QoL.ini').write_text('[Updates]\nCheckForUpdates=0\n');self.request.write_text('again')
        self.tick();self.assertEqual(self.code(),'cancelled');self.assertFalse(self.request.exists());self.apply.assert_not_called()
        self.live.return_value=False;self.tick();self.apply.assert_not_called()
    def test_creation_time_rejects_previous_process_request(self):
        self.request.write_text('stale');self.start.return_value=time.time()+5
        self.tick();self.assertFalse(self.request.exists());self.u.beta_candidate.assert_not_called()
    def test_unreadable_process_time_fails_closed(self):
        self.request.write_text('check');self.start.side_effect=RuntimeError('private details')
        with self.assertRaises(RuntimeError):self.tick()
        self.assertEqual(self.code(),'failed');self.u.beta_candidate.assert_not_called()
        self.assertNotIn('private details',(self.game/'GTAIV-QoL-update-status.json').read_text())
    def test_idle_restart_cleans_request_and_busy_receipt(self):
        self.u.update_status(self.game,'downloading');self.request.write_text('stale')
        (self.game/'GTAIV-QoL-update-request.tmp').write_text('stale');self.live.return_value=False
        self.tick();self.assertEqual(self.code(),'failed');self.assertFalse(self.request.exists())
        self.assertFalse((self.game/'GTAIV-QoL-update-request.tmp').exists())
    def test_manual_install_invalidates_old_stage(self):
        self.stage();(self.u.ROOT/'installed.json').write_text(json.dumps({'sha':'c'*40}))
        self.live.return_value=False;self.tick();self.apply.assert_not_called();self.assertEqual(self.code(),'cancelled')
    def test_tampered_stage_never_applies(self):
        self.stage();self.live.return_value=False;self.u.validate_archive.side_effect=RuntimeError('tampered')
        with self.assertRaises(RuntimeError):self.tick()
        self.apply.assert_not_called();self.assertEqual(self.code(),'failed')
    def test_shared_lock_defers_manual_and_session_contention(self):
        import fcntl
        self.stage();self.live.return_value=False
        with (self.u.ROOT/'update.lock').open('a') as lock:
            fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB);self.assertFalse(self.tick());self.apply.assert_not_called()
        self.apply.side_effect=BlockingIOError('session busy')
        with self.assertRaises(BlockingIOError):self.tick()
        self.assertTrue((self.u.pending_directory(self.game)/'candidate.json').exists())
        self.apply.side_effect=None;self.assertTrue(self.tick())
    def test_worker_handoff_after_transaction_and_lock_release(self):
        exe=self.root/'frozen.exe';exe.write_bytes(b'fixture')
        self.u.atomic(self.u.ROOT/'worker-target.json',json.dumps({'exe':str(exe)}).encode())
        events=[]
        def applied(*args):events.append('applied');return True
        def spawn(target):
            import fcntl
            with (self.u.ROOT/'watch.lock').open('a') as lock:fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
            events.append('successor')
        with patch.object(adapter,'windows_tick',side_effect=applied),patch.object(adapter,'start_worker',side_effect=spawn):
            self.assertEqual(adapter.watch(self.u,exe,Mock()),0)
        self.assertEqual(events,['applied','successor'])
    def test_registration_idempotent_and_never_elevated(self):
        class Key:
            def __enter__(self):return self
            def __exit__(self,*a):pass
        registry=types.SimpleNamespace(HKEY_CURRENT_USER=1,REG_SZ=1,CreateKey=Mock(return_value=Key()),SetValueEx=Mock())
        target=self.root/'Updated worker.exe'
        adapter.register_worker(target,registry);adapter.register_worker(target,registry)
        calls=registry.SetValueEx.call_args_list;self.assertEqual(calls[0],calls[1]);self.assertIn('--watch',calls[0].args[-1])
        with patch.object(adapter.subprocess,'Popen') as spawn:adapter.start_worker(target)
        self.assertEqual(spawn.call_args.kwargs['creationflags'],adapter.NO_WINDOW)
    def test_reinstall_refreshes_frozen_code_game_and_cancels_same_sha_stage(self):
        exe=self.root/'installer.exe';exe.write_bytes(b'new frozen code')
        self.stage()
        class Key:
            def __enter__(self):return self
            def __exit__(self,*args):pass
        registry=types.SimpleNamespace(HKEY_CURRENT_USER=1,REG_SZ=1,CreateKey=Mock(return_value=Key()),SetValueEx=Mock())
        with patch.dict(sys.modules,{'winreg':registry}),patch.object(adapter,'powershell',return_value='powershell.exe'),patch.object(adapter.subprocess,'run',return_value=types.SimpleNamespace(stdout=str(self.root))),patch.object(adapter,'create_shortcut') as shortcut,patch.object(adapter,'start_worker') as spawn:
            self.u.shortcut();target=adapter.worker_target(self.u.ROOT)
            self.assertEqual(target.read_bytes(),exe.read_bytes());self.assertFalse((self.u.pending_directory(self.game)/'candidate.json').exists())
            self.assertFalse((self.u.ROOT/'stop-watcher').exists())
            self.u.shortcut();self.assertEqual(adapter.worker_target(self.u.ROOT),target)
            self.assertEqual(spawn.call_count,2);self.assertEqual(shortcut.call_count,2)
            exe.write_bytes(b'next frozen code');self.u.shortcut()
            self.assertNotEqual(adapter.worker_target(self.u.ROOT),target);self.assertEqual(target.read_bytes(),b'new frozen code')
        other=self.root/'different game';other.mkdir()
        self.u.CONFIG.write_text(json.dumps({'game':str(other),'channel':'beta'}))
        self.assertEqual(self.u.selected_game(),other.resolve())
    def test_legacy_stop_signal_and_singleton_prevent_duplicate_worker(self):
        import fcntl
        exe=self.root/'worker.exe';exe.write_bytes(b'fixture')
        self.u.atomic(self.u.ROOT/'worker-target.json',json.dumps({'exe':str(exe)}).encode())
        with (self.u.ROOT/'watch.lock').open('a') as lock:
            fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
            with patch.object(adapter,'windows_tick') as tick:
                self.assertEqual(adapter.watch(self.u,exe,Mock()),0);tick.assert_not_called()
        (self.u.ROOT/'stop-watcher').touch()
        with patch.object(adapter,'start_worker') as spawn,patch.object(adapter,'windows_tick') as tick:
            self.assertEqual(adapter.watch(self.u,exe,Mock()),0);spawn.assert_not_called();tick.assert_not_called()
        adapter.stop_worker_for_refresh(self.u.ROOT)
        self.assertFalse((self.u.ROOT/'stop-watcher').exists())
    @unittest.skipUnless(sys.platform=='win32','Real Windows process creation-time API')
    def test_real_windows_process_creation_time(self):
        # Exercise GetProcessTimes against an actual process,
        # without needing GTA or Python on the user's machine.
        before=time.time();child=subprocess.Popen([sys.executable,'-c','import time; time.sleep(30)'],creationflags=adapter.NO_WINDOW)
        self.addCleanup(lambda:child.poll() is None and child.kill())
        with patch.object(adapter,'running_pid',return_value=child.pid):
            created=adapter.game_start_time()
        self.assertGreaterEqual(created,before-2);self.assertLessEqual(created,time.time())
        child.terminate();child.wait(timeout=10)
