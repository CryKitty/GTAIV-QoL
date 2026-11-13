#!/usr/bin/env python3
"""Extend an existing RoadNav uploader; capture the current QoL log without launching GTA."""
import ast
import base64
import configparser
from contextlib import contextmanager
import re
import zlib
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
from datetime import datetime, timezone

DIAGNOSTICS_ISSUE_MAX_COMMENTS = 2500
DIAGNOSTICS_ISSUE_TITLE_PREFIX = 'QoL diagnostics'


def diagnostics_issue_day():
    return datetime.now(timezone.utc).date().isoformat()


def diagnostics_issue_title(day=None):
    return '%s — %s' % (DIAGNOSTICS_ISSUE_TITLE_PREFIX, day or diagnostics_issue_day())


def diagnostics_issue_usable(issue):
    return (isinstance(issue, dict) and issue.get('state') == 'open'
            and not issue.get('locked')
            and int(issue.get('comments') or 0) < DIAGNOSTICS_ISSUE_MAX_COMMENTS)


def resolve_diagnostics_issue(api, token, config):
    day = diagnostics_issue_day()
    current = config.get('issue')
    if isinstance(current, int) and config.get('issue_date') == day:
        try:
            current_issue = api(token, '/issues/%d' % current)
        except Exception:
            current_issue = None
        if diagnostics_issue_usable(current_issue):
            return current
    try:
        open_issues = api(token, '/issues?state=open&per_page=100') or []
    except Exception:
        open_issues = []
    for candidate in open_issues:
        if (candidate.get('title') == diagnostics_issue_title(day)
                and diagnostics_issue_usable(candidate)):
            config['issue'] = candidate['number']
            config['issue_date'] = day
            return candidate['number']
    created = api(token, '/issues', {
        'title': diagnostics_issue_title(day),
        'body': ('Daily automatic diagnostics uploads for the GTA IV QoL mod. '
                 'New captures for this date go here.'),
    })
    config['issue'] = created['number']
    config['issue_date'] = day
    return created['number']


TITLE = 'GTA IV QoL bundle'
BUNDLE = {}
FUSIONFIX_BRIDGE = {}
INGAME_UPDATER = {}
INSTALL_BUILD = ''

def progress(percent,phase,**details):pass


def dialog(message, error=False):
    if shutil.which('kdialog'):
        args = ['kdialog', '--title', TITLE, '--error' if error else '--msgbox', message]
    elif shutil.which('zenity'):
        args = ['zenity', '--title', TITLE, '--error' if error else '--info', '--text', message]
    else:
        raise RuntimeError('Open this helper in Steam Deck Desktop Mode; KDE dialogs are required.')
    subprocess.run(args, check=False)


"""Read-only GTA IV audio metadata inventory. Embedded into the installer at packaging.

DAT15 header layout: Parik27/IV.EFLC.Rainbomizer lib/audEngine.hh/.cc;
named object index and volume field: Funkapppeee/liberty crates/rage/src/audio.rs.
No audio samples, executable bytes, user paths or credentials enter the report.
"""
import collections
import hashlib
import json
import struct


def audio_objects(data):
    """Validate the entire named-object index before returning any records."""
    def u32(at):
        if at < 0 or at + 4 > len(data):
            raise ValueError('truncated integer')
        return struct.unpack_from('<I', data, at)[0]
    if len(data) < 24:
        raise ValueError('truncated DAT')
    size = u32(4)
    end = 8 + size
    table_size, count = u32(end), u32(end + 4)
    if count > 131072 or table_size < 4 + count * 4:
        raise ValueError('invalid string table')
    pos = end + 4 + table_size
    total = u32(pos)
    if total > 131072:
        raise ValueError('too many objects')
    pos += 8
    result = []
    for _ in range(total):
        if pos >= len(data):
            raise ValueError('truncated object index')
        length = data[pos]
        if not length or pos + 9 + length > len(data):
            raise ValueError('invalid object name')
        name = data[pos + 1:pos + 1 + length].decode('ascii', 'strict')
        if any(ord(c) < 32 or ord(c) > 126 for c in name):
            raise ValueError('nonprintable object name')
        offset, length_data = u32(pos + 1 + length), u32(pos + 5 + length)
        if offset > size or length_data > size - offset:
            raise ValueError('object outside data block')
        result.append((name, data[8 + offset:8 + offset + length_data]))
        pos += 9 + length
    return result


def audio_hash(name):
    value = 0
    for byte in name.lower().encode('ascii'):
        value = (value + byte) & 0xffffffff
        value = (value + (value << 10)) & 0xffffffff
        value ^= value >> 6
    value = (value + (value << 3)) & 0xffffffff
    value ^= value >> 11
    return (value + (value << 15)) & 0xffffffff


def audio_sound_header(data):
    if len(data) < 15:
        raise ValueError('truncated sound header')
    flags = struct.unpack_from('<I', data, 11)[0]
    sizes = (2, 2, 2, 2, 2, 2, 2, 2, 4, 4, 2, 2, 2, 4)
    pos, category, volume = 15, None, None
    for bit, size in enumerate(sizes):
        if not flags & (1 << bit):
            continue
        if pos + size > len(data):
            raise ValueError('truncated optional field')
        if bit == 0:
            volume = struct.unpack_from('<h', data, pos)[0]
        if bit == 13:
            category = struct.unpack_from('<I', data, pos)[0]
        pos += size
    return {'type': data[0], 'flags': '%08X' % flags,
            'category': '%08X' % category if category is not None else None,
            'volume_centidb': volume}


