# Adapted from CryKitty/GTAIV-Road-Nav fix/guided-player-installer
# pinned b87c6e9bc09ca085f3fea1481e179213650087f9. Original dependency checks retained.
"""Optional offline RoadNav installer. Dependency downloads stay with their authors."""
import base64
import hashlib
import html
import configparser
import shlex
import re
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import io
import zipfile
import zlib
import urllib.request
import urllib.parse
import time

ASI_B64 = '__ROADNAV_ASI_B64__'
ASI_SHA256 = '88c96eed09f72ee8ef7cec55ef276bc093b11442020c50d551bbaf3cd0e579e3'
TITLE = 'GTA IV QoL Setup'
URLS = {
    'loader': 'https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/tag/v10.0.2',
    'hook': 'https://www.dev-c.com/gtaiv/scripthook/',
    'hook_fallback': 'https://github.com/HazardX/gta4_scripthookdotnet/releases/tag/v1.7.1.7b',
    'ce': 'https://www.lcpdfr.com/downloads/gta4mods/g17media/26726-compatibility-patch-for-gta-iv-complete-edition/',
    'runtime': 'https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist',
    'game': 'https://support.rockstargames.com/articles/5paYceP7Rlop5S2x3YycS5/installing-grand-theft-auto-iv-for-pc-on-steam',
}
HOOK_EXPORTS = {
    '??0ScriptThread@@QAE@XZ', '??1ScriptThread@@UAE@XZ',
    '?RegisterThread@ScriptHookManager@@SAXPAVScriptThread@@PAUHINSTANCE__@@@Z',
    '?SpeedyInvoke@NativeInvoke@@CAXPAVNativeContext@@I@Z',
    '?RunScript@ScriptThread@@MAEXXZ',
    '??0ID3DDeviceHook@@QAE@XZ',
    '?RequestService@ScriptHookManager@@SAPAVIService@@PBD@Z',
    '?GetNativeAddressFromHash@Game@@SAPAXI@Z',
}
# Only common root-folder ASI proxy names. Rendering wrappers alone do not count.
LOADER_EXPORTS = {
    'dinput8.dll': {'DirectInput8Create'},
    'dsound.dll': {'DirectSoundCreate'},
    'xlive.dll': {'XLiveInitialize'},
    'version.dll': {'GetFileVersionInfoA'},
    'winmm.dll': {'timeGetTime'},
}


class SetupError(Exception):
    pass


class Cancelled(Exception):
    pass


class PE32:
    """Read PE headers/exports without loading or executing a downloaded DLL."""
    def __init__(self, path):
        self.path = Path(path)
        if self.path.stat().st_size > 64 * 1024 * 1024:
            raise SetupError('File is unexpectedly large: ' + self.path.name)
        self.data = self.path.read_bytes()
        try:
            if self.data[:2] != b'MZ':
                raise ValueError()
            pe = self.u32(0x3c)
            machine, count = struct.unpack_from('<HH', self.data, pe + 4)
            size = self.u16(pe + 20)
            self.is_dll = bool(self.u16(pe + 22) & 0x2000)
            opt = pe + 24
            if (self.data[pe:pe+4] != b'PE\0\0' or machine != 0x14c or
                    self.u16(opt) != 0x10b or size < 104 or not 0 < count <= 96):
                raise ValueError()
            self.export_rva = self.u32(opt + 96)
            self.resource_rva = self.u32(opt + 112) if size >= 120 else 0
            self.sections = []
            for i in range(count):
                at = opt + size + i * 40
                vs, va, raw_size, raw = struct.unpack_from('<IIII', self.data, at + 8)
                if raw + raw_size > len(self.data):
                    raise ValueError()
                self.sections.append((va, raw_size, raw))
        except (ValueError, struct.error, IndexError):
            raise SetupError('Not a readable 32-bit Windows file: ' + self.path.name) from None

    def u16(self, offset):
        return struct.unpack_from('<H', self.data, offset)[0]

    def u32(self, offset):
        return struct.unpack_from('<I', self.data, offset)[0]

    def offset(self, rva):
        for va, size, raw in self.sections:
            if va <= rva < va + size:
                return raw + rva - va
        raise SetupError('Unreadable export table: ' + self.path.name)

    def exports(self):
        if not self.export_rva:
            return set()
        try:
            start = self.offset(self.export_rva)
            count, names = self.u32(start + 24), self.u32(start + 32)
            if count > 100000:
                raise ValueError()
            result = set()
            for i in range(count):
                at = self.offset(self.u32(self.offset(names) + i * 4))
                end = self.data.index(b'\0', at, at + 1024)
                result.add(self.data[at:end].decode('ascii'))
            return result
        except (ValueError, struct.error, UnicodeError):
            raise SetupError('Unreadable export table: ' + self.path.name) from None


def game_version(exe):
    """Read the executable's RT_VERSION resource on Linux and Windows alike."""
    image = PE32(exe)
    if not image.resource_rva:
        raise SetupError('GTA IV version could not be read from GTAIV.exe.')
    base = image.offset(image.resource_rva)
    def entries(relative):
        at = base + relative
        count = image.u16(at + 12) + image.u16(at + 14)
        if count > 256:
            raise SetupError('Invalid GTAIV.exe version resource.')
        return [struct.unpack_from('<II', image.data, at + 16 + i*8) for i in range(count)]
    try:
        roots = [target for ident, target in entries(0) if ident == 16]
        for target in roots:
            if not target & 0x80000000:
                continue
            for _, child in entries(target & 0x7fffffff):
                if not child & 0x80000000:
                    continue
                for _, leaf in entries(child & 0x7fffffff):
                    if leaf & 0x80000000:
                        continue
                    rva, size = struct.unpack_from('<II', image.data, base + leaf)
                    at = image.offset(rva)
                    blob = image.data[at:at+size]
                    key = 'VS_VERSION_INFO\0'.encode('utf-16le')
                    if blob[6:6+len(key)] != key:
                        continue
                    fixed = (6 + len(key) + 3) & ~3
                    signature, structure, ms, ls = struct.unpack_from('<IIII', blob, fixed)
                    if signature == 0xfeef04bd and structure == 0x10000:
                        return (ms >> 16, ms & 0xffff, ls >> 16, ls & 0xffff)
    except (struct.error, ValueError, IndexError):
        pass
    raise SetupError('GTA IV version could not be read from GTAIV.exe.')


