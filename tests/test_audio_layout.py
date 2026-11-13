import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('audio_helper', ROOT/'tools/steam-deck/enable-qol-logs.py')
helper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)


def dat(objects):
    data, index = b'', b''
    for name, body in objects:
        encoded = name.encode('ascii')
        index += bytes([len(encoded)]) + encoded + struct.pack('<II', len(data), len(body))
        data += body
    return struct.pack('<II', 15, len(data)) + data + struct.pack('<IIII', 4, 0, len(objects), 0) + index


class AudioLayoutTests(unittest.TestCase):
    def test_installer_collects_and_queues_without_launching_game(self):
        with tempfile.TemporaryDirectory() as tmp:
            home = Path(tmp); game = home/'game'; game.mkdir()
            (game/'GTAIV.exe').write_bytes(b'do not upload this')
            uploader = home/'.local/share/roadnav-uploader/roadnav-upload.py'
            uploader.parent.mkdir(parents=True)
            uploader.write_text("""import json, hashlib
from pathlib import Path
ROOT = Path(__file__).parent
NAMES = ('RoadNav.log', 'RoadNav-arrow.log')
API = 'https://api.github.com/repos/CryKitty/GTAIV-Road-Nav'
def snapshot(game): return {}
def enqueue(game, before, code): pass
def atomic(path, item): path.write_text(json.dumps(item))
def drain(config): pass
def api(token, path): return {'state':'open','locked':False} if path=='/issues/10' else {'private': True}
if __name__ == '__main__': pass
""")
            config = home/'.config/roadnav-uploader/config.json'
            config.parent.mkdir(parents=True)
            config.write_text(json.dumps({'game': str(game), 'issue': 10, 'issue_date': helper.diagnostics_issue_day(), 'token': 'fixture-secret'}))
            # The installer targets Linux; CI also exercises it on Windows.
            # Stub only the OS lock boundary, retaining capture and real queue writes.
            lock_api = mock.Mock(LOCK_EX=2, LOCK_NB=4)
            with mock.patch.object(Path, 'home', return_value=home), \
                    mock.patch.dict('sys.modules', {'fcntl': lock_api}), \
                    mock.patch.object(helper, 'dialog'), mock.patch.object(helper.subprocess, 'run') as run:
                helper.main()
                self.assertEqual(run.call_args.args[0], ['systemctl', '--user', '--no-block', 'start', 'roadnav-upload.service'])
                self.assertEqual(run.call_args.kwargs['timeout'],5)
                self.assertEqual(lock_api.flock.call_count,2)
            self.assertEqual(json.loads(config.read_text())['issue'],10)
            self.assertEqual(json.loads(config.read_text())['token'],'fixture-secret')
            queued = [json.loads(p.read_text()) for p in (uploader.parent/'pending').glob('*.json')]
            self.assertEqual(len(queued), 1)
            text = queued[0]['logs']['GTAIV-QoL-audio-layout.log']
            self.assertEqual(json.loads(text)['schema'], 1)
            self.assertNotIn('fixture-secret', text)
            self.assertNotIn('do not upload this', text)

    def test_signed_gain_and_variable_category_offset(self):
        body = bytes([4]) + bytes(10) + struct.pack('<I', 1 | 2 | 256 | 8192)
        body += struct.pack('<hhII', -450, 100, 8000, helper.audio_hash('cutscenes'))
        header = helper.audio_sound_header(body)
        self.assertEqual(header['volume_centidb'], -450)
        self.assertEqual(header['category'], '%08X' % helper.audio_hash('cutscenes'))
        self.assertEqual(helper.audio_sound_header(bytes(15))['volume_centidb'], None)
        with self.assertRaises(ValueError): helper.audio_sound_header(body[:-1])

    def test_validated_object_index_and_truncation(self):
        raw = dat([('VOICE', b'abc'), ('OTHER', b'defg')])
        self.assertEqual(helper.audio_objects(raw), [('VOICE', b'abc'), ('OTHER', b'defg')])
        for length in range(len(raw)):
            with self.assertRaises((ValueError, UnicodeError)):
                helper.audio_objects(raw[:length])
        invalid = bytearray(raw)
        struct.pack_into('<I', invalid, len(raw) - 8, 0x7fffffff)
        with self.assertRaises(ValueError): helper.audio_objects(invalid)

    def test_real_report_metadata_only_and_config_capitalization(self):
        with tempfile.TemporaryDirectory() as tmp:
            game = Path(tmp)
            config = game/'pc/audio/Config'; config.mkdir(parents=True)
            voice_hash = helper.audio_hash('speech')
            category = bytearray(40)
            struct.pack_into('<h', category, 11, -300)
            category[35] = 1
            struct.pack_into('<I', category, 36, voice_hash)
            (config/'categories.dat15').write_bytes(dat([('cutscenes', category)]))
            sound = bytes([8]) + bytes(10) + struct.pack('<II', 8192, helper.audio_hash('cutscenes'))
            (config/'sounds.dat15').write_bytes(dat([('INTRO', sound)]))
            (game/'GTAIV.exe').write_bytes(b'PRIVATE_BINARY_DO_NOT_CAPTURE')
            (game/'GTAIV.EFLC.FusionFix.asi').write_bytes(b'MZ_AsiSettings_GetApi\0PRIVATE_BINARY_DO_NOT_CAPTURE')
            text = helper.audio_layout_report(game)
            report = json.loads(text)
            self.assertEqual(report['files'][0]['categories'][0]['children'], ['%08X' % voice_hash])
            self.assertEqual(report['files'][1]['examples'][0]['name'], 'INTRO')
            self.assertTrue(report['menu_hosts'][0]['asi_settings_marker'])
            self.assertNotIn('PRIVATE_BINARY', text)
            self.assertNotIn(str(game), text)
            self.assertEqual(text, helper.audio_layout_report(game))

    def test_linked_parent_excluded_and_large_file_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); game = root/'game'; game.mkdir()
            outside = root/'outside'; outside.mkdir()
            (outside/'data').write_bytes(b'secret')
            (game/'pc').symlink_to(outside, target_is_directory=True)
            with self.assertRaises(ValueError): helper.audio_safe_read(game, 'pc/data', 32)
            (game/'big').write_bytes(b'x'*33)
            with self.assertRaises(ValueError): helper.audio_safe_read(game, 'big', 32)
            self.assertEqual(json.loads(helper.audio_layout_report(game))['files'][0]['status'], 'ValueError')

    def test_v2_upgrade_includes_complete_audio_report(self):
        source = """NAMES = ('RoadNav.log', 'RoadNav-arrow.log', 'GTAIV-QoL.log', 'GTAIV-QoL-diagnostics.log', 'GTAIV-QoL-pause.log')
API = 'https://api.github.com/repos/CryKitty/GTAIV-Road-Nav'
def snapshot(game): return {}
def enqueue(game, before, code): pass
def atomic(path, item): pass
def drain(config): pass
# QOL_FULL_CAPTURE_V2
_qol_original_snapshot = snapshot
def old_override(): pass
if __name__ == '__main__': pass
"""
        upgraded = helper.extend(source)
        self.assertNotIn('old_override', upgraded)
        self.assertEqual(upgraded.count('_qol_original_snapshot = snapshot'), 1)
        self.assertEqual(helper.extend(upgraded), upgraded)
        with tempfile.TemporaryDirectory() as tmp:
            game = Path(tmp)
            text = '{"schema": 1, "data": "' + 'a'*200000 + '"}'
            (game/'GTAIV-QoL-audio-layout.log').write_text(text)
            ns = {'__name__': 'fixture'}; exec(upgraded, ns)
            self.assertEqual(ns['snapshot'](game)['GTAIV-QoL-audio-layout.log']['text'], text)


if __name__ == '__main__': unittest.main()
