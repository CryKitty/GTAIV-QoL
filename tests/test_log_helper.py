import importlib.util
from pathlib import Path
import tempfile
import unittest
import json
import hashlib

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('helper', ROOT/'tools/steam-deck/enable-qol-logs.py')
helper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)
FIXTURE = """NAMES = ('RoadNav.log', 'RoadNav-arrow.log')
API = 'https://api.github.com/repos/CryKitty/GTAIV-Road-Nav'
def snapshot(game):
    return {}
def enqueue(game, before, code):
    pass
def atomic(path, item):
    path.write_text(json.dumps(item))
def drain(config):
    pass
if __name__ == '__main__':
    pass
"""

class HelperTests(unittest.TestCase):
    def test_individual_line_choices_survive_updates_and_new_installs_default_on(self):
        keys=['ShowPoliceEscape','ShowMission','ShowFriend','ShowEnemy','ShowCustom']
        fresh=(ROOT/'GTAIV-QoL.ini.example').read_bytes()
        import configparser
        def values(data):
            ini=configparser.ConfigParser();ini.read_string(data.decode());return [ini.getint('RoadGPSLine',key) for key in keys]
        self.assertEqual(values(fresh),[1]*5)
        for off in range(5):
            old=('[RoadGPSLine]\n'+''.join('%s=%d\n'%(key,int(i!=off)) for i,key in enumerate(keys))).encode()
            expected=[int(i!=off) for i in range(5)]
            self.assertEqual(values(helper.merge_preferences(old,fresh)),expected)

    def test_cutscene_output_migration_does_not_activate_old_unused_boost(self):
        fresh=b'[VoiceVolume]\nOutputMixVersion=1\nCutscenePercent=100\n[Camera]\nTaxiRearFOV=70\n'
        self.assertIn(b'TaxiRearFOV=120',helper.merge_preferences(b'[Camera]\nTaxiFovSliderVersion=1\nTaxiRearFOV=120\n',fresh))
        old=b'[VoiceVolume]\nCutscenePercent=800\n[Camera]\nTaxiRearFOV=80\n'
        merged=helper.merge_preferences(old,fresh)
        self.assertIn(b'CutscenePercent=100',merged)
        self.assertIn(b'TaxiRearFOV=70',merged)
        self.assertIn(b'TaxiRearFOV=80',helper.merge_preferences(b'[Camera]\nTaxiFovSliderVersion=1\nTaxiRearFOV=80\n',fresh))
        self.assertIn(b'CutscenePercent=800',helper.merge_preferences(b'[VoiceVolume]\nOutputMixVersion=1\nCutscenePercent=800\n',fresh))

    def test_upgrade_and_idempotence(self):
        for source in (FIXTURE, FIXTURE.replace("'RoadNav-arrow.log')", "'RoadNav-arrow.log', 'GTAIV-QoL.log')")):
            result = helper.extend(source)
            self.assertIn("'GTAIV-QoL-diagnostics.log'", result)
            self.assertEqual(helper.extend(result), result)
            upgraded=helper.extend(result.replace('QOL_FULL_CAPTURE_V12','QOL_FULL_CAPTURE_V8'))
            self.assertEqual(upgraded.count('# QOL_FULL_CAPTURE_V12'),1)
            self.assertNotIn('# QOL_FULL_CAPTURE_V8',upgraded)
            self.assertLess(result.index('# QOL_FULL_CAPTURE_V12'), result.index("if __name__ == '__main__':"))

    def test_complete_multichunk_capture_and_unchanged_suppression(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); (root/'pending').mkdir()
            text = ('voice slider event\nvoice slider CRLF\r\n' * 7500) + 'FINAL_EVENT'
            (root/'GTAIV-QoL-diagnostics.log').write_bytes(text.encode('utf-8'))
            (root/'GTAIV-QoL.asi').write_bytes(b'fixture')
            ns = {'__name__': 'fixture', 'json': json, 'hashlib': hashlib, 'ROOT': root}
            exec(helper.extend(FIXTURE), ns)
            ns['enqueue'](root, {}, 0)
            items = [json.loads(p.read_text()) for p in (root/'pending').glob('*.json')]
            items.sort(key=lambda x: int(x['exit'].split(' part ')[1].split('/')[0]))
            self.assertGreater(len(items), 1)
            self.assertEqual(''.join(x['logs']['GTAIV-QoL-diagnostics.log'] for x in items), text)
            for item in items:
                rendered = '\n'.join('    '+line for line in next(iter(item['logs'].values())).splitlines())
                self.assertLess(len(rendered), 45000)
            before=ns['snapshot'](root); ns['enqueue'](root,before,0)
            self.assertEqual(len(list((root/'pending').glob('*.json'))), len(items))

    def test_navigation_summary_precedes_full_logs(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);(root/'pending').mkdir()
            (root/'RoadNav-arrow.log').write_text('RoadNav vOLD\nCompass mission brief: OLD\nRoadNav vNEW\nCompass mission brief: CURRENT\nFork scan: accepted=1\nCompass minimap: Radar Off=active preference-reads=4\nPolice escape: route committed\n'+'unrelated detail\n'*650)
            ns={'__name__':'fixture','ROOT':root,'hashlib':hashlib,'json':json};exec(helper.extend(FIXTURE),ns)
            ns['enqueue'](root,{},0)
            queued=sorted((root/'pending').glob('*.json'))
            first=json.loads(queued[0].read_text())
            self.assertEqual(list(first['logs']),['QoL-session-summary.txt'])
            summary=first['logs']['QoL-session-summary.txt']
            self.assertIn('CURRENT',summary);self.assertNotIn('brief: OLD',summary)
            self.assertIn('Radar Off=active preference-reads=4',summary)
            self.assertIn('Police escape: route committed',summary)
            self.assertTrue(any('RoadNav-arrow.log' in json.loads(item.read_text())['logs'] for item in queued))

    def test_escape_diagnostics_survive_later_idle_hud_records(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);(root/'pending').mkdir()
            (root/'RoadNav-arrow.log').write_text('RoadNav vNEW\nCompass radar resolver: tables=0 entries=1 queues=1 candidates=1\nCompass radar code: entry=12345678 +000 c3\nPolice escape: GPS request timed out; points=0\n'+'Compass minimap: active\n'*800)
            ns={'__name__':'fixture','ROOT':root,'hashlib':hashlib,'json':json};exec(helper.extend(FIXTURE),ns)
            ns['enqueue'](root,{},0)
            summary=json.loads(next((root/'pending').glob('0000-nav-*')).read_text())['logs']['QoL-session-summary.txt']
            self.assertIn('GPS request timed out; points=0',summary)
            self.assertIn('tables=0 entries=1 queues=1 candidates=1',summary)
            self.assertIn('entry=12345678 +000 c3',summary)

    def test_dedicated_police_capture_is_included_and_prioritized(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);(root/'pending').mkdir()
            (root/'GTAIV-QoL-police.log').write_text('Police detail: code exact fixture')
            ns={'__name__':'fixture','ROOT':root,'hashlib':hashlib,'json':json};exec(helper.extend(FIXTURE),ns)
            ns['enqueue'](root,{},0)
            files=list((root/'pending').glob('*.json'));self.assertEqual(len(files),1)
            self.assertTrue(files[0].name.startswith('0000-police-'))
            self.assertEqual(json.loads(files[0].read_text())['logs']['GTAIV-QoL-police.log'],'Police detail: code exact fixture')

    def test_symlinks_excluded(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp); (root/'outside').write_text('secret')
            (root/'GTAIV-QoL-diagnostics.log').symlink_to(root/'outside')
            ns={'__name__':'fixture'};exec(helper.extend(FIXTURE),ns)
            self.assertNotIn('GTAIV-QoL-diagnostics.log',ns['snapshot'](root))

    def test_v3_upgrade_includes_appended_fault_tail(self):
        names = "NAMES = ('RoadNav.log', 'RoadNav-arrow.log', 'GTAIV-QoL.log', 'GTAIV-QoL-diagnostics.log', 'GTAIV-QoL-pause.log', 'GTAIV-QoL-audio-layout.log')"
        source = FIXTURE.replace("NAMES = ('RoadNav.log', 'RoadNav-arrow.log')", names)
        source = source.replace("if __name__ == '__main__':", "# QOL_FULL_CAPTURE_V3\ndef obsolete_override(): pass\nif __name__ == '__main__':")
        upgraded = helper.extend(source)
        self.assertNotIn('obsolete_override', upgraded)
        self.assertEqual(upgraded.count('_qol_original_snapshot = snapshot'), 1)
        self.assertEqual(helper.extend(upgraded), upgraded)
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root/'RoadNav-arrow.log').write_text('x' * 1200000 + '\nQoL passive fault EIP_FINAL')
            ns = {'__name__': 'fixture'}
            exec(upgraded, ns)
            capture = ns['snapshot'](root)['RoadNav-arrow.log']['text']
            self.assertTrue(capture.startswith('[Tail capture:'))
            self.assertTrue(capture.endswith('EIP_FINAL'))
            self.assertLess(len(capture), 1060000)

    def test_compass_off_preference_survives_update(self):
        fresh = b'[Compass]\nEnabled=1\nFallbackRadiusMeters=300\nMetricDistances=0\n'
        merged = helper.merge_preferences(b'[Compass]\nEnabled=0\nFallbackRadiusMeters=250\nMetricDistances=1\n', fresh)
        self.assertIn(b'Enabled=0', merged)
        self.assertIn(b'FallbackRadiusMeters=250', merged)
        self.assertIn(b'MetricDistances=1', merged)

    def test_chase_guidance_opt_out_survives_update(self):
        merged=helper.merge_preferences(b'[RoadNav]\nChaseGuidance=0\n', b'[RoadNav]\nEnabled=1\nChaseGuidance=1\n')
        self.assertIn(b'ChaseGuidance=0',merged)

    def test_view_bend_preferences_survive_update(self):
        fresh = b'[RoadGPSLine]\nEnabled=0\nViewBendMenuVersion=2\nViewBendStrengthPercent=0\nViewBendMaxDegrees=75\nViewBendResponsePercent=100\n'
        for strength in (0, 35, 100):
            old = ('[RoadGPSLine]\nEnabled=1\nViewBendMenuVersion=2\nViewBendStrengthPercent=%d\nViewBendMaxDegrees=60\nViewBendResponsePercent=250\n' % strength).encode()
            merged = helper.merge_preferences(old, fresh)
            self.assertEqual(merged, old)
            self.assertEqual(helper.merge_preferences(merged, fresh), merged)
        self.assertEqual(helper.merge_preferences(b'[RoadGPSLine]\nEnabled=1\n', fresh), fresh.replace(b'Enabled=0', b'Enabled=1'))
        for key, value in (('ViewBendStrengthPercent', '-1'), ('ViewBendStrengthPercent', '101'),
                           ('ViewBendMaxDegrees', '86'), ('ViewBendResponsePercent', '24'),
                           ('ViewBendResponsePercent', '401'), ('ViewBendStrengthPercent', 'invalid')):
            with self.subTest(key=key, value=value):
                self.assertEqual(helper.merge_preferences(('[RoadGPSLine]\n%s=%s\n' % (key, value)).encode(), fresh), fresh)

    def test_updater_delivers_working_road_line_defaults_and_preserves_menu_modes(self):
        import configparser
        fresh = (ROOT/'GTAIV-QoL.ini.example').read_bytes()
        def settings(raw):
            ini=configparser.ConfigParser();ini.read_string(raw.decode());return ini['RoadGPSLine']
        shipped=settings(fresh)
        self.assertEqual((shipped.getint('Enabled'),shipped.getint('ViewBendStrengthPercent')),(1,100))
        # Existing enabled flat line adopts the now-accessible twist on first
        # update. The previously inaccessible zero is not a menu Flat choice.
        legacy=b'[RoadGPSLine]\nEnabled=1\nViewBendStrengthPercent=0\nViewBendMaxDegrees=0\nViewBendResponsePercent=100\n'
        updated=helper.merge_preferences(legacy,fresh)
        self.assertEqual((settings(updated).getint('Enabled'),settings(updated).getint('ViewBendStrengthPercent'),settings(updated).getint('ViewBendMaxDegrees')),(1,100,75))
        # An explicit visibility Off still survives; the player can recover from
        # the pause menu alone. Legacy Flat/Twisted both become On on repeats.
        for enabled,strength in ((0,100),(1,0),(1,100)):
            old=('[RoadGPSLine]\nViewBendMenuVersion=1\nEnabled=%d\nViewBendStrengthPercent=%d\n' % (enabled,strength)).encode()
            merged=helper.merge_preferences(old,fresh)
            for _ in range(2):merged=helper.merge_preferences(merged,fresh)
            self.assertEqual((settings(merged).getint('Enabled'),settings(merged).getint('ViewBendStrengthPercent')),(enabled,100))
        self.assertEqual(settings(helper.merge_preferences(b'[RoadGPSLine]\nEnabled=0\n',fresh)).getint('Enabled'),0)

    def test_unknown_version_is_rejected(self):
        with self.assertRaises(RuntimeError): helper.extend("NAMES = ('other.log',)\n")

    def test_installer_replaces_and_backs_up_duplicates(self):
        import base64
        with tempfile.TemporaryDirectory() as tmp:
            game=Path(tmp);(game/'plugins').mkdir()
            (game/'GTAIV-QoL.asi').write_bytes(b'old')
            (game/'plugins/GTAIV-QoL.asi').write_bytes(b'duplicate')
            files={name:{'data':base64.b64encode(value).decode(),'sha256':hashlib.sha256(value).hexdigest()} for name,value in {'GTAIV-QoL.asi':b'new','GTAIV-QoL-Pause.asi':b'pause','GTAIV-QoL.ini':b'ZoomPercent=300'}.items()}
            helper.install_bundle(game,files)
            self.assertEqual((game/'GTAIV-QoL.asi').read_bytes(),b'new')
            self.assertFalse((game/'plugins/GTAIV-QoL.asi').exists())
            self.assertEqual(len(list(game.rglob('*.before-qol-bundle-*'))),2)
            self.assertEqual((game/'GTAIV-QoL-Pause.asi').read_bytes(),b'pause')
            files['GTAIV-QoL.asi']['sha256']='bad'
            with self.assertRaises(RuntimeError):helper.install_bundle(game,files)
            self.assertEqual((game/'GTAIV-QoL.asi').read_bytes(),b'new')

    def test_v1_upgrade_retains_one_capture_override(self):
        # A real installed V1 source has the diagnostics name and an override block.
        source=FIXTURE.replace("'RoadNav-arrow.log')", "'RoadNav-arrow.log', 'GTAIV-QoL.log', 'GTAIV-QoL-diagnostics.log')")
        source=source.replace("if __name__ == '__main__':", "# QOL_FULL_CAPTURE_V1\ndef old_override(): pass\nif __name__ == '__main__':")
        result=helper.extend(source)
        self.assertNotIn('def old_override',result)
        self.assertIn("'GTAIV-QoL-pause.log'",result)
        self.assertEqual(result.count('_qol_original_snapshot = snapshot'),1)
        self.assertEqual(helper.extend(result),result)

    def test_installer_never_contains_credentials(self):
        source=(ROOT/'tools/steam-deck/enable-qol-logs.py').read_text()
        for secret in ('github_pat_', 'ghp_', "config['token'] ="):
            self.assertNotIn(secret,source)

if __name__ == '__main__':
    unittest.main()
