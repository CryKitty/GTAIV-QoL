import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch, Mock

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('recovery_helper',ROOT/'tools/steam-deck/enable-qol-logs.py')
helper=importlib.util.module_from_spec(spec);spec.loader.exec_module(helper)

class RecoveryTests(unittest.TestCase):
    def setUp(self):
        try:import fcntl
        except ImportError:
            lock_patch=patch.dict(sys.modules,{'fcntl':Mock(LOCK_EX=2,LOCK_NB=4)})
            lock_patch.start();self.addCleanup(lock_patch.stop)

    def fixture(self,root,api):
        ns={'ROOT':root,'json':json,'sys':sys,'snapshot':lambda game:{},'api':api,'body':lambda item:'<!-- roadnav-session:'+item['id']+' -->\n'+item['logs']['probe.log']}
        exec(helper.EXTENSION,ns)
        return ns

    def enqueue(self,root,n,prefix=''):
        item={'id':('%032x'%n),'logs':{'probe.log':'Police search probe: fixture '+str(n)}}
        path=root/'pending'/(prefix+item['id']+'.json');path.write_text(json.dumps(item));return path,path.read_bytes(),item

    def test_reconcile_old_issue_and_archive_exact_originals(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);(root/'pending').mkdir()
            first=self.enqueue(root,1);second=self.enqueue(root,2,'0000-nav-');posts=[];gets=[]
            def api(token,route,payload=None):
                self.assertEqual(token,'fixture token')
                if payload:
                    self.assertEqual(route,'/issues/10/comments');posts.append(payload['body']);return {'id':99,'body':payload['body']}
                gets.append(route)
                if route=='':return {'private':True}
                if route=='/issues/10':return {'state':'open','locked':False}
                return [{'body':'<!-- roadnav-session:'+first[2]['id']+' -->'}] if route.startswith('/issues/2/comments') else []
            ns=self.fixture(root,api)
            with patch('time.sleep'):
                ns['drain']({'issue':2,'token':'fixture token'})
            self.assertEqual(len(posts),1);self.assertIn(second[2]['id'],posts[0]);self.assertEqual(len(gets),5)
            for path,data,_ in [first,second]:
                self.assertFalse(path.exists());self.assertEqual((root/'delivered'/path.name).read_bytes(),data)

    def test_ambiguous_post_stays_pending_and_next_retry_deduplicates(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);(root/'pending').mkdir();path,raw,item=self.enqueue(root,3);remote=[];posts=[]
            def api(token,route,payload=None):
                if payload:
                    posts.append(payload);remote.append({'body':payload['body']});raise TimeoutError('simulated lost reply')
                if route=='':return {'private':True}
                if route=='/issues/10':return {'state':'open','locked':False}
                return remote if route.startswith('/issues/10/comments') else []
            ns=self.fixture(root,api);config={'issue':9,'token':'fixture'}
            ns['drain'](config);self.assertEqual(path.read_bytes(),raw)
            ns['drain'](config);self.assertEqual(len(posts),1);self.assertEqual((root/'delivered'/path.name).read_bytes(),raw)

    def test_failed_or_unconfirmed_upload_never_removes_pending(self):
        for response in ({}, {'id':99,'body':'unrelated'}):
            with tempfile.TemporaryDirectory() as tmp:
                root=Path(tmp);(root/'pending').mkdir();path,raw,_=self.enqueue(root,4)
                def api(token,route,payload=None):
                    if payload:return response
                    if route=='':return {'private':True}
                    if route=='/issues/10':return {'state':'open','locked':False}
                    return []
                self.fixture(root,api)['drain']({'issue':9,'token':'fixture'})
                self.assertEqual(path.read_bytes(),raw)

    def test_bounded_batch_preserves_backlog_and_prioritizes_summary(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);(root/'pending').mkdir();all_files=[self.enqueue(root,n) for n in range(10,32)]
            summary=self.enqueue(root,32,'0000-nav-');all_files.append(summary);police=self.enqueue(root,33,'0000-police-');all_files.append(police);posts=[]
            def api(token,route,payload=None):
                if payload:posts.append(payload['body']);return {'id':len(posts),'body':payload['body']}
                if route=='':return {'private':True}
                if route=='/issues/10':return {'state':'open','locked':False}
                return []
            with patch('time.sleep'):
                self.fixture(root,api)['drain']({'issue':9,'token':'fixture'})
            self.assertIn(police[2]['id'],posts[0]);self.assertIn(summary[2]['id'],posts[1]);self.assertEqual(len(posts),20)
            self.assertEqual(len(list((root/'pending').glob('*.json'))),4)
            for path,raw,_ in all_files:
                actual=path if path.exists() else root/'delivered'/path.name
                self.assertEqual(actual.read_bytes(),raw)

    def test_queued_route_log_precedes_third_party_backlog_without_rewriting(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);(root/'pending').mkdir();route,_,item=self.enqueue(root,100)
            item['logs']={'RoadNav-arrow.log':'original route evidence'};route.write_text(json.dumps(item));raw=route.read_bytes()
            for n in range(101,130):self.enqueue(root,n)
            posts=[]
            def api(token,path,payload=None):
                if payload:posts.append(payload['body']);return {'id':len(posts),'body':payload['body']}
                if path=='':return {'private':True}
                if path=='/issues/10':return {'state':'open','locked':False}
                return []
            ns=self.fixture(root,api);ns['body']=lambda value:'<!-- roadnav-session:'+value['id']+' -->\n'+next(iter(value['logs'].values()))
            with patch('time.sleep'):ns['drain']({'issue':10,'token':'fixture'})
            self.assertIn('original route evidence',posts[0]);self.assertEqual((root/'delivered'/route.name).read_bytes(),raw)

    def test_config_migration_preserves_authorization_and_is_repeatable(self):
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'config.json';original={'issue':2,'token':'private fixture','game':'/fixture/game','extra':True};path.write_text(json.dumps(original));raw=path.read_bytes()
            helper.migrate_upload_destination(path,original)
            self.assertEqual(json.loads(path.read_text()),dict(original,issue=10));self.assertEqual(original['issue'],10)
            backup=path.with_name('config.before-log-issue-10.json');self.assertEqual(backup.read_bytes(),raw)
            helper.migrate_upload_destination(path,original);self.assertEqual(backup.read_bytes(),raw)
            if sys.platform!='win32':self.assertEqual(path.stat().st_mode&0o777,0o600)