def check_game_version(game):
    try:
        version = game_version(find_file(game, 'GTAIV.exe'))
        text = '.'.join(map(str, version))
        return version == (1, 2, 0, 59), ('GTA IV ' + text if version == (1, 2, 0, 59) else
            'GTA IV ' + text + ' is not supported. The QoL bundle requires Complete Edition 1.2.0.59.')
    except (OSError, SetupError) as exc:
        return False, str(exc)


def find_file(folder, name):
    # Proton folders are case-sensitive; GTA DLL names are not.
    matches = [p for p in Path(folder).iterdir() if p.name.lower() == name.lower()]
    if len(matches) > 1:
        raise SetupError('Duplicate names differing only in case: ' + name)
    return matches[0] if matches else Path(folder) / name


def validate_game(exe):
    exe = Path(exe).resolve()
    if exe.name.lower() != 'gtaiv.exe' or PE32(exe).is_dll:
        raise SetupError('Select the actual GTAIV.exe, not a launcher or mod file.')
    return exe.parent


def check_dependency(game, key):
    try:
        if key == 'loader':
            # Identify Ultimate ASI Loader even when its proxy DLL is renamed.
            candidates = sorted(p for p in Path(game).iterdir() if p.suffix.lower() == '.dll')
            for path in candidates:
                try:
                    image = PE32(path)
                    if path.name.lower() in set(LOADER_EXPORTS)|{'d3d9.dll'} and image.is_dll and 'IsUltimateASILoader' in image.exports():
                        return True, 'ASI loader detected: ' + path.name
                except (SetupError, OSError):
                    continue
            # Older loaders lack that export; require both proxy exports and a loader marker.
            for name, required in LOADER_EXPORTS.items():
                path = find_file(game, name)
                if path.is_file():
                    try:
                        image = PE32(path)
                        markers = (b'asi loader', b'asiload', b'xlive less', b'xliveless')
                        if image.is_dll and required <= image.exports() and any(
                                marker in image.data.lower() for marker in markers):
                            return True, 'ASI loader detected: ' + path.name
                    except SetupError:
                        continue
            return False, 'ASI loader missing or unrecognized. Install the Win32/x86 dinput8 loader.'
        name = {'hook': 'ScriptHook.dll', 'ce': 'aCompleteEditionHook.asi'}[key]
        path = find_file(game, name)
        if not path.is_file():
            return False, name + ' is missing beside GTAIV.exe.'
        image = PE32(path)
        if not image.is_dll:
            return False, name + ' is not a DLL/ASI plugin.'
        if key == 'hook' and not HOOK_EXPORTS <= image.exports():
            return False, 'ScriptHook.dll lacks the native GTA IV functions RoadNav uses.'
        return True, name + ' found (32-bit).'
    except (OSError, SetupError) as exc:
        return False, str(exc)


def prefix_candidates(game):
    candidates = []
    for parent in Path(game).parents:
        if parent.name.lower() == 'steamapps':
            candidates.append(parent / 'compatdata/12210/pfx')
    for root in (Path.home()/'.local/share/Steam', Path.home()/'.steam/steam'):
        candidates.append(root / 'steamapps/compatdata/12210/pfx')
    return list(dict.fromkeys(p.resolve() for p in candidates if (p/'drive_c/windows').is_dir()))


def check_runtime(game, prefix=None):
    if sys.platform == 'win32':
        windows = Path(os.environ.get('WINDIR', r'C:\Windows'))
        # A 32-bit bundled installer sees the correct 32-bit System32 view.
        system = windows / ('SysWOW64' if (windows/'SysWOW64').is_dir() else 'System32')
    elif prefix:
        windows = Path(prefix) / 'drive_c/windows'
        system = windows / ('syswow64' if (windows/'syswow64').is_dir() else 'system32')
    else:
        return False, 'Select GTA IV\'s Proton prefix so its runtime files can be checked.'
    missing = []
    for name in ('msvcp140.dll', 'vcruntime140.dll', 'ucrtbase.dll'):
        found = False
        for folder in (game, system):
            try:
                image = PE32(find_file(folder, name))
                found = image.is_dll and (name=='ucrtbase.dll' or b'wine builtin dll' not in image.data.lower())
                if found:
                    break
            except (OSError, SetupError):
                pass
        if not found:
            missing.append(name)
    return (not missing, '32-bit runtime files found.' if not missing else
            'Missing or invalid 32-bit runtime files: ' + ', '.join(missing))


def install_mod(game, payload, prefix=None):
    raise SetupError("Use the QoL guided installer entry point.")


def status_markup(message, kde=False):
    lines = []
    for line in message.splitlines():
        safe = html.escape(line)
        color = '#16803c' if line.startswith('✓') else '#d22f36' if line.startswith('✗') else None
        if color:
            safe = ('<font color="' + color + '">' + safe + '</font>' if kde else
                    '<span foreground="' + color + '">' + safe + '</span>')
        lines.append(safe)
    return '<html><body>' + '<br>'.join(lines) + '</body></html>' if kde else '\n'.join(lines)


# Fixed author releases and hashes: no Git client, token or untrusted mirrors.
AUTO_DEPENDENCIES = {
    'loader': {
        'url': 'https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/download/v10.0.2/Ultimate-ASI-Loader.zip',
        'archive_sha256': 'a8f76a891f9beeef3b02babf1062c128b00e0285fa17c227a45ec4f81f45116f',
        'name': 'dinput8.dll',
        'file_sha256': '63b465dbc1bf9c5ab4e33c3ccad099d9edb702f2c51587ac8d4f196497396437',
    },
    'hook': {
        'url': 'https://github.com/HazardX/gta4_scripthookdotnet/releases/download/v1.7.1.7b/scripthookdotnet_v1.7.1.7b.zip',
        'archive_sha256': '2e9df1e30c6693920d55d1c211b839c681c3b4168868c704b08e63897bd7b674',
        'name': 'ScriptHook.dll',
        'file_sha256': '2b10866a374b52f8550f7d0e416b3550f9b58f9dc839f62c938197fe9f56fa8e',
    },
}
MAX_DOWNLOAD = 32 * 1024 * 1024
# Microsoft-maintained winget manifest: Microsoft.VCRedist.2015+.x86 14.44.35211.0.
# Download/execute only these verified official bytes, never arbitrary DLL copies.
RUNTIME = {
    'url': 'https://download.visualstudio.microsoft.com/download/pr/73aabf2e-9532-4f68-99f7-3247081a619c/0C09F2611660441084CE0DF425C51C11E147E6447963C3690F97E0B25C55ED64/VC_redist.x86.exe',
    'archive_sha256': '0c09f2611660441084ce0df425c51c11e147e6447963c3690f97e0b25c55ed64',
    'allowed_hosts': ('download.visualstudio.microsoft.com',),
    'suffix': '.exe',
}
EXTRA_CHECKS = {}
EXTRA_DOWNLOADERS = {}
EXTRA_IMPORTERS = {}


