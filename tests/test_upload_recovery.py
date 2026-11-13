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
                    if route=='/issues':return {'number':11,'state':'open','locked':False,'comments':0}
                    self.assertEqual(route,'/issues/11/comments');posts.append(payload['body']);return {'id':99,'body':payload['body']}
                gets.append(route)
                if route=='':return {'private':True}
                return [{'body':'<!-- roadnav-session:'+first[2]['id']+' -->'}] if route.startswith('/issues/2/comments') else []
            ns=self.fixture(root,api);config={'issue':2,'token':'fixture token'}
            with patch('time.sleep'):
                ns['drain'](config)
            self.assertEqual(len(posts),1);self.assertIn(second[2]['id'],posts[0]);self.assertEqual(len(gets),6);self.assertEqual(config['issue'],11);self.assertEqual(config['issue_date'],helper.diagnostics_issue_day())
            for path,data,_ in [first,second]:
                self.assertFalse(path.exists());self.assertEqual((root/'delivered'/path.name).read_bytes(),data)

    def test_ambiguous_post_stays_pending_and_next_retry_deduplicates(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);(root/'pending').mkdir();path,raw,item=self.enqueue(root,3);remote=[];posts=[]
            def api(token,route,payload=None):
                if payload:
                    if route=='/issues':return {'number':12,'state':'open','locked':False,'comments':0}
                    posts.append(payload);remote.append({'body':payload['body']});raise TimeoutError('simulated lost reply')
                if route=='':return {'private':True}
                if route=='/issues/12':return {'number':12,'state':'open','locked':False,'comments':0}
                return remote if '/comments' in route else []
            ns=self.fixture(root,api);config={'issue':9,'token':'fixture'}
            ns['drain'](config);self.assertEqual(path.read_bytes(),raw)
            ns['drain'](config);self.assertEqual(len(posts),1);self.assertEqual((root/'delivered'/path.name).read_bytes(),raw)

    def test_failed_or_unconfirmed_upload_never_removes_pending(self):
        for response in ({}, {'id':99,'body':'unrelated'}):
            with tempfile.TemporaryDirectory() as tmp:
                root=Path(tmp);(root/'pending').mkdir();path,raw,_=self.enqueue(root,4)
                def api(token,route,payload=None):
                    if payload:
                        if route=='/issues':return {'number':13,'state':'open','locked':False,'comments':0}
                        return response
                    if route=='':return {'private':True}
                    return []
                self.fixture(root,api)['drain']({'issue':9,'token':'fixture'})
                self.assertEqual(path.read_bytes(),raw)

    def test_bounded_batch_preserves_backlog_and_prioritizes_summary(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);(root/'pending').mkdir();all_files=[self.enqueue(root,n) for n in range(10,32)]
            summary=self.enqueue(root,32,'0000-nav-');all_files.append(summary);police=self.enqueue(root,33,'0000-police-');all_files.append(police);posts=[]
            def api(token,route,payload=None):
                if payload:
                    if route=='/issues':return {'number':14,'state':'open','locked':False,'comments':0}
                    posts.append(payload['body']);return {'id':len(posts),'body':payload['body']}
                if route=='':return {'private':True}
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
                if payload:
                    if path=='/issues':return {'number':14,'state':'open','locked':False,'comments':0}
                    posts.append(payload['body']);return {'id':len(posts),'body':payload['body']}
                if path=='':return {'private':True}
                return []
            ns=self.fixture(root,api);ns['body']=lambda value:'<!-- roadnav-session:'+value['id']+' -->\n'+next(iter(value['logs'].values()))
            with patch('time.sleep'):ns['drain']({'issue':10,'token':'fixture'})
            self.assertIn('original route evidence',posts[0]);self.assertEqual((root/'delivered'/route.name).read_bytes(),raw)

    def test_config_migration_preserves_authorization_and_retires_old_issue(self):
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'config.json';original={'issue':2,'token':'private fixture','game':'/fixture/game','extra':True};path.write_text(json.dumps(original));raw=path.read_bytes()
            helper.migrate_upload_destination(path,original)
            self.assertEqual(json.loads(path.read_text()),dict(original,issue=None,issue_date=None));self.assertIsNone(original['issue']);self.assertIsNone(original['issue_date'])
            backup=path.with_name('config.before-log-issue-2.json');self.assertEqual(backup.read_bytes(),raw)
            with self.assertRaisesRegex(RuntimeError,'Unrecognized RoadNav log issue'):
                helper.migrate_upload_destination(path,original)
            self.assertEqual(backup.read_bytes(),raw)
            if sys.platform!='win32':self.assertEqual(path.stat().st_mode&0o777,0o600)

    def test_full_current_day_issue_opens_fresh_issue_and_flushes_pending(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);(root/'pending').mkdir();path,data,_=self.enqueue(root,50);posts=[];today=helper.diagnostics_issue_day()
            def api(token,route,payload=None):
                if payload:
                    if route=='/issues':return {'number':31,'state':'open','locked':False,'comments':0}
                    posts.append(payload['body']);return {'id':1,'body':payload['body']}
                if route=='':return {'private':True}
                if route=='/issues/10':return {'number':10,'state':'open','locked':False,'comments':2500}
                return []
            config={'issue':10,'issue_date':today,'token':'fixture token'}
            with patch('time.sleep'):
                self.fixture(root,api)['drain'](config)
            self.assertEqual(len(posts),1);self.assertEqual(config['issue'],31);self.assertEqual(config['issue_date'],today)
            self.assertFalse(path.exists());self.assertEqual((root/'delivered'/path.name).read_bytes(),data)

    def test_resolve_reuses_usable_current_day_issue(self):
        today=helper.diagnostics_issue_day();calls=[]
        def api(token,path,payload=None):
            calls.append(path)
            if path=='/issues/7':return {'number':7,'state':'open','locked':False,'comments':100}
            return []
        config={'issue':7,'issue_date':today,'token':'fixture'}
        self.assertEqual(helper.resolve_diagnostics_issue(api,'fixture',config),7)
        self.assertEqual(calls,['/issues/7']);self.assertEqual(config,{'issue':7,'issue_date':today,'token':'fixture'})

    def test_resolve_opens_new_issue_when_current_day_issue_is_full(self):
        today=helper.diagnostics_issue_day();title='QoL diagnostics — '+today;created=[]
        def api(token,path,payload=None):
            if payload:
                created.append(payload);return {'number':22,'state':'open','locked':False,'comments':0}
            if path=='/issues/7':return {'number':7,'state':'open','locked':False,'comments':2500}
            if path.startswith('/issues?'):return [{'number':8,'state':'open','locked':False,'comments':2500,'title':title}]
            return []
        config={'issue':7,'issue_date':today,'token':'fixture'}
        self.assertEqual(helper.resolve_diagnostics_issue(api,'fixture',config),22)
        self.assertEqual((config['issue'],config['issue_date']),(22,today))
        self.assertEqual(created[0]['title'],title);self.assertIn('Daily automatic diagnostics',created[0]['body'])

    def test_resolve_reuses_open_issue_with_todays_title(self):
        today=helper.diagnostics_issue_day();title='QoL diagnostics — '+today
        def api(token,path,payload=None):
            if payload:self.fail('a reusable issue already exists for today')
            if path=='/issues/7':return {'number':7,'state':'open','locked':True}
            if path.startswith('/issues?'):return [{'number':23,'state':'open','locked':False,'comments':3,'title':title}]
            return []
        config={'issue':7,'issue_date':today,'token':'fixture'}
        self.assertEqual(helper.resolve_diagnostics_issue(api,'fixture',config),23)
        self.assertEqual((config['issue'],config['issue_date']),(23,today))

    def test_resolve_creates_issue_when_current_is_unreachable(self):
        today=helper.diagnostics_issue_day();created=[]
        def api(token,path,payload=None):
            if payload:
                created.append(payload);return {'number':24,'state':'open','locked':False,'comments':0}
            if path=='/issues/7':raise ConnectionError('unreachable')
            return []
        config={'issue':7,'issue_date':today,'token':'fixture'}
        self.assertEqual(helper.resolve_diagnostics_issue(api,'fixture',config),24)
        self.assertEqual((config['issue'],config['issue_date']),(24,today))
        self.assertEqual(created[0]['title'],'QoL diagnostics — '+today)

    def test_config_migration_leaves_current_day_destination_alone(self):
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'config.json';today=helper.diagnostics_issue_day()
            original={'issue':21,'issue_date':today,'token':'private fixture'}
            path.write_text(json.dumps(original));raw=path.read_bytes()
            helper.migrate_upload_destination(path,original)
            self.assertEqual(path.read_bytes(),raw);self.assertEqual(original,{'issue':21,'issue_date':today,'token':'private fixture'})
            self.assertFalse(path.with_name('config.before-log-issue-21.json').exists())

    def test_save_upload_config_writes_atomically_with_restricted_mode(self):
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'config.json';today=helper.diagnostics_issue_day()
            path.write_text(json.dumps({'issue':7}))
            helper.save_upload_config(path,{'issue':7,'issue_date':today})
            self.assertEqual(json.loads(path.read_text()),{'issue':7,'issue_date':today})
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