class RepairLockTests(unittest.TestCase):
    def test_busy_service_is_paused_and_restarted_even_if_installation_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);pending=root/'pending';pending.mkdir();queued=pending/'session.json';queued.write_bytes(b'exact queued diagnostics')
            locks=Mock(LOCK_EX=2,LOCK_NB=4);locks.flock.side_effect=[BlockingIOError(),None]
            completed=Mock(returncode=0)
            with patch.dict(sys.modules,{'fcntl':locks}), patch.object(helper.subprocess,'run',return_value=completed) as run:
                with self.assertRaisesRegex(ValueError,'fixture install failure'):
                    with helper.upload_repair_lock(root):raise ValueError('fixture install failure')
            commands=[c.args[0] for c in run.call_args_list]
            self.assertEqual(commands,[
                ['systemctl','--user','is-active','--quiet','roadnav-upload.timer'],
                ['systemctl','--user','stop','roadnav-upload.timer','roadnav-upload.service'],
                ['systemctl','--user','--no-block','start','roadnav-upload.timer','roadnav-upload.service']])
            self.assertEqual(queued.read_bytes(),b'exact queued diagnostics')
            self.assertTrue((root/'upload.lock').exists())

    def test_timeout_restores_retries_and_preserves_pending(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);queued=root/'keep.json';queued.write_bytes(b'keep')
            locks=Mock(LOCK_EX=2,LOCK_NB=4);locks.flock.side_effect=BlockingIOError()
            sequence=[Mock(returncode=0),helper.subprocess.TimeoutExpired('systemctl',15),Mock(returncode=0)]
            with patch.dict(sys.modules,{'fcntl':locks}),patch.object(helper.subprocess,'run',side_effect=sequence) as run:
                with self.assertRaisesRegex(RuntimeError,'in time'):
                    with helper.upload_repair_lock(root):self.fail('Must not bypass lock after timeout')
            self.assertIn('start',run.call_args.args[0]);self.assertEqual(queued.read_bytes(),b'keep')

    @unittest.skipIf(sys.platform=='win32','real Linux/Unix flock regression')
    def test_actual_busy_os_lock_released_by_service_stop_and_closed_before_restart(self):
        import fcntl
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);path=root/'upload.lock'
            with path.open('a') as service_lock:
                fcntl.flock(service_lock,fcntl.LOCK_EX | fcntl.LOCK_NB)
                inode=path.stat().st_ino
                def systemctl(command,**kwargs):
                    if 'stop' in command:fcntl.flock(service_lock,fcntl.LOCK_UN)
                    if 'start' in command:
                        with path.open('a') as probe:fcntl.flock(probe,fcntl.LOCK_EX | fcntl.LOCK_NB)
                    return Mock(returncode=0)
                with patch.object(helper.subprocess,'run',side_effect=systemctl):
                    with helper.upload_repair_lock(root):
                        with self.assertRaises(BlockingIOError):fcntl.flock(service_lock,fcntl.LOCK_EX | fcntl.LOCK_NB)
                self.assertEqual(path.stat().st_ino,inode)

if __name__=='__main__':unittest.main()