def validate_dependency_bytes(data, key):
    with tempfile.TemporaryDirectory(prefix='roadnav-check-') as folder:
        name = {'loader': 'dinput8.dll', 'hook': 'ScriptHook.dll', 'ce': 'aCompleteEditionHook.asi'}[key]
        path = Path(folder)/name
        path.write_bytes(data)
        image = PE32(path)
        if not image.is_dll:
            raise SetupError(name + ' is not a 32-bit DLL/ASI.')
        required = {'loader': {'IsUltimateASILoader', 'DirectInput8Create'}, 'hook': HOOK_EXPORTS, 'ce': set()}[key]
        if not required <= image.exports():
            raise SetupError(name + ' does not provide the required functions.')


def zip_dependency(data, name, max_file=16*1024*1024):
    """Read a single expected member; never unpack arbitrary paths or other mods."""
    try:
        with zipfile.ZipFile(io.BytesIO(data)) as archive:
            if len(archive.infolist()) > 1000:
                raise SetupError('Too many entries in the dependency archive.')
            matches = [i for i in archive.infolist() if i.filename.replace('\\', '/').split('/')[-1].lower() == name.lower()]
            if len(matches) != 1:
                raise SetupError('The archive must contain exactly one ' + name + '.')
            info = matches[0]
            parts = info.filename.replace('\\', '/').split('/')
            if (info.is_dir() or info.filename.startswith(('/', '\\')) or '..' in parts or
                    ':' in info.filename or (info.external_attr >> 16) & 0o170000 == 0o120000 or
                    info.flag_bits & 1 or not 0 < info.file_size <= max_file):
                raise SetupError('Invalid dependency archive member: ' + name)
            return archive.read(info)
    except (zipfile.BadZipFile, RuntimeError, NotImplementedError, EOFError, zlib.error) as exc:
        raise SetupError('The dependency ZIP is incomplete or unreadable. Download it again.') from exc


def install_dependency_bytes(game, key, data):
    validate_game(find_file(game, 'GTAIV.exe'))
    ok, message = check_game_version(game)
    if not ok:
        raise SetupError(message)
    if check_dependency(game, key)[0]:
        return 'Already installed; existing ' + key + ' was kept.'
    validate_dependency_bytes(data, key)
    name = {'loader': 'dinput8.dll', 'hook': 'ScriptHook.dll', 'ce': 'aCompleteEditionHook.asi'}[key]
    target = find_file(game, name)
    # Exclusive creation prevents a race or a mistaken overwrite of another mod.
    if target.exists() or target.is_symlink():
        raise SetupError(name + ' already exists but failed checks. It was preserved; resolve the incompatible dependency with its installer before retrying.')
    created = False
    try:
        with target.open('xb') as file:
            created = True
            file.write(data)
            file.flush()
            os.fsync(file.fileno())
        if target.read_bytes() != data:
            raise SetupError('Installed dependency failed verification: ' + name)
    except Exception:
        if created:
            target.unlink(missing_ok=True)
        raise
    return 'Installed ' + name + '.'


QUIET_COMPLETION=False
GUIDE_PROTOCOL=2
SETUP_NOTE=''
def report_progress(phase,received,total):pass

def download_author_archive(spec):
    cache = Path(os.environ.get('XDG_CACHE_HOME', str(Path.home()/'.cache')))/'RoadNav/setup'
    cache.mkdir(parents=True, exist_ok=True)
    target = cache/(spec['archive_sha256'] + spec.get('suffix', '.zip'))
    if target.is_file() and target.stat().st_size <= MAX_DOWNLOAD:
        data = target.read_bytes()
        if hashlib.sha256(data).hexdigest() == spec['archive_sha256']:
            return data
    request = urllib.request.Request(spec['url'], headers={'User-Agent': 'RoadNav-Setup/5'})
    chunks, size, started = [], 0, time.monotonic()
    with urllib.request.urlopen(request, timeout=20) as response:
        total=int(getattr(response,'headers',{}).get('Content-Length',0) or 0)
        final = urllib.parse.urlparse(response.geturl())
        if final.scheme != 'https' or final.hostname not in spec.get('allowed_hosts', ('github.com', 'release-assets.githubusercontent.com', 'objects.githubusercontent.com')):
            raise SetupError('Unexpected dependency download destination.')
        while True:
            chunk = response.read(256*1024)
            if not chunk:
                break
            size += len(chunk)
            report_progress("Downloading dependencies",size,total)
            if size > MAX_DOWNLOAD or time.monotonic()-started > 90:
                raise SetupError('Dependency download exceeded its size/time limit.')
            chunks.append(chunk)
    data = b''.join(chunks)
    if hashlib.sha256(data).hexdigest() != spec['archive_sha256']:
        raise SetupError('Dependency download failed verification. No game files were changed.')
    # Cache verified bytes only; an interrupted cache write will be rejected next time.
    target.write_bytes(data)
    return data


def install_missing_downloads(game, progress=None):
    ok, message = check_game_version(game)
    if not ok:
        raise SetupError(message)
    messages = []
    for key, spec in AUTO_DEPENDENCIES.items():
        if check_dependency(game, key)[0]:
            continue
        # Check collisions before network access.
        target = find_file(game, spec['name'])
        if target.exists() or target.is_symlink():
            raise SetupError(spec['name'] + ' exists but is unrecognized. Existing files are preserved; resolve the incompatible dependency with its installer before retrying.')
        if progress:
            progress('Downloading ' + spec['name'] + ' from its author...')
        archive = download_author_archive(spec)
        data = zip_dependency(archive, spec['name'])
        if hashlib.sha256(data).hexdigest() != spec['file_sha256']:
            raise SetupError('The downloaded ' + spec['name'] + ' failed verification.')
        messages.append(install_dependency_bytes(game, key, data))
    return messages


def downloads_dirs():
    candidates = [Path.home()/'Downloads']
    config = Path(os.environ.get('XDG_CONFIG_HOME', str(Path.home()/'.config')))/'user-dirs.dirs'
    try:
        text = config.read_text(encoding='utf-8')
        match = re.search(r'^XDG_DOWNLOAD_DIR="([^"\n]+)"', text, re.M)
        if match:
            value = match.group(1).replace('$HOME', str(Path.home()))
            if '$' not in value and Path(value).is_absolute():
                candidates.insert(0, Path(value))
    except (OSError, UnicodeError):
        pass
    if sys.platform == 'win32':
        import winreg
        try:
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r'Software\Microsoft\Windows\CurrentVersion\Explorer\User Shell Folders') as key:
                value = winreg.QueryValueEx(key, '{374DE290-123F-4565-9164-39C4925E467B}')[0]
                candidates.insert(0, Path(os.path.expandvars(value)))
        except OSError:
            pass
    return list(dict.fromkeys(path.resolve() for path in candidates if path.is_dir()))