def audio_safe_read(game, relative, limit):
    # Check every component, not just the leaf. Do not follow links outside the game.
    current = game
    for part in relative.split('/'):
        if not current.is_dir():
            return None
        matches = [child for child in current.iterdir() if child.name.casefold() == part.casefold()]
        if not matches:
            return None
        if len(matches) != 1:
            raise ValueError('ambiguous filename case')
        current = matches[0]
        if current.is_symlink():
            raise ValueError('linked path excluded')
    if not current.is_file():
        return None
    with current.open('rb') as stream:
        data = stream.read(limit + 1)
    if len(data) > limit:
        raise ValueError('file exceeds capture bound')
    return data


def audio_layout_report(game):
    report = {'schema': 1, 'purpose': 'separate gameplay/cutscene voice controls',
              'files': [], 'menu_hosts': []}
    for episode, prefix in (('', ''), ('TLAD/', 'ep1_'), ('TBoGT/', 'ep2_')):
        for kind in ('categories.dat15', 'sounds.dat15'):
            filename = prefix + kind
            # Linux installs can preserve the original Config capitalization.
            record = {'path': episode + 'pc/audio/config/' + filename}
            try:
                data = None
                for config in ('config', 'Config'):
                    relative = episode + 'pc/audio/' + config + '/' + filename
                    data = audio_safe_read(game, relative, 32 * 1024 * 1024)
                    if data is not None:
                        record['path'] = relative
                        break
                if data is None:
                    record['status'] = 'missing'
                else:
                    record.update(bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
                    objects = audio_objects(data)
                    record['objects'] = len(objects)
                    if kind.startswith('categories'):
                        record['categories'] = []
                        for name, body in objects[:512]:
                            item = {'name': name, 'hash': '%08X' % audio_hash(name), 'bytes': len(body)}
                            if len(body) >= 36 and 36 + 4 * body[35] <= len(body):
                                item['volume_centidb'] = struct.unpack_from('<h', body, 11)[0]
                                item['children'] = ['%08X' % struct.unpack_from('<I', body, 36 + i * 4)[0]
                                                    for i in range(body[35])]
                            record['categories'].append(item)
                        record['omitted_categories'] = max(0, len(objects) - 512)
                    else:
                        counts, examples = collections.Counter(), []
                        invalid = 0
                        for name, body in objects:
                            try:
                                item = audio_sound_header(body)
                            except ValueError:
                                invalid += 1
                                continue
                            counts['%s/type%d' % (item['category'] or 'inherited', item['type'])] += 1
                            if len(examples) < 64 and (item['type'] == 4 or
                                    item['category'] == '%08X' % audio_hash('cutscenes') or
                                    'CUTSCENE' in name.upper()):
                                item.update(name=name, bytes=len(body))
                                # Metadata only: bounded records help identify track references.
                                item['metadata_prefix'] = body[:96].hex()
                                examples.append(item)
                        record.update(groups=dict(sorted(counts.items())[:1024]), examples=examples,
                                      omitted_groups=max(0, len(counts) - 1024),
                                      invalid_headers=invalid)
                    record['status'] = 'parsed'
            except (OSError, ValueError, UnicodeError, struct.error) as error:
                record['status'] = type(error).__name__
            report['files'].append(record)
    for folder in ('', 'plugins/', 'scripts/'):
        relative = folder + 'GTAIV.EFLC.FusionFix.asi'
        try:
            data = audio_safe_read(game, relative, 32 * 1024 * 1024)
            if data is not None:
                # Presence is a hint, not proof that a callable menu API exists.
                report['menu_hosts'].append({'path': relative, 'bytes': len(data),
                    'sha256': hashlib.sha256(data).hexdigest(),
                    'asi_settings_marker': b'AsiSettings_GetApi\0' in data,
                    'qol_menu_bridge_marker': b'QoLFusionFixBridgeVersion\0' in data})
        except (OSError, ValueError):
            report['menu_hosts'].append({'path': relative, 'status': 'unreadable_or_excluded'})
    text = json.dumps(report, sort_keys=True, indent=2)
    if len(text) > 512000:
        # Never emit a truncated JSON document or an unbounded upload.
        return json.dumps({'schema': 1, 'status': 'report exceeded capture bound'})
    return text


EXTENSION = "# QOL_FULL_CAPTURE_V12\n# Added by the QoL helper. Keep the established API/auth/retry implementation.\n_qol_original_snapshot = snapshot\n\ndef snapshot(game):\n    result = _qol_original_snapshot(game)\n    for name in NAMES:\n        if name not in NAMES:\n            continue\n        path = game / name\n        if path.is_file() and not path.is_symlink():\n            st = path.stat()\n            # ASI capture is capped at 1 MiB; allow its final record too.\n            with path.open('rb') as stream:\n                # RoadNav and third-party logs append across sessions. Keep the newest fault.\n                tail = max(0, st.st_size - (1048576 + 8192)) if name in ('RoadNav.log', 'RoadNav-arrow.log', 'ScriptHook.log', 'LibertyLoadout.log', 'FusionFix.log') else 0\n                stream.seek(tail)\n                data = stream.read(1048576 + 8192)\n                if tail:\n                    data = ('[Tail capture: %d earlier bytes omitted]\\n' % tail).encode() + data\n            result[name] = {'stamp': [st.st_ino, st.st_mtime_ns, st.st_size],\n                            'text': data.decode('utf-8', 'replace')}\n    return result\n\n\ndef enqueue(game, before, code):\n    import datetime\n    import uuid\n    current = snapshot(game)\n    changed = [(name, value['text']) for name, value in current.items()\n               if name not in before or value['stamp'] != before[name]['stamp']]\n    if not changed:\n        return\n    asi = game / 'GTAIV-QoL.asi'\n    digest = hashlib.sha256(asi.read_bytes()).hexdigest() if asi.is_file() else 'missing'\n    session = uuid.uuid4().hex\n    parts = []\n    # Put the current session's navigation evidence ahead of full historical logs.\n    # Full captures below are retained; this does not narrow diagnostic coverage.\n    arrow = current.get('RoadNav-arrow.log', {}).get('text', '')\n    if arrow and any(name == 'RoadNav-arrow.log' for name, _ in changed):\n        start = arrow.rfind('RoadNav v')\n        recent = arrow[start:] if start >= 0 else arrow\n        keys = [line for line in recent.splitlines() if any(tag in line for tag in\n                ('RoadNav v', 'native observers', 'mission brief:', 'Compass objective:',\n                 'Compass bound objective:', 'Transition cover:', 'Fork scan:', 'Automatic guidance:', 'attacker pool:', 'passive fault', 'GPS request:', 'Mission guidance:', 'Road line route:', 'Road line routes:', 'Road surface:', 'Police escape surface:', 'Police search probe:', 'Road GPS Line:', 'Compass minimap:', 'Police escape:', 'Compass: geometry masked', 'radar layout guard'))]\n        escape = [line for line in recent.splitlines() if 'Police escape:' in line]\n        summary = '\\n'.join(keys[-60:]) + '\\n\\nRecent navigation tail:\\n' + recent[-4000:] + '\\n\\nEscape planning for this session:\\n' + '\\n'.join(escape[-60:])\n        radar = [line for line in recent.splitlines() if 'Compass radar ' in line or 'Compass minimap: typed radar queue' in line]\n        summary = summary[-16000:] + '\\n\\nRadar initialization for this session:\\n' + '\\n'.join(radar[-100:])[-12000:]\n        parts.append(('QoL-session-summary.txt', summary))\n    for name, text in changed:\n        # Bound rendered comment length even for newline-heavy logs.\n        part, rendered = '', 0\n        for char in text:\n            cost = 5 if char == '\\n' else 1\n            if rendered + cost > 40000:\n                parts.append((name, part))\n                part, rendered = '', 0\n            part += char\n            rendered += cost\n        parts.append((name, part))\n    for number, (name, text) in enumerate(parts, 1):\n        item = {'id': uuid.uuid4().hex,\n                'utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),\n                'exit': str(code) + '; QoL capture ' + session + ' part ' + str(number) + '/' + str(len(parts)) + '; GTAIV-QoL.asi SHA256=' + digest,\n                # Existing renderer labels this RoadNav hash; do not mislabel QoL.\n                'asi_sha256': 'not collected; QoL hash is above',\n                'logs': {name: text}}\n        priority = '0000-police-' if name == 'GTAIV-QoL-police.log' else '0000-nav-' if name == 'QoL-session-summary.txt' else ''\n        atomic(ROOT / 'pending' / (priority + item['id'] + '.json'), item)\n\n# QOL_UPLOAD_RECOVERY_V2\n# Daily destination rotation: a fresh issue per UTC day. Pending items keep\n# their original identifiers and content, and delivered originals are archived.\ndef resolve_daily_issue(config):\n    import time\n    day = time.strftime('%Y-%m-%d', time.gmtime())\n    title = 'QoL diagnostics — %s' % day\n    def usable(issue):\n        return isinstance(issue, dict) and issue.get('state') == 'open' and not issue.get('locked') and int(issue.get('comments') or 0) < 2500\n    current = config.get('issue')\n    if isinstance(current, int) and config.get('issue_date') == day:\n        try:\n            issue = api(config['token'], '/issues/%d' % current)\n        except Exception:\n            issue = None\n        if usable(issue):\n            return issue\n    try:\n        open_issues = api(config['token'], '/issues?state=open&per_page=100') or []\n    except Exception:\n        open_issues = []\n    for candidate in open_issues:\n        if candidate.get('title') == title and usable(candidate):\n            config['issue'] = candidate['number']\n            config['issue_date'] = day\n            return candidate\n    created = api(config['token'], '/issues', {'title': title, 'body': 'Daily automatic diagnostics uploads for the GTA IV QoL mod. New captures for this date go here.'})\n    config['issue'] = created['number']\n    config['issue_date'] = day\n    return created            return issue\n\n\ndef drain(config):\n    import fcntl\n    import re\n    import time\n    with (ROOT / 'upload.lock').open('a') as lock:\n        try:\n            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)\n        except BlockingIOError:\n            return\n        pending = ROOT / 'pending'\n        paths = list(pending.glob('*.json'))\n        if not paths:\n            return\n        try:\n            # The destination rotates daily; see resolve_daily_issue.\n            if not api(config['token'], '')['private']:\n                raise RuntimeError('Log repository must remain private')\n            destination = resolve_daily_issue(config)\n            # resolve_daily_issue already picked a usable issue.\n            # Fetch each issue once per batch, rather than all history for every\n            # single queued part. Include the old issue to reconcile lost replies.\n            known = set()\n            for issue in (2, 9, 10, destination['number']):\n                page = 1\n                while True:\n                    comments = api(config['token'], '/issues/%d/comments?per_page=100&page=%d' % (issue, page))\n                    for comment in comments:\n                        known.update(re.findall(r'<!-- roadnav-session:([0-9a-f]{32}) -->', comment.get('body') or ''))\n                    if len(comments) < 100:\n                        break\n                    page += 1\n            delivered = ROOT / 'delivered'\n            if delivered.is_symlink():\n                raise RuntimeError('Linked receipt archive')\n            delivered.mkdir(exist_ok=True, mode=0o700)\n            # Bring the newest navigation summary forward without dropping older\n            # sessions or full diagnostics. The timer continues bounded batches.\n            def priority(path):\n                if path.name.startswith('0000-police-'):return 0\n                if path.name.startswith('0000-nav-'):return 1\n                # Existing queue entries retain their original names and bytes.\n                # Bring route evidence ahead of very large third-party logs.\n                try:\n                    if 'RoadNav-arrow.log' in json.loads(path.read_text()).get('logs', {}):return 2\n                except (OSError, ValueError):pass\n                return 3\n            paths.sort(key=lambda p: (priority(p), -p.stat().st_mtime_ns, p.name))\n            for path in paths[:20]:\n                if path.is_symlink():\n                    raise RuntimeError('Linked queue entry')\n                item = json.loads(path.read_text())\n                if not re.fullmatch(r'[0-9a-f]{32}', item['id']):\n                    raise RuntimeError('Invalid queued session ID')\n                archive = delivered / path.name\n                if archive.exists() or archive.is_symlink():\n                    raise RuntimeError('Receipt archive collision; queue retained')\n                marker = '<!-- roadnav-session:' + item['id'] + ' -->'\n                if item['id'] not in known:\n                    receipt = api(config['token'], '/issues/%d/comments' % destination['number'], {'body': body(item)})\n                    if not receipt.get('id') or marker not in (receipt.get('body') or ''):\n                        raise RuntimeError('Upload receipt not confirmed')\n                    known.add(item['id'])\n                    time.sleep(1.1)\n                # Never unlink queued diagnostics. Move the exact original only\n                # after a confirmed receipt, preserving it locally in delivered/.\n                path.replace(archive)\n                print('RoadNav: confirmed session ' + item['id'] + '; original archived locally')\n        except Exception as exc:\n            # Do not expose tokens, request bodies or headers in service logs.\n            print('RoadNav: upload deferred (' + type(exc).__name__ + '); pending diagnostics preserved.', file=sys.stderr)\n"


def extend(source):
    ast.parse(source)
    if '# QOL_FULL_CAPTURE_V12' in source:
        return source
    for marker in ('# QOL_FULL_CAPTURE_V1', '# QOL_FULL_CAPTURE_V2', '# QOL_FULL_CAPTURE_V3', '# QOL_FULL_CAPTURE_V4', '# QOL_FULL_CAPTURE_V5', '# QOL_FULL_CAPTURE_V6', '# QOL_FULL_CAPTURE_V7', '# QOL_FULL_CAPTURE_V8', '# QOL_FULL_CAPTURE_V9', '# QOL_FULL_CAPTURE_V10', '# QOL_FULL_CAPTURE_V11'):
        if marker in source:
            start = source.index(marker)
            end = source.index("if __name__ == '__main__':", start)
            source = source[:start] + source[end:]
    old = "NAMES = ('RoadNav.log', 'RoadNav-arrow.log')"
    previous = "NAMES = ('RoadNav.log', 'RoadNav-arrow.log', 'GTAIV-QoL.log')"
    v3 = "NAMES = ('RoadNav.log', 'RoadNav-arrow.log', 'GTAIV-QoL.log', 'GTAIV-QoL-diagnostics.log', 'GTAIV-QoL-pause.log', 'GTAIV-QoL-audio-layout.log')"
    v7 = "NAMES = ('RoadNav.log', 'RoadNav-arrow.log', 'GTAIV-QoL.log', 'GTAIV-QoL-diagnostics.log', 'GTAIV-QoL-pause.log', 'GTAIV-QoL-audio-layout.log', 'ScriptHook.log', 'LibertyLoadout.log', 'FusionFix.log')"
    new = "NAMES = ('RoadNav.log', 'RoadNav-arrow.log', 'GTAIV-QoL.log', 'GTAIV-QoL-diagnostics.log', 'GTAIV-QoL-pause.log', 'GTAIV-QoL-audio-layout.log', 'ScriptHook.log', 'LibertyLoadout.log', 'FusionFix.log', 'GTAIV-QoL-police.log')"
    full = "NAMES = ('RoadNav.log', 'RoadNav-arrow.log', 'GTAIV-QoL.log', 'GTAIV-QoL-diagnostics.log')"
    v2 = "NAMES = ('RoadNav.log', 'RoadNav-arrow.log', 'GTAIV-QoL.log', 'GTAIV-QoL-diagnostics.log', 'GTAIV-QoL-pause.log')"
    match = new if new in source else v7 if v7 in source else v3 if v3 in source else v2 if v2 in source else full if full in source else previous if previous in source else old
    if source.count(match) != 1 or "API = 'https://api.github.com/repos/CryKitty/GTAIV-Road-Nav'" not in source:
        raise RuntimeError('This RoadNav uploader version is unfamiliar. No files were changed.')
    required = ('def snapshot(', 'def enqueue(', 'def atomic(', 'def drain(')
    if not all(item in source for item in required):
        raise RuntimeError('The installed uploader lacks the expected queue functions. No files were changed.')
    # Put overrides before the main guard, so automatic launches use them too.
    guard = "if __name__ == '__main__':"
    if source.count(guard) != 1:
        raise RuntimeError('The installed uploader has an unfamiliar entry point. No files were changed.')
    result = source.replace(match, new).replace(guard, EXTENSION + '\n\n' + guard)
    ast.parse(result)
    return result


def merge_preferences(previous, fresh):
    limits = {('Updates', 'CheckForUpdates'): (0, 1), ('Camera', 'TaxiRearFOV'): (0, 120), ('RoadGPSLine', 'Enabled'): (0, 1), ('RoadNav', 'Enabled'): (0, 1), ('RoadNav', 'ChaseGuidance'): (0, 1), ('RoadNav', 'DestinationColours'): (0, 1), ('DefaultMapZoom', 'Enabled'): (0, 1),
              ('RoadGPSLine', 'ShowPoliceEscape'): (0, 1),
              ('RoadGPSLine', 'ShowMission'): (0, 1),
              ('RoadGPSLine', 'ShowFriend'): (0, 1),
              ('RoadGPSLine', 'ShowEnemy'): (0, 1),
              ('RoadGPSLine', 'ShowCustom'): (0, 1),
              ('RoadGPSLine', 'ViewBendStrengthPercent'): (0, 100),
              ('RoadGPSLine', 'ViewBendMaxDegrees'): (0, 85),
              ('RoadGPSLine', 'ViewBendResponsePercent'): (25, 400),
              ('DefaultMapZoom', 'ZoomPercent'): (100, 400), ('CutscenePause', 'Enabled'): (0, 1),
              ('CutscenePause', 'Overlay'): (0, 1), ('VoiceVolume', 'Enabled'): (0, 1),
              ('VoiceVolume', 'GameplayPercent'): (0, 400), ('VoiceVolume', 'CutscenePercent'): (0, 1600), ('Compass', 'Enabled'): (0, 1),
              ('Compass', 'FallbackRadiusMeters'): (50, 1000), ('Compass', 'MetricDistances'): (0, 1)}
    old = configparser.ConfigParser(interpolation=None, strict=False)
    try:
        old.read_string(previous.decode('utf-8-sig'))
    except (UnicodeError, configparser.Error):
        return fresh
    saved = {}
    try:
        taxi_slider = old.getint('Camera', 'TaxiFovSliderVersion', fallback=0)
    except (configparser.Error, ValueError):
        taxi_slider = 0
    try:
        output_mix = old.getint('VoiceVolume', 'OutputMixVersion', fallback=0)
    except (configparser.Error, ValueError):
        output_mix = 0
    try:
        bend_menu = old.getint('RoadGPSLine', 'ViewBendMenuVersion', fallback=0)
    except (configparser.Error, ValueError):
        bend_menu = 0
    for (section, key), (low, high) in limits.items():
        try:
            # Both legacy Flat and Twisted now map to On with working twist
            # defaults. Adopt them once; preserve the menu choice afterward. Keep
            # the existing visibility toggle, including a user's explicit Off.
            if section == 'RoadGPSLine' and key.startswith('ViewBend') and bend_menu<2:
                continue
            value = old.getint(section, key)
            if (section, key) == ('VoiceVolume', 'CutscenePercent') and output_mix < 1 and value > 100:
                value = 100
            if (section, key) == ('Camera', 'TaxiRearFOV') and value == 80 and taxi_slider < 1:
                value = 70
            if low <= value <= high:
                saved[(section.lower(), key.lower())] = str(value)
        except (configparser.Error, ValueError):
            pass
    lines, section = [], ''
    for line in fresh.decode('utf-8-sig').splitlines(keepends=True):
        match = re.match(r'\s*\[([^]]+)\]', line)
        if match:
            section = match[1].lower()
        else:
            match = re.match(r'(\s*([^;#=]+?)\s*=\s*)([^;#\r\n]*)(.*)', line)
            if match and (section, match[2].strip().lower()) in saved:
                newline = '\n' if line.endswith('\n') else ''
                line = match[1] + saved[(section, match[2].strip().lower())] + match[4] + newline
        lines.append(line)
    return ''.join(lines).encode('utf-8')


def install_bundle(game, bundle):
    progress(68,'Checking installed files and backups')
    allowed = {'GTAIV-QoL.asi', 'GTAIV-QoL-Pause.asi', 'GTAIV-QoL.ini'}
    if set(bundle) != allowed:
        raise RuntimeError('The bundle is incomplete. No game files were changed.')
    decoded = {}
    for name, item in bundle.items():
        data = base64.b64decode(item['data'], validate=True)
        if hashlib.sha256(data).hexdigest() != item['sha256']:
            raise RuntimeError('The installer checksum failed. No game files were changed.')
        decoded[game/name] = data
    worker=None;worker_source=None
    if INGAME_UPDATER:
        import types
        worker_source=base64.b64decode(INGAME_UPDATER['data'],validate=True)
        if hashlib.sha256(worker_source).hexdigest()!=INGAME_UPDATER['sha256']:raise RuntimeError('In-game updater checksum failed.')
        worker=types.ModuleType('qol_game_update_installer')
        exec(compile(worker_source,'<verified in-game updater>','exec'),worker.__dict__)
        decoded[game/'GTAIV-QoL-Updater.py']=worker_source
    changes = []
    ff_targets = []
    for folder in (game, game/'plugins', game/'scripts'):
        if not folder.is_dir():
            continue
        for path in folder.iterdir():
            lower = path.name.lower()
            if lower == 'gtaiv.eflc.fusionfix.asi':
                ff_targets.append(path)
            if lower not in {name.lower() for name in allowed} | {'roadnav.asi'}:
                continue
            if folder != game and path.suffix.lower() != '.asi':
                continue
            if path.is_symlink() or not path.is_file():
                raise RuntimeError('A bundle target is a link or directory. No game files were changed.')
            changes.append((path, path.read_bytes()))
    prior_ini = next((raw for path, raw in changes if path.name.lower() == 'gtaiv-qol.ini'), None)
    if prior_ini is not None:
        decoded[game/'GTAIV-QoL.ini'] = merge_preferences(prior_ini, decoded[game/'GTAIV-QoL.ini'])
    if FUSIONFIX_BRIDGE:
        state_path = game/'GTAIV-QoL-bundle-state.json'
        state = {}
        if state_path.is_file() and not state_path.is_symlink() and state_path.stat().st_size < 65536:
            try:
                state = json.loads(state_path.read_text())
            except (ValueError, OSError):
                pass
        if len(ff_targets) != 1 or ff_targets[0].is_symlink() or not ff_targets[0].is_file():
            raise RuntimeError('Expected one installed FusionFix 5.1.2 file. No game files were changed.')
        target = ff_targets[0]
        old_ff = target.read_bytes()
        original_sha = hashlib.sha256(old_ff).hexdigest()
        supported = set(FUSIONFIX_BRIDGE['supported_sha256'])
        if state.get('fusionfix_sha256'):
            supported.add(state['fusionfix_sha256'])
        if original_sha not in supported:
            raise RuntimeError('This FusionFix build is not supported by the native menu bridge. No game files were changed.')
        encoded = base64.b64decode(FUSIONFIX_BRIDGE['data'], validate=True)
        raw_ff = zlib.decompress(encoded)
        if hashlib.sha256(raw_ff).hexdigest() != FUSIONFIX_BRIDGE['sha256']:
            raise RuntimeError('FusionFix menu bridge checksum failed. No game files were changed.')
        decoded[target] = raw_ff
        changes.append((target, old_ff))
        decoded[state_path] = json.dumps({'schema': 1, 'fusionfix_sha256': FUSIONFIX_BRIDGE['sha256'], 'fusionfix_path': target.relative_to(game).as_posix()}).encode()
        decoded[game/'GTAIV-QoL-THIRD-PARTY.txt'] = FUSIONFIX_BRIDGE['notice'].encode()
    for path in decoded:
        if path.is_symlink() or (path.exists() and not path.is_file()):
            raise RuntimeError('A bundle target is a link or directory. No game files were changed.')
        if path.exists() and not any(prior == path for prior, _ in changes):
            changes.append((path, path.read_bytes()))
    original = {path: data for path, data in changes}
    installed = []
    try:
        progress(72,'Backing up existing files')
        for path, data in changes:
            backup = path.with_name(path.name+'.before-qol-bundle-'+hashlib.sha256(data).hexdigest()[:12])
            if not backup.exists():
                backup.write_bytes(data)
            path.unlink()
        for number,(dest, data) in enumerate(decoded.items(),1):
            progress(74+12*number/len(decoded),'Installing files (%d/%d)'%(number,len(decoded)))
            tmp = dest.with_name(dest.name+'.qol-install-tmp')
            if tmp.is_symlink():
                raise RuntimeError('Linked temporary install target.')
            tmp.write_bytes(data)
            tmp.replace(dest)
            installed.append(dest)
        for path, data in decoded.items():
            if hashlib.sha256(path.read_bytes()).hexdigest() != hashlib.sha256(data).hexdigest():
                raise RuntimeError('Installed bundle checksum failed.')
        if worker is not None:worker.install_game_updater(game,worker_source)
    except Exception:
        for path in installed:
            path.unlink(missing_ok=True)
        for path, data in original.items():
            path.write_bytes(data)
        raise RuntimeError('Installation failed; prior bundle, RoadNav and FusionFix files were restored.') from None


@contextmanager
def upload_repair_lock(root):
    """Quiesce a busy retry service before replacing its code/configuration."""
    import fcntl
    paused=False
    timer_active=False
    try:
        with (root/'upload.lock').open('a') as upload_lock:
            try:fcntl.flock(upload_lock,fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError:
                progress(68,'Pausing background log retries for repair')
                try:
                    timer_active=subprocess.run(['systemctl','--user','is-active','--quiet','roadnav-upload.timer'],capture_output=True,timeout=5).returncode==0
                    paused=True
                    units=(['roadnav-upload.timer'] if timer_active else [])+['roadnav-upload.service']
                    result=subprocess.run(['systemctl','--user','stop']+units,capture_output=True,timeout=15)
                    if result.returncode!=0:raise RuntimeError('Could not pause the background log retry service. Queued logs are preserved.')
                except (OSError,subprocess.TimeoutExpired):
                    raise RuntimeError('Could not pause the background log retry service in time. Queued logs are preserved.') from None
                # A stopped service releases its OS lock; never remove a lock
                # file or write around another live process holding the lock.
                try:fcntl.flock(upload_lock,fcntl.LOCK_EX | fcntl.LOCK_NB)
                except BlockingIOError:
                    raise RuntimeError('Another log upload process is still active after pausing retries. Queued logs are preserved.') from None
            yield
    finally:
        # Run only after closing our lock, including on installation failure.
        if paused:
            units=(['roadnav-upload.timer'] if timer_active else [])+['roadnav-upload.service']
            try:subprocess.run(['systemctl','--user','--no-block','start']+units,capture_output=True,timeout=5)
            except (OSError,subprocess.TimeoutExpired):pass # Persistent queue remains for the timer/manual retry.


def save_upload_config(config_path, config):
    tmp = config_path.with_suffix('.qol-log-config-tmp')
    if tmp.is_symlink():
        raise RuntimeError('Linked upload configuration temporary file.')
    tmp.write_text(json.dumps(config))
    tmp.chmod(0o600)
    tmp.replace(config_path)


def migrate_upload_destination(config_path,config):
    if not isinstance(config.get('issue'), int):
        raise RuntimeError('Unrecognized RoadNav log issue; configuration unchanged.')
    if config.get('issue_date') == diagnostics_issue_day():
        return
    backup = config_path.with_name('config.before-log-issue-%d.json' % config['issue'])
    if not backup.exists():
        with backup.open('x') as stream:stream.write(config_path.read_text())
        backup.chmod(0o600)
    updated = dict(config)
    updated['issue'] = None
    updated['issue_date'] = None
    tmp=config_path.with_suffix('.qol-log-migration-tmp')
    if tmp.is_symlink():raise RuntimeError('Linked upload configuration temporary file.')
    tmp.write_text(json.dumps(updated));tmp.chmod(0o600);tmp.replace(config_path)
    config.update(updated)


def main():
    os.umask(0o077)
    config_path = Path.home()/'.config/roadnav-uploader/config.json'
    uploader = Path.home()/'.local/share/roadnav-uploader/roadnav-upload.py'
    standalone_config=Path.home()/'.config/gtaiv-qol-updater/config.json'
    standalone=json.loads(standalone_config.read_text()) if standalone_config.is_file() else {}
    selected=Path(standalone['game']) if standalone.get('game') else None
    existing=json.loads(config_path.read_text()) if config_path.is_file() else {}
    if BUNDLE and selected and (not uploader.is_file() or uploader.is_symlink() or not existing or Path(existing.get('game',''))!=selected):
        if not (selected/'GTAIV.exe').is_file():raise RuntimeError('The selected folder no longer contains GTAIV.exe.')
        root=Path.home()/'.local/share/roadnav-uploader';root.mkdir(parents=True,exist_ok=True,mode=0o700)
        with (root/'session.lock').open('a') as lock:
            import fcntl
            try:fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
            except BlockingIOError:raise RuntimeError('Close GTA IV before updating.') from None
            install_bundle(selected,BUNDLE)
        dialog('Installed the verified QoL bundle. Existing settings were preserved. Use Update GTA IV QoL for future builds.')
        return
    if not config_path.is_file() or not uploader.is_file() or uploader.is_symlink():
        raise RuntimeError('The existing RoadNav automatic uploader was not found. No files were changed.')
    config = json.loads(config_path.read_text())
    if config.get('issue') is not None and not isinstance(config.get('issue'), int):
        raise RuntimeError('The RoadNav log configuration has an invalid diagnostics issue. No files were changed.')
    game = Path(config['game'])
    if not (game/'GTAIV.exe').is_file():
        raise RuntimeError('The saved RoadNav game folder no longer contains GTAIV.exe. No files were changed.')
    # Confirm destination before changing the installed helper or queuing logs.
    spec = importlib.util.spec_from_file_location('roadnav_existing', uploader)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    source = uploader.read_text()
    updated = extend(source)
    if not module.api(config['token'], '')['private']:
        raise RuntimeError('The RoadNav repository is not private. Nothing was uploaded or changed.')
    try:
        resolve_diagnostics_issue(module.api, config['token'], config)
    except Exception:
        raise RuntimeError('No diagnostics issue is available today; queued logs were preserved.') from None
    save_upload_config(config_path, config)
    root = uploader.parent
    with (root/'session.lock').open('a') as lock:
        import fcntl
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise RuntimeError('GTA IV appears to be running. Close it, then run this helper again.') from None
        with upload_repair_lock(root):
            if BUNDLE:
                install_bundle(game, BUNDLE)
            if source != updated:
                backup = uploader.with_name('roadnav-upload.before-qol.py')
                if not backup.exists():
                    backup.write_text(source); backup.chmod(0o700)
                tmp = uploader.with_suffix('.qol-tmp')
                tmp.write_text(updated); tmp.chmod(0o700); tmp.replace(uploader)
            migrate_upload_destination(config_path,config)
            # Read metadata only; no game binary or audio samples are uploaded.
            # A failed optional inventory must not interrupt installation or existing logs.
            capture = game/'GTAIV-QoL-audio-layout.log'
            if not capture.is_symlink():
                try:
                    report = audio_layout_report(game)
                    if not capture.exists() or capture.read_text() != report:
                        tmp_capture = capture.with_suffix('.qol-capture-tmp')
                        if not tmp_capture.is_symlink():
                            tmp_capture.write_text(report)
                            tmp_capture.replace(capture)
                except (OSError, ValueError):
                    pass
            # The original enqueue/drain implementation retains offline snapshots and
            # reconciles ambiguous delivery. Capture QoL only for this manual upload.
            spec.loader.exec_module(module)
            module.NAMES = ('RoadNav.log', 'RoadNav-arrow.log', 'GTAIV-QoL.log', 'GTAIV-QoL-diagnostics.log', 'GTAIV-QoL-pause.log', 'GTAIV-QoL-audio-layout.log', 'ScriptHook.log', 'LibertyLoadout.log', 'FusionFix.log', 'GTAIV-QoL-police.log')
            module.ROOT.joinpath('pending').mkdir(parents=True, exist_ok=True)
            logs = module.snapshot(game)
            marker = root/'last-qol-manual-sha256'
            digest = hashlib.sha256(json.dumps(logs, sort_keys=True).encode()).hexdigest()
            captured = bool(logs)
            if captured and (not marker.exists() or marker.read_text() != digest):
                module.enqueue(game, {}, 'manual QoL crash-log capture; no game launch')
                marker.write_text(digest)
    progress(90,'Queuing diagnostic logs for background upload')
    # Keep the existing timer and Steam options. Request an immediate retry too.
    try:subprocess.run(['systemctl', '--user', '--no-block', 'start', 'roadnav-upload.service'], capture_output=True,timeout=5)
    except (OSError,subprocess.TimeoutExpired):pass # Queued logs remain for the existing retry timer.
    pending = list((root/'pending').glob('*.json'))
    message = ('Automatic uploads now include all QoL logs, with full diagnostic captures queued in numbered parts. '
               'A read-only audio-layout report is included for the upcoming voice sliders. No game launch is needed for this report.\nYour existing Steam launch option and authorization are unchanged.\n\n')
    if not captured:
        message += 'No GTAIV-QoL.log was found. Run the diagnostic build, exit normally, then use this helper again if needed.'
    elif pending:
        message += 'The existing log was captured. Some uploads remain queued; RoadNav will retry in the background. Tell Codex you ran the helper.'
    else:
        message += 'The existing QoL log has been delivered to the private RoadNav log issue #10. Tell Codex to check it. No game launch is needed.'
    if BUNDLE:
        message = ('Installed QoL bundle '+INSTALL_BUILD+' with verified ASI checksums.\nYour settings were preserved. Prior QoL, RoadNav and FusionFix files were backed up; standalone RoadNav copies were disabled.\n\nQoL tab: RoadNav, Map Autozoom, Pause During Cutscenes and Cutscene Audio Volume. Audio: Cutscene Audio controls the complete cutscene mix; gameplay volume is restored afterward. Esc/Start pauses and resumes cutscenes without a time limit.\n\n') + message
    dialog("GTA IV QoL is ready to play. Logs will upload in the background.")


if __name__ == '__main__':
    try:
        main()
    except Exception as exc:
        # Do not expose API responses, saved tokens, headers or config contents.
        message = str(exc) if isinstance(exc, RuntimeError) else ('The helper could not finish ('+type(exc).__name__+'). Existing logs remain on the Deck. Check the connection and RoadNav authorization.')
        dialog(message, error=True)
        sys.exit(1)