def import_ce_download(game):
    if check_dependency(game, 'ce')[0]:
        return None
    candidates = []
    for folder in downloads_dirs():
        for path in folder.iterdir():
            if path.is_file() and re.fullmatch(r'GTAIV[ _-]Complete[ _-]Edition[ _-]Fix[ _-]0[ _-]4(?:\s*\(\d+\))?\.zip', path.name, re.I):
                candidates.append(path)
    problems = []
    # Newest completed ZIP first; .part/.crdownload files never match.
    for path in sorted(candidates, key=lambda p: p.stat().st_mtime, reverse=True):
        try:
            if path.stat().st_size > 10*1024*1024:
                raise SetupError('CE patch ZIP is unexpectedly large.')
            data = zip_dependency(path.read_bytes(), 'aCompleteEditionHook.asi', max_file=2*1024*1024)
            return install_dependency_bytes(game, 'ce', data) + ' Downloaded ZIP was kept.'
        except (OSError, SetupError, zipfile.BadZipFile) as exc:
            problems.append(path.name + ': ' + str(exc))
    if problems:
        raise SetupError('Could not import the CE patch download.\n' + '\n'.join(problems[:3]))
    return None


def completed_downloads(pattern, limit=32*1024*1024):
    found=[]
    for folder in downloads_dirs():
        for path in folder.iterdir():
            if not path.is_symlink() and path.is_file() and pattern(path.name.lower()) and 0<path.stat().st_size<=limit:
                found.append(path)
    return sorted(found,key=lambda p:p.stat().st_mtime,reverse=True)[:100]


def import_dependency_download(game,key):
    if check_dependency(game,key)[0]:return None
    spec=AUTO_DEPENDENCIES[key];name=spec['name']
    candidates=completed_downloads(lambda n:n==name.lower() or n.endswith('.zip') and n.startswith('ultimate-asi-loader' if key=='loader' else 'scripthook'))
    errors=[]
    for path in candidates:
        try:
            raw=path.read_bytes()
            data=zip_dependency(raw,name) if path.suffix.lower()=='.zip' else raw
            if hashlib.sha256(data).hexdigest()!=spec['file_sha256']:
                raise SetupError('Not the supported verified '+name+'; use its supported download page or automatic download.')
            return install_dependency_bytes(game,key,data)+' Original download kept.'
        except (OSError,SetupError) as exc:errors.append(str(exc))
    if errors:raise SetupError('\n'.join(errors[:3]))
    return None


def proton_tools():
    tools=[]
    for root in steam_roots():
        libraries=[root]
        try:libraries += [Path(p) for p in vdf_values((root/'steamapps/libraryfolders.vdf').read_text(), 'path')]
        except (OSError,UnicodeError):pass
        tools.extend(root.glob('compatibilitytools.d/*/proton'))
        for library in libraries:tools.extend(library.glob('steamapps/common/Proton*/proton'))
    return list(dict.fromkeys(p.resolve() for p in tools if p.is_file() and os.access(p,os.X_OK)))


def read_vdf(path):
    if path.stat().st_size>4*1024*1024:raise SetupError('Steam configuration is too large.')
    tokens=re.findall(r'"(?:\\.|[^"\\])*"|[{}]|//[^\n]*',path.read_text(encoding='utf-8-sig'))
    tokens=iter(t for t in tokens if not t.startswith('//'))
    def decode(t):return re.sub(r'\\(["\\])',r'\1',t[1:-1])
    def block(depth=0):
        if depth>64:raise SetupError('Steam configuration is too deeply nested.')
        result={}
        for token in tokens:
            if token=='}':return result
            if not token.startswith('"'):raise SetupError('Steam configuration is incomplete.')
            value=next(tokens,None)
            if value=='{':value=block(depth+1)
            elif value and value.startswith('"'):value=decode(value)
            else:raise SetupError('Steam configuration is incomplete.')
            result[decode(token).lower()]=value
        if depth:raise SetupError('Steam configuration is incomplete.')
        return result
    return block()


def vdf_sections(tree,name):
    for key,value in tree.items():
        if isinstance(value,dict):
            if key==name:yield value
            yield from vdf_sections(value,name)


def steam_roots_for_prefix(prefix):
    roots=steam_roots()
    library=Path(prefix).parent.parent.parent.parent.resolve()
    associated=[]
    for root in roots:
        libraries=[root.resolve()]
        for config in (root/'steamapps/libraryfolders.vdf',root/'config/libraryfolders.vdf'):
            try:libraries += [Path(p).resolve() for p in vdf_values(config.read_text(encoding='utf-8'),'path')]
            except (OSError,UnicodeError):pass
        if library in libraries:associated.append(root)
    return associated or roots


def configured_proton(prefix):
    tools=proton_tools();roots=steam_roots_for_prefix(prefix)
    names=[]
    for root in roots:
        try:
            for mapping in vdf_sections(read_vdf(root/'config/config.vdf'),'compattoolmapping'):
                entry=mapping.get('12210',mapping.get('0',{}))
                if not isinstance(entry,dict):raise SetupError('Steam game configuration is incomplete. Launch GTA IV once from Steam, close it, then Retry.')
                if entry.get('name'):names.append(entry['name'])
        except (OSError,UnicodeError):pass
    names=set(names)
    if len(names)>1:raise SetupError('Steam game configuration is ambiguous. Launch GTA IV once from Steam, close it, then Retry.')
    if names:
        name=next(iter(names));matches=[]
        for tool in tools:
            try:
                tree=read_vdf(tool.parent/'compatibilitytool.vdf')
                if any(name.lower() in section for section in vdf_sections(tree,'compat_tools')):matches.append(tool)
            except (OSError,UnicodeError):pass
        if len(matches)==1:return matches[0]
        raise SetupError('The game compatibility tool configured in Steam is unavailable. Launch GTA IV once from Steam, close it, then Retry.')
    # With no explicit/default Steam mapping, use only the exact last-run tool
    # recorded by Steam/Proton for this app, never another installed version.
    try:
        path=Path(prefix).parent/'config_info'
        if path.stat().st_size>4*1024*1024:raise SetupError('Steam game configuration is too large.')
        info=path.read_text(encoding='utf-8')
        matches=[tool for tool in tools if any(line==str(tool.parent) or line.startswith(str(tool.parent)+os.sep) for line in info.splitlines())]
        if len(matches)==1:return matches[0]
    except (OSError,UnicodeError):pass
    raise SetupError('Setup could not read Steam game compatibility settings. Launch GTA IV once from Steam, close it, then Retry.')


def existing_prefix(game):
    candidates=prefix_candidates(game)
    for parent in Path(game).resolve().parents:
        if parent.name.lower()=='steamapps':
            preferred=(parent/'compatdata/12210/pfx').resolve()
            if preferred in candidates:return preferred
    return candidates[0] if len(candidates)==1 else None


def proton_command(prefix,ui):
    if not prefix or not (Path(prefix)/'drive_c/windows').is_dir():raise SetupError('Launch GTA IV once from Steam, close it, then Retry.')
    data_path=Path(prefix).parent.resolve()
    if (data_path/'pfx').resolve()!=Path(prefix).resolve():raise SetupError('GTA IV game folder could not be matched to its existing Steam prefix; no other prefix will be modified.')
    tool=configured_proton(prefix)
    roots=steam_roots_for_prefix(prefix)
    if not roots:raise SetupError('Steam installation was not found.')
    environment=dict(os.environ,STEAM_COMPAT_DATA_PATH=str(data_path),STEAM_COMPAT_CLIENT_INSTALL_PATH=str(roots[0]),STEAM_COMPAT_APP_ID='12210',SteamAppId='12210',SteamGameId='12210')
    return [str(tool),'run'],environment


def run_in_prefix(prefix,ui,args):
    if sys.platform=='win32':command,environment=[],dict(os.environ)
    else:command,environment=proton_command(prefix,ui)
    result=subprocess.run(command+list(args),env=environment,capture_output=True,timeout=240)
    if result.returncode not in (0,1638,3010):raise SetupError('Dependency installer returned status '+str(result.returncode)+'. Launch GTA IV once from Steam, close it, then Retry.')
    return result


def install_runtime(game,prefix,ui,data):
    if check_runtime(game,prefix)[0]:return 'Microsoft X86 runtime already installed.'
    if hashlib.sha256(data).hexdigest()!=RUNTIME['archive_sha256']:raise SetupError('Microsoft X86 runtime checksum mismatch; no installer was executed. Use Download supported Microsoft X86 installer (14.44.35211.0) or automatic setup.')
    folder=Path(os.environ.get('XDG_CACHE_HOME',str(Path.home()/'.cache')))/'RoadNav/setup'/RUNTIME['archive_sha256']
    folder.mkdir(parents=True,exist_ok=True);path=folder/'VC_redist.x86.exe'
    if folder.is_symlink() or path.is_symlink():raise SetupError('Unsafe runtime staging path.')
    path.write_bytes(data)
    if hashlib.sha256(path.read_bytes()).hexdigest()!=RUNTIME['archive_sha256']:raise SetupError('Runtime staging verification failed.')
    if ui:ui.progress('Installing verified Microsoft X86 runtime into GTA IV\'s selected prefix...')
    run_in_prefix(prefix,ui,[str(path),'/install','/quiet','/norestart'])
    ok,message=check_runtime(game,prefix)
    if not ok:raise SetupError('Microsoft installer finished, but re-check failed: '+message)
    return 'Installed and re-checked Microsoft X86 runtime.'


def import_runtime_download(game,prefix,ui):
    if check_runtime(game,prefix)[0]:return None
    candidates=completed_downloads(lambda n:bool(re.fullmatch(r'vc_redist\.x86(?:\s*\(\d+\))?\.exe',n)))
    errors=[]
    for path in candidates:
        try:return install_runtime(game,prefix,ui,path.read_bytes())+' Original download kept.'
        except (OSError,SetupError) as exc:errors.append(str(exc))
    if errors:raise SetupError('\n'.join(errors[:3]))
    return None


VERIFIED_OVERRIDES=set()
def loader_proxy(game):
    ok,message=check_dependency(game,'loader')
    return Path(message.split(': ',1)[1]).stem.lower() if ok and ': ' in message else None


def check_loader_override(game,prefix):
    if sys.platform=='win32':return True,'Native Windows DLL loading needs no Proton override.'
    proxy=loader_proxy(game)
    if not prefix or not proxy:return False,'ASI loader native DLL override needs a detected loader and GTA IV Proton prefix.'
    if (str(Path(prefix).resolve()),proxy) in VERIFIED_OVERRIDES:return True,'ASI loader native override verified in GTA IV prefix.'
    try:
        path=Path(prefix)/'user.reg'
        if path.stat().st_size>8*1024*1024:return False,'Proton registry exceeds the supported inspection size.'
        text=path.read_text(errors='replace')
        section=re.search(r'^\[Software\\\\Wine\\\\DllOverrides\][^\n]*\n(.*?)(?=^\[|\Z)',text,re.M|re.S)
        value=re.search(r'^"'+re.escape(proxy)+r'"="([^"\n]*)"',section[1],re.M|re.I) if section else None
        if value and value[1].replace(' ','').lower().split(',')[0] in ('native','n'):
            return True,'ASI loader native override found for '+proxy+'.'
    except OSError:pass
    return False,'Enable the installed '+proxy+' ASI loader in GTA IV\'s Proton prefix using automatic setup.'


def install_loader_override(game,prefix,ui):
    if check_loader_override(game,prefix)[0]:return 'Existing ASI loader override kept.'
    proxy=loader_proxy(game)
    if not proxy:raise SetupError('Install a supported ASI loader first.')
    key=r'HKCU\Software\Wine\DllOverrides'
    run_in_prefix(prefix,ui,['reg.exe','add',key,'/v',proxy,'/t','REG_SZ','/d','native,builtin','/f'])
    result=run_in_prefix(prefix,ui,['reg.exe','query',key,'/v',proxy])
    if b'native,builtin' not in result.stdout.replace(b'\0',b'').lower():raise SetupError('Proton loader override read-back failed; no Steam launch options were changed.')
    VERIFIED_OVERRIDES.add((str(Path(prefix).resolve()),proxy))
    return 'Enabled and verified the ASI loader in GTA IV\'s Proton prefix; Steam launch options were kept.'


_BROWSER_DESKTOP = None


def application_dirs():
    roots = [Path(os.environ.get('XDG_DATA_HOME', str(Path.home()/'.local/share')))]
    roots += [Path(p) for p in os.environ.get('XDG_DATA_DIRS', '/usr/local/share:/usr/share').split(':') if p]
    roots += [Path.home()/'.local/share/flatpak/exports/share', Path('/var/lib/flatpak/exports/share')]
    return list(dict.fromkeys(root/'applications' for root in roots))


def browser_entry(path):
    """Accept installed browser launchers, excluding store/install placeholders."""
    try:
        parser = configparser.ConfigParser(interpolation=None, strict=False)
        parser.read_string(Path(path).read_text(encoding='utf-8'))
        entry = parser['Desktop Entry']
        if entry.get('Type') != 'Application' or entry.get('Hidden', '').lower() == 'true':
            return None
        types = entry.get('MimeType', '')
        if not (any(kind in types for kind in ('x-scheme-handler/https', 'x-scheme-handler/http')) or
                ('text/html' in types and 'WebBrowser;' in entry.get('Categories', ''))):
            return None
        command = entry.get('Exec', '')
        lower = command.lower()
        if any(marker in lower for marker in ('appstream:', 'discover', 'flatpak install')):
            return None
        args = shlex.split(command)
        if not args:
            return None
        if Path(args[0]).name == 'env':
            args = args[1:]
            while args and '=' in args[0] and not args[0].startswith('/'):
                args.pop(0)
        if not args or Path(args[0]).name in ('sh', 'bash', 'xdg-open', 'gio'):
            return None
        executable = shutil.which(args[0])
        if not executable:
            return None
        # SteamOS can ship a browser-named script that opens the software store.
        with open(executable, 'rb') as file:
            header = file.read(8192)
        if header.startswith(b'#!') and any(marker in header.lower() for marker in
                (b'appstream:', b'discover', b'flatpak install')):
            return None
        flatpak_id = entry.get('X-Flatpak', '')
        if Path(args[0]).name == 'flatpak':
            if 'run' not in args:
                return None
            if not flatpak_id:
                # Only tokens after "run" can be the app, not launcher arguments.
                flatpak_id = next((arg for arg in args[args.index('run')+1:]
                    if not arg.startswith(('-', '/', '%', '@')) and '.' in arg), '')
            if not flatpak_id or subprocess.run([executable, 'info', flatpak_id],
                    capture_output=True, timeout=10).returncode:
                return None
        return entry.get('Name', Path(path).stem), Path(path)
    except (OSError, ValueError, configparser.Error, subprocess.SubprocessError):
        return None


def registered_browsers():
    directories = application_dirs()
    defaults = []
    for command in (['xdg-mime', 'query', 'default', 'x-scheme-handler/https'],
                    ['xdg-settings', 'get', 'default-web-browser']):
        if shutil.which(command[0]):
            try:
                result = subprocess.run(command, capture_output=True, text=True, timeout=10)
                desktop_id = result.stdout.strip()
                if result.returncode == 0 and desktop_id.endswith('.desktop') and '/' not in desktop_id:
                    defaults.append(desktop_id)
            except (OSError, subprocess.SubprocessError):
                pass
    installed = {}
    seen = set()
    for directory in directories:
        try:
            paths = sorted(directory.glob('*.desktop'))
        except OSError:
            continue
        for path in paths:
            if path.name in seen:
                continue
            seen.add(path.name)  # User overrides, including Hidden=true, take precedence.
            info = browser_entry(path)
            if info:
                installed[path.name] = info
    for desktop_id in defaults:
        if desktop_id in installed:
            return installed[desktop_id], list(installed.values())
    return None, list(installed.values())


def open_url(url, ui=None):
    global _BROWSER_DESKTOP
    if sys.platform == 'win32':
        os.startfile(url)
        return
    if not shutil.which('gio'):
        raise SetupError('The desktop application launcher (gio) is unavailable. Open this link in your browser: ' + url)
    selected = _BROWSER_DESKTOP
    if selected is None:
        default, installed = registered_browsers()
        if default:
            selected = default[1]
        elif len(installed) == 1:
            selected = installed[0][1]
        elif installed and ui:
            options = [(str(i), name) for i, (name, _) in enumerate(installed)]
            choice = ui.choice('Choose a browser', 'Your desktop default does not point to an installed browser. Choose an installed browser for setup links. This does not change your default settings.', options)
            selected = installed[int(choice)][1]
        else:
            raise SetupError('No installed browser could be identified. Check Desktop Mode default applications, then retry. Link: ' + url)
    # gio launch uses this exact desktop entry, including Flatpak URL forwarding.
    # Never delegate back to the stale SteamOS Firefox/install handler.
    env = os.environ.copy()
    env.pop('BROWSER', None)
    result = subprocess.run(['gio', 'launch', str(selected), url], capture_output=True, timeout=30, env=env)
    if result.returncode:
        _BROWSER_DESKTOP = None
        raise SetupError('The configured browser could not be launched. Link: ' + url)
    _BROWSER_DESKTOP = selected


def steam_roots():
    roots = [Path.home()/'.local/share/Steam', Path.home()/'.steam/steam',
             Path.home()/'.steam/root', Path.home()/'.var/app/com.valvesoftware.Steam/.local/share/Steam']
    if sys.platform == 'win32':
        import winreg
        for hive, key, value in [(winreg.HKEY_CURRENT_USER, r'Software\Valve\Steam', 'SteamPath'),
                                 (winreg.HKEY_LOCAL_MACHINE, r'SOFTWARE\WOW6432Node\Valve\Steam', 'InstallPath')]:
            try:
                with winreg.OpenKey(hive, key) as handle:
                    roots.append(Path(winreg.QueryValueEx(handle, value)[0]))
            except OSError:
                pass
        for key in ('ProgramFiles(x86)', 'ProgramFiles'):
            if os.environ.get(key):
                roots.append(Path(os.environ[key])/'Steam')
    return list(dict.fromkeys(p.resolve() for p in roots if p.is_dir()))


def vdf_values(text, key):
    pattern = r'"' + re.escape(key) + r'"\s*"((?:\\.|[^"\\])*)"'
    return [re.sub(r'\\(["\\])', r'\1', value) for value in re.findall(pattern, text, re.I)]


def discover_games():
    """Bounded read-only search of Steam libraries/manifests, including SD libraries."""
    roots = steam_roots()
    libraries = list(roots)
    for root in roots:
        for file in (root/'steamapps/libraryfolders.vdf', root/'config/libraryfolders.vdf'):
            try:
                libraries += [Path(value) for value in vdf_values(file.read_text(encoding='utf-8'), 'path')]
            except (OSError, UnicodeError):
                pass
    found = []
    for library in dict.fromkeys(p.resolve() for p in libraries):
        names = ['Grand Theft Auto IV']
        manifest = library/'steamapps/appmanifest_12210.acf'
        try:
            names = vdf_values(manifest.read_text(encoding='utf-8'), 'installdir') or names
        except (OSError, UnicodeError):
            pass
        for name in names:
            # A manifest is input data, never a command or permission to sweep disk.
            if '/' in name or '\\' in name or name in ('.', '..'):
                continue
            folder = library/'steamapps/common'/name
            for game in (folder/'GTAIV', folder):
                try:
                    exe = find_file(game, 'GTAIV.exe')
                    validate_game(exe)
                    found.append(exe.resolve())
                except (OSError, SetupError):
                    pass
    return list(dict.fromkeys(found))


def select_game(ui, search=True):
    candidates = discover_games() if search else []
    if candidates:
        options = [('found:' + str(i), 'Use ' + str(exe.parent)) for i, exe in enumerate(candidates)]
        options.append(('browse', 'Choose another installation'))
        action = ui.choice('GTA IV found', 'Steam installation found. Select the game folder you want to use.', options)
        if action.startswith('found:'):
            return validate_game(candidates[int(action.split(':')[1])])
    ui.choice('Locate GTA IV', 'Next, choose GTAIV.exe in your GTA IV installation folder.\n\nIn Steam you can find this folder under GTA IV > Properties > Installed Files > Browse. Setup will check the game version and dependencies for you.', [('next', 'Next')])
    return validate_game(ui.pick('Select GTAIV.exe'))


class DeckUI:
    def __init__(self):
        self.program = shutil.which('kdialog') or shutil.which('zenity')
        if not self.program:
            raise SetupError('Run setup in Steam Deck Desktop Mode (KDE dialogs are required).')
        self.kde = Path(self.program).name == 'kdialog'

    def choice(self, heading, message, choices):
        single = len(choices) == 1
        if choices==[('done','Finish')]:
            cmd=([self.program,'--title',TITLE+' - '+heading,'--msgbox',status_markup(message,kde=True),'--ok-label','Finish'] if self.kde else [self.program,'--info','--title',TITLE+' - '+heading,'--text',status_markup(message),'--ok-label','Finish'])
        elif single:
            if self.kde:
                cmd = [self.program, '--title', TITLE + ' - ' + heading,
                       '--yes-label', choices[0][1], '--no-label', 'Cancel setup',
                       '--yesno', status_markup(message, kde=True)]
            else:
                cmd = [self.program, '--question', '--title', TITLE + ' - ' + heading,
                       '--ok-label', choices[0][1], '--cancel-label', 'Cancel setup',
                       '--text', status_markup(message), '--width=680']
        elif self.kde:
            cmd = [self.program, '--title', TITLE + ' - ' + heading, '--menu', status_markup(message, kde=True)]
            cmd += [part for pair in choices for part in pair]
        else:
            cmd = [self.program, '--list', '--title', TITLE + ' - ' + heading,
                   '--text', status_markup(message), '--column', 'Action', '--column', 'Description',
                   '--hide-column=1', '--print-column=1', '--width=720', '--height=560']
            cmd += [part for pair in choices for part in pair]
        result = subprocess.run(cmd, text=True, capture_output=True)
        if result.returncode:
            raise Cancelled()
        return choices[0][0] if single else result.stdout.strip()

    def progress(self, message):
        if self.kde:
            subprocess.Popen([self.program, '--title', TITLE, '--passivepopup', message, '8'],
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    def pick(self, heading, folder=False, start=None):
        start = str(start or Path.home()) + '/'
        if self.kde:
            cmd = [self.program, '--title', heading, '--getexistingdirectory' if folder else '--getopenfilename', start]
            if not folder:
                cmd.append('*.exe|GTAIV.exe')
        else:
            cmd = [self.program, '--file-selection', '--title', heading, '--filename', start]
            if folder:
                cmd.append('--directory')
        result = subprocess.run(cmd, text=True, capture_output=True)
        if result.returncode or not result.stdout.strip():
            raise Cancelled()
        return Path(result.stdout.strip())


class TkUI:
    def __init__(self):
        import tkinter as tk
        from tkinter import ttk
        self.tk, self.ttk = tk, ttk
        self.root = tk.Tk()
        self.root.withdraw()

    def choice(self, heading, message, choices):
        if getattr(self, '_progress_window', None) and self._progress_window.winfo_exists():
            self._progress_window.destroy()
        win = self.tk.Toplevel(self.root)
        win.title(TITLE)
        win.minsize(620, 360)
        win.resizable(False, False)
        frame = self.ttk.Frame(win, padding=24)
        frame.pack(fill='both', expand=True)
        self.ttk.Label(frame, text=heading, font=('Segoe UI', 17, 'bold')).pack(anchor='w', pady=(0, 14))
        for line in message.splitlines():
            color = '#16803c' if line.startswith('✓') else '#d22f36' if line.startswith('✗') else None
            options = {'foreground': color} if color else {}
            self.ttk.Label(frame, text=line or ' ', wraplength=590, justify='left', **options).pack(anchor='w')
        self.ttk.Frame(frame, height=16).pack()
        answer = []
        def choose(key):
            answer.append(key)
            win.destroy()
        for key, label in choices:
            self.ttk.Button(frame, text=label, command=lambda k=key: choose(k)).pack(fill='x', pady=4)
        if choices!=[('done','Finish')]:self.ttk.Button(frame, text='Cancel setup', command=win.destroy).pack(fill='x', pady=(14, 0))
        win.protocol('WM_DELETE_WINDOW', win.destroy)
        win.grab_set()
        win.wait_window()
        if not answer:
            raise Cancelled()
        return answer[0]

    def progress(self, message):
        if not getattr(self, '_progress_window', None) or not self._progress_window.winfo_exists():
            self._progress_window = self.tk.Toplevel(self.root)
            self._progress_window.title(TITLE)
            self._progress_label = self.ttk.Label(self._progress_window, padding=24, wraplength=500)
            self._progress_label.pack()
        self._progress_label.configure(text=message)
        self._progress_window.update()

    def pick(self, heading, folder=False, start=None):
        from tkinter import filedialog
        options = {'parent': self.root, 'title': heading, 'initialdir': str(start or Path.home())}
        path = (filedialog.askdirectory(**options) if folder else
                filedialog.askopenfilename(**options, filetypes=[('GTA IV', 'GTAIV.exe'), ('Executables', '*.exe')]))
        if not path:
            raise Cancelled()
        return Path(path)


REQUIREMENTS = {
    'game': 'Game update', 'loader': 'ASI loader', 'hook': 'Native ScriptHook',
    'ce': 'Complete Edition patch', 'runtime': 'Microsoft runtime',
    'proton_loader': 'Game loading',
}


def inspect_setup(game, prefix=None):
    checks = {'game': check_game_version(game)}
    for key in ('loader', 'hook', 'ce'):
        checks[key] = check_dependency(game, key)
    checks['runtime'] = check_runtime(game, prefix)
    if sys.platform!='win32':checks['proton_loader']=check_loader_override(game,prefix)
    for key,check in EXTRA_CHECKS.items():checks[key]=check(game,prefix)
    return checks


def open_folder(folder):
    if sys.platform == 'win32':
        os.startfile(str(folder))
    else:
        subprocess.Popen(['xdg-open', str(folder)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def wizard(ui):
    ui.choice('Welcome to GTA IV QoL',(SETUP_NOTE+'\n\n' if SETUP_NOTE else '')+'Setup will find GTA IV, guide you through one required step at a time, then show a final review. Close GTA IV before installing.',[('next','Next')])
    while True:
        try:game=select_game(ui,search=True);break
        except (OSError,SetupError) as exc:ui.choice('Find GTA IV',str(exc),[('retry','Choose GTA IV again')])
    prefix=existing_prefix(game) if sys.platform!='win32' else None
    notice=None
    while True:
        checks=inspect_setup(game,prefix)
        missing=next((key for key,(ok,_) in checks.items() if not ok),None)
        if missing is None:
            summary='\n'.join('✓ '+REQUIREMENTS[key]+' ready' for key in checks)
            ui.choice('Ready to install',summary+'\n\nEverything is ready. Install the bundle and its updater.',[('install','Install GTA IV QoL')])
            try:install_mod(game,b'',prefix)
            except (OSError,SetupError) as exc:
                ui.choice('Installation paused',str(exc),[('retry','Try again')]);continue
            if not QUIET_COMPLETION:ui.choice('GTA IV QoL installed','Done — ready to play. Use Update GTA IV QoL for future updates.',[('done','Finish')])
            return
        key=missing
        # Loader configuration is automatic and uses Steam's app configuration.
        if key=='proton_loader' and prefix and notice is None:
            try:install_loader_override(game,prefix,ui);continue
            except (OSError,SetupError,subprocess.SubprocessError) as exc:notice=str(exc)
        message=(notice+'\n\n' if notice else '')+checks[key][1]
        notice=None
        prefix_missing=sys.platform!='win32' and prefix is None and key in ('runtime','proton_loader')
        if prefix_missing:
            message='Launch GTA IV once from Steam, close it, then Retry. Setup will find the game environment automatically.'
            choices=[('retry','Retry')]
        else:
            automatic=key in AUTO_DEPENDENCIES or key in ('runtime','proton_loader') or key in EXTRA_DOWNLOADERS
            choices=([('auto','Set up '+REQUIREMENTS[key])] if automatic else [])
            if key=='game':choices.append(('game','Choose another GTA IV installation'))
            if key!='proton_loader':choices.append(('help:'+key,'Open download page'))
            if key=='runtime':choices.append(('runtime-download','Download supported installer'))
            choices.append(('retry','Retry / check downloaded file'))
            message+='\n\nSave the supported download in Downloads, then Retry. Setup places the files for you.'
        action=ui.choice('Set up '+REQUIREMENTS[key],message,choices)
        try:
            if action=='game':
                game=select_game(ui,search=True);prefix=existing_prefix(game) if sys.platform!='win32' else None
            elif action.startswith('help:'):open_url(URLS[key],ui)
            elif action=='runtime-download':open_url(RUNTIME['url'],ui)
            elif action=='auto':
                if key in AUTO_DEPENDENCIES:
                    spec=AUTO_DEPENDENCIES[key]
                    install_dependency_bytes(game,key,zip_dependency(download_author_archive(spec),spec['name']))
                elif key=='runtime':install_runtime(game,prefix,ui,download_author_archive(RUNTIME))
                elif key=='proton_loader':install_loader_override(game,prefix,ui)
                else:EXTRA_DOWNLOADERS[key](game,prefix,ui)
            elif action=='retry':
                if key in AUTO_DEPENDENCIES:notice=import_dependency_download(game,key)
                elif key=='ce':notice=import_ce_download(game)
                elif key=='runtime' and not prefix_missing:notice=import_runtime_download(game,prefix,ui)
                elif key=='proton_loader' and prefix:notice=install_loader_override(game,prefix,ui)
                elif key in EXTRA_IMPORTERS:notice=EXTRA_IMPORTERS[key](game,prefix,ui)
                if prefix_missing:prefix=existing_prefix(game)
        except (OSError,SetupError,subprocess.SubprocessError) as exc:notice=str(exc)


def gui_smoke_test():
    """CI-only mode: render and invoke a real GUI button, without touching a game."""
    if hashlib.sha256(b'').hexdigest() != ASI_SHA256:
        raise SetupError('Bundled ASI failed smoke-test verification')
    ui = TkUI()
    def finish():
        def walk(widget):
            for child in widget.winfo_children():
                if child.winfo_class() == 'TButton' and child.cget('text') == 'Continue':
                    child.invoke()
                    return True
                if walk(child):
                    return True
            return False
        if not walk(ui.root):
            ui.root.destroy()
    ui.root.after(300, finish)
    try:
        return 0 if ui.choice('RoadNav setup check',
            '✓ Native ScriptHook found.\n✗ Complete Edition patch missing.\n\nGUI rendering check.', [('next', 'Continue')]) == 'next' else 1
    finally:
        ui.root.destroy()


def main():
    ui = None
    try:
        ui = TkUI() if sys.platform == 'win32' else DeckUI()
        wizard(ui)
        return 0
    except Cancelled:
        return 0
    except Exception as exc:
        message = str(exc) if isinstance(exc, (OSError, SetupError)) else 'Setup stopped (' + type(exc).__name__ + ').'
        if ui:
            try:
                ui.choice('Setup could not finish', message + '\n\nClose the game and resolve the problem, then run setup again.', [('close', 'Close')])
            except Cancelled:
                pass
        else:
            print(message, file=sys.stderr)
        return 1


if __name__ == '__main__':
    if sys.argv[1:] == ['--smoke-test-gui']:
        raise SystemExit(gui_smoke_test())
    raise SystemExit(main())
