#!/usr/bin/env python3
"""Persistent, user-run updater for the public QoL development/beta/release channels."""
import base64
import hashlib
import io
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import types
import urllib.error
import urllib.parse
import urllib.request
import zipfile

UPDATE_SOURCE={'repository':'CryKitty/GTAIV-QoL','branch':'main'}
# Canonical public source. The old GTAIV-QoL-dev repository remains private.
REPO=UPDATE_SOURCE['repository']
BOOTSTRAP_REVISION=5
PROGRESS_UI = ''
PROGRESS = None

def progress(percent,phase,**details):
    if PROGRESS:PROGRESS.update(percent=percent,phase=phase,**details)

def make_progress(title="GTA IV QoL Updates"):
    source=PROGRESS_UI
    if not source and globals().get('__file__'):
        path=Path(__file__).with_name("progress_ui.py")
        if path.is_file():source=path.read_text()
    if not source:return None
    module=types.ModuleType("qol_progress")
    exec(compile(source,"<QoL progress UI>","exec"),module.__dict__)
    window=module.Progress(title,source=source)
    import time
    time.sleep(.15)
    return window

API='https://api.github.com/repos/'+REPO
BRANCH=UPDATE_SOURCE['branch']
ARTIFACT='GTAIV-QoL-update-payload'
ASSET='GTAIV-QoL-update.zip'
FILES={'GTAIV-QoL.asi','GTAIV-QoL-Pause.asi','GTAIV-QoL.ini','install-helper.py','qol-updater.py'}
MAX_ARCHIVE=12*1024*1024
MAX_EXPANDED=16*1024*1024
ROOT=Path.home()/'.local/share/gtaiv-qol-updater'
CONFIG=Path.home()/'.config/gtaiv-qol-updater/config.json'

class AuthError(RuntimeError):pass
class PublicBetaUnavailable(RuntimeError):pass
class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self,req,fp,code,msg,headers,newurl):return None

def dialog(kind,text):
    if shutil.which('kdialog'):
        flag={'info':'--msgbox','error':'--error','password':'--password'}[kind]
        result=subprocess.run(['kdialog','--title','GTA IV QoL Updates',flag,text],capture_output=True,text=True)
    elif shutil.which('zenity'):
        flag={'info':'--info','error':'--error','password':'--password'}[kind]
        args=['zenity','--title','GTA IV QoL Updates',flag]
        args+=['--text',text] if kind!='password' else []
        result=subprocess.run(args,capture_output=True,text=True)
    else:raise RuntimeError('Run the updater in Steam Deck Desktop Mode; a graphical dialog tool is required.')
    if kind=='password':return result.stdout.strip() if result.returncode==0 else ''
    return ''

def atomic(path,data):
    path.parent.mkdir(parents=True,exist_ok=True,mode=0o700)
    tmp=path.with_name(path.name+'.tmp');tmp.write_bytes(data);tmp.chmod(0o600);tmp.replace(path)

def request(token,route,binary=False):
    if route and (not route.startswith('/') or route.startswith('//')):raise RuntimeError('Invalid update request.')
    headers={'User-Agent':'GTAIV-QoL-Updater','Accept':'application/octet-stream' if binary and route.startswith('/releases/assets/') else 'application/vnd.github+json','X-GitHub-Api-Version':'2022-11-28'}
    if token:headers['Authorization']='Bearer '+token
    req=urllib.request.Request(API+route,headers=headers)
    opener=urllib.request.build_opener(NoRedirect())
    try:
        response=opener.open(req,timeout=30)
    except urllib.error.HTTPError as exc:
        if exc.code in (301,302,303,307,308) and binary:
            target=exc.headers.get('Location','');parsed=urllib.parse.urlparse(target)
            host=parsed.hostname or ''
            if parsed.scheme!='https' or parsed.username or parsed.password or not (host.endswith('.blob.core.windows.net') or host.endswith('.githubusercontent.com') or host.endswith('.actions.githubusercontent.com')):
                raise RuntimeError('GitHub returned an unrecognized download destination.') from None
            # Never forward the private GitHub authorization header to artifact storage.
            response=urllib.request.urlopen(urllib.request.Request(target,headers={'User-Agent':'GTAIV-QoL-Updater'}),timeout=30)
        elif exc.code==403 and exc.headers.get('X-RateLimit-Remaining')=='0':raise RuntimeError('GitHub rate limit reached; try again later. Installed files were not changed.') from None
        elif exc.code in (401,403,404):raise AuthError('GitHub access to the QoL update endpoint is unavailable.') from None
        else:raise RuntimeError('GitHub update request failed (HTTP '+str(exc.code)+'). Installed files were not changed.') from None
    with response:
        if binary:
            chunks=[];received=0
            total=int(getattr(response,'headers',{}).get('Content-Length',0) or 0)
            while True:
                chunk=response.read(min(65536,MAX_ARCHIVE+1-received))
                if not chunk:break
                chunks.append(chunk);received+=len(chunk)
                progress(15+40*min(1,received/total) if total else 25,'Downloading update',size=('Download: %.3f MB / %.3f MB'%(received/1000000,total/1000000)) if total else 'Downloaded %.3f MB'%(received/1000000))
                if received>MAX_ARCHIVE:break
            data=b''.join(chunks)
        else:data=response.read(MAX_ARCHIVE+1)
    if len(data)>MAX_ARCHIVE:raise RuntimeError('Update download exceeds the size limit.')
    return data if binary else json.loads(data)

def dev_candidate(token,fetch=request):
    runs=fetch(token,'/actions/workflows/build.yml/runs?'+urllib.parse.urlencode({'branch':BRANCH,'status':'success','per_page':30}))['workflow_runs']
    for run in runs:
        if run.get('status')!='completed' or run.get('conclusion')!='success' or run.get('head_branch')!=BRANCH or run.get('event') not in ('push','workflow_dispatch'):
            continue
        if run.get('head_repository',{}).get('full_name')!=REPO or not re.fullmatch('[0-9a-f]{40}',run.get('head_sha','')):continue
        artifacts=fetch(token,'/actions/runs/'+str(int(run['id']))+'/artifacts?per_page=100')['artifacts']
        for artifact in artifacts:
            origin=artifact.get('workflow_run',{})
            if artifact.get('name')!=ARTIFACT or artifact.get('expired') or origin.get('head_sha')!=run['head_sha'] or origin.get('head_branch')!=BRANCH:continue
            digest=artifact.get('digest','')
            if not re.fullmatch('sha256:[0-9a-f]{64}',digest):continue
            return {'channel':'dev','sha':run['head_sha'],'run_id':int(run['id']),'digest':digest[7:],'route':'/actions/artifacts/'+str(int(artifact['id']))+'/zip','label':'development build '+str(run.get('run_number',run['id'])),'size':artifact.get('size_in_bytes',0),'notes':run.get('head_commit',{}).get('message','')}
    raise RuntimeError('No successful development build with a compatible updater payload is available yet. Installed files were not changed.')

def release_candidate(token,fetch=request):
    for release in fetch(token,'/releases?per_page=30'):
        if release.get('draft') or release.get('prerelease'):continue
        for asset in release.get('assets',[]):
            digest=asset.get('digest','')
            if asset.get('name')==ASSET and re.fullmatch('sha256:[0-9a-f]{64}',digest):
                return {'channel':'release','sha':None,'run_id':None,'digest':digest[7:],'route':'/releases/assets/'+str(int(asset['id'])),'label':'release '+str(release['tag_name']),'size':asset.get('size',0),'notes':release.get('body','')}
    raise RuntimeError('No full release with a compatible updater payload is available yet. Development builds are excluded in release mode.')

def version_key(value):
    match=re.fullmatch(r'v?(\d+)\.(\d+)\.(\d+)(?:-beta\.(\d+))?',value or '')
    if not match:return None
    return tuple(int(match[n]) for n in (1,2,3))+(1 if match[4] is None else 0,int(match[4] or 0))

def beta_candidate(token='',fetch=None):
    # Never pass private development/upload credentials to the public channel.
    fetch=fetch or request
    try:releases=fetch('','/releases?per_page=100')
    except (AuthError,RuntimeError,urllib.error.URLError) as exc:
        raise PublicBetaUnavailable('Public beta checks are unavailable. The bundled archive can still install. Check the connection and try again; no private token is requested.') from None
    releases=[r for r in releases if not r.get('draft') and version_key(r.get('tag_name')) is not None]
    for release in sorted(releases,key=lambda r:version_key(r['tag_name']),reverse=True):
        for asset in release.get('assets',[]):
            digest=asset.get('digest','')
            if asset.get('name')==ASSET and re.fullmatch('sha256:[0-9a-f]{64}',digest):
                return {'channel':'beta','sha':None,'run_id':None,'digest':digest[7:],'route':'/releases/assets/'+str(int(asset['id'])),
                    'label':'beta channel '+release['tag_name'],'version':release['tag_name'].removeprefix('v'),
                    'size':asset.get('size',0),'notes':release.get('body','')}
    return None

def validate_archive(data,candidate):
    if hashlib.sha256(data).hexdigest()!=candidate['digest']:raise RuntimeError('GitHub archive checksum mismatch; nothing installed.')
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        entries=archive.infolist();names=[x.filename for x in entries]
        if len(names)!=len(set(names)) or set(names)!=FILES|{'manifest.json'}:raise RuntimeError('Unexpected update archive contents; nothing installed.')
        if sum(x.file_size for x in entries)>MAX_EXPANDED or any((x.external_attr>>16)&0o170000==0o120000 for x in entries):raise RuntimeError('Invalid update archive entries.')
        contents={name:archive.read(name) for name in names}
    manifest=json.loads(contents.pop('manifest.json'))
    if manifest.get('schema')!=1 or manifest.get('repository')!=REPO or set(manifest.get('files',{}))!=FILES or not re.fullmatch('[0-9a-f]{40}',manifest.get('sha','')):
        raise RuntimeError('Update manifest is incompatible.')
    if candidate['channel'] in ('dev','bundle'):
        branch=BRANCH if candidate['channel']=='dev' else candidate['branch']
        if manifest['sha']!=candidate['sha'] or manifest.get('branch')!=branch or manifest.get('run_id')!=candidate['run_id']:raise RuntimeError('Update manifest does not match the successful build selected.')
    if candidate['channel']=='beta' and (manifest.get('release_version')!=candidate['version'] or manifest.get('branch')!=BRANCH):
        raise RuntimeError('Published beta version/branch does not match its verified payload.')
    for name,raw in contents.items():
        if hashlib.sha256(raw).hexdigest()!=manifest['files'][name]:raise RuntimeError('Update file checksum mismatch; nothing installed.')
    return manifest,contents

def authorize(config):
    dialog('info','GitHub requires authorization to download Actions artifacts, including public development builds. Create or update a fine-grained GitHub token for CryKitty → the new public GTAIV-QoL repository, with Actions: Read and Contents: Read. GTAIV-QoL-dev is the private archive, not the update source. Keep your RoadNav upload token unchanged. Paste the new read-only token into the next password dialog on this Deck; never send it in chat.')
    subprocess.Popen(['xdg-open','https://github.com/settings/personal-access-tokens/new'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    token=dialog('password','Paste the read-only QoL update token. It stays on this Deck.')
    if not token:raise RuntimeError('Update authorization cancelled. Installed game files were not changed.')
    request(token,'');config['token']=token
    atomic(CONFIG,json.dumps(config).encode());return token

def shortcut():
    ROOT.mkdir(parents=True,exist_ok=True,mode=0o700)
    desktop=Path.home()/'Desktop'
    if shutil.which('xdg-user-dir'):
        result=subprocess.run(['xdg-user-dir','DESKTOP'],capture_output=True,text=True)
        if result.returncode==0 and result.stdout.strip():desktop=Path(result.stdout.strip())
    desktop.mkdir(parents=True,exist_ok=True)
    code="import pathlib,runpy;runpy.run_path(str(pathlib.Path.home()/'.local/share/gtaiv-qol-updater/qol-updater.py'),run_name='__main__')"
    text='[Desktop Entry]\nType=Application\nName=Update GTA IV QoL\nIcon=system-software-update\nTerminal=false\nExec=/usr/bin/python3 -c "'+code+'"\n'
    target=desktop/'Update-GTAIV-QoL.desktop';target.write_text(text);target.chmod(0o755)

def apply_update(manifest,files,channel):
    if Path('/proc').is_dir():
        for path in Path('/proc').glob('[0-9]*/comm'):
            try:running=path.read_text().strip().lower() in ('gtaiv.exe','gtaiv')
            except OSError:continue
            if running:raise RuntimeError('Close GTA IV before installing an update.')
    # Only code from the authenticated successful repository build is executed.
    module=types.ModuleType('qol_verified_installer')
    exec(compile(files['install-helper.py'],'<verified QoL installer>','exec'),module.__dict__)
    module.BUNDLE={name:{'data':base64.b64encode(files[name]).decode(),'sha256':manifest['files'][name]} for name in ('GTAIV-QoL.asi','GTAIV-QoL-Pause.asi','GTAIV-QoL.ini')}
    module.INSTALL_BUILD=manifest['sha'][:12]
    module.progress=progress
    progress(65,'Backing up and installing files')
    messages=[]
    module.dialog=lambda message,error=False:messages.append(message)
    module.main() # existing game-session lock, backups, duplicate removal, log upgrade
    progress(95,'Finishing installation')
    atomic(ROOT/'qol-updater.py',files['qol-updater.py'])
    atomic(ROOT/'installed.json',json.dumps({'sha':manifest['sha'],'files':manifest['files'],'channel':channel,'version':manifest.get('release_version',''),'repository':REPO,'branch':BRANCH}).encode())
    shortcut()

def bundle_files_intact(game,manifest):
    if not all((game/name).is_file() and hashlib.sha256((game/name).read_bytes()).hexdigest()==manifest['files'][name] for name in ('GTAIV-QoL.asi','GTAIV-QoL-Pause.asi')):
        return False
    state=game/'GTAIV-QoL-bundle-state.json'
    try:
        saved=json.loads(state.read_text())
        relative=Path(saved['fusionfix_path'])
        if relative.is_absolute() or '..' in relative.parts or len(relative.parts)>2:return False
        target=game/relative
        if not target.is_file() or target.is_symlink() or hashlib.sha256(target.read_bytes()).hexdigest()!=saved['fusionfix_sha256']:return False
        for folder in (game,game/'plugins',game/'scripts'):
            if folder.is_dir() and any(p.name.lower()=='roadnav.asi' for p in folder.iterdir()):return False
    except (OSError,ValueError,KeyError,TypeError):return False
    return True

def updater_files_intact(saved):
    try:
        path=ROOT/'qol-updater.py'
        return (saved.get('repository')==REPO and saved.get('branch')==BRANCH and
                path.is_file() and not path.is_symlink() and
                hashlib.sha256(path.read_bytes()).hexdigest()==saved['files']['qol-updater.py'])
    except (OSError,KeyError,TypeError):return False

def perform_update():
    progress(3,"Checking for updates")
    os.umask(0o077)
    ROOT.mkdir(parents=True,exist_ok=True,mode=0o700)
    import fcntl
    with (ROOT/'update.lock').open('a') as lock:
        try:fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
        except BlockingIOError:raise RuntimeError('Another QoL update check is already running.') from None
        config=json.loads(CONFIG.read_text()) if CONFIG.exists() else {'channel':'dev'}
        if '--releases' in sys.argv:config['channel']='release'
        if '--development' in sys.argv:config['channel']='dev'
        if '--betas' in sys.argv:config['channel']='beta'
        channel=config.get('channel','dev')
        if channel not in ('dev','release','beta'):raise RuntimeError('Unknown update channel.')
        token='' if channel=='beta' else config.get('token','')
        select=beta_candidate if channel=='beta' else dev_candidate if channel=='dev' else release_candidate
        try:candidate=select(token)
        except AuthError:
            if channel=='beta':raise PublicBetaUnavailable('Public beta download is unavailable; no private token is requested.') from None
            token=authorize(config);candidate=select(token)
        if candidate is None:
            progress(100,'No public beta update has been published yet');return
        if channel=='beta' and (ROOT/'installed.json').is_file() and config.get('game'):
            saved=json.loads((ROOT/'installed.json').read_text())
            previous,next_version=version_key(saved.get('version')),version_key(candidate.get('version'))
            if previous and next_version and previous>next_version and bundle_files_intact(Path(config['game']),saved):
                progress(100,'Installed beta is newer than the published update; kept');return
        notes=candidate.get('notes','')
        installed=ROOT/'installed.json'
        if channel=='dev' and installed.is_file():
            try:
                previous=json.loads(installed.read_text()).get('sha','')
                if re.fullmatch('[0-9a-f]{40}',previous) and previous!=candidate['sha']:
                    compare=request(token,'/compare/'+previous+'...'+candidate['sha'])
                    notes='\n\n'.join(item['commit']['message'] for item in compare.get('commits',[])[:40]) or notes
            except (OSError,ValueError,RuntimeError,urllib.error.URLError):pass
        progress(12,'Preparing download',version=candidate['label'].capitalize(),size='Download: %.3f MB'%(candidate.get('size',0)/1000000),changelog=notes)
        data=request(token,candidate['route'],binary=True)
        progress(58,'Verifying download')
        manifest,files=validate_archive(data,candidate)
        if manifest.get('changelog'):progress(60,'Download verified',changelog=str(manifest['changelog'])[:20000])
        state=ROOT/'installed.json'
        if state.exists() and json.loads(state.read_text()).get('sha')==manifest['sha']:
            roadnav=Path.home()/'.config/roadnav-uploader/config.json'
            game=Path(config['game']) if config.get('game') else Path(json.loads(roadnav.read_text())['game'])
            if bundle_files_intact(game,manifest) and updater_files_intact(json.loads(state.read_text())):
                atomic(CONFIG,json.dumps(config).encode());shortcut();progress(100,'Already up to date');return
        apply_update(manifest,files,channel)
        atomic(CONFIG,json.dumps(config).encode())
        progress(100,'Update complete — ready to play')

# In-game bridge: a Linux user service watches the shared game folder. The ASI
# queues requests; it never tries to execute Linux Python through Wine.
def selected_game():
    for path in (CONFIG,Path.home()/'.config/roadnav-uploader/config.json'):
        try:
            value=json.loads(path.read_text()).get('game')
            if value:return Path(value).resolve()
        except (OSError,ValueError,TypeError):pass
    return None

def game_running():
    for path in Path('/proc').glob('[0-9]*/comm'):
        try:
            if path.read_text().strip().lower() in ('gtaiv.exe','gtaiv'):return True
        except OSError:pass
    return False

def update_opt_in(game):
    import configparser
    try:
        ini=configparser.ConfigParser(interpolation=None,strict=False)
        ini.read_string((game/'GTAIV-QoL.ini').read_text(encoding='utf-8-sig'))
        return ini.getint('Updates','CheckForUpdates',fallback=0)==1
    except (OSError,ValueError,configparser.Error):return False

def install_game_updater(game,source):
    # Keep the existing five-file archive format: this verified updater is its
    # own worker. No unsigned extra program is fetched at installation time.
    if sys.platform=='win32':return False # Deck/Linux installer owns this bridge.
    game=Path(game).resolve()
    if any(ord(c)<32 for c in str(game)):raise RuntimeError('Unsupported game path.')
    atomic(game/'GTAIV-QoL-Updater.py',source)
    unit=Path.home()/'.config/systemd/user/gtaiv-qol-updates.service'
    def quote(value):
        return '"'+str(value).replace('\\','\\\\').replace('"','\\"').replace('%','%%').replace('$','$$')+'"'
    text='[Unit]\nDescription=GTA IV QoL opt-in update staging\n[Service]\nType=simple\nExecStart=/usr/bin/python3 '+quote(game/'GTAIV-QoL-Updater.py')+' --watch-game '+quote(game)+'\nRestart=always\nRestartSec=5\n[Install]\nWantedBy=default.target\n'
    atomic(unit,text.encode())
    # A manual/first-time install reloads the worker (including a changed game
    # path). During the worker's own apply, do not stop ourselves mid-transaction;
    # watch_game returns after success and systemd reloads the new script.
    action='start' if '--watch-game' in sys.argv else 'restart'
    for args in (['daemon-reload'],['enable','gtaiv-qol-updates.service'],[action,'gtaiv-qol-updates.service']):
        result=subprocess.run(['systemctl','--user']+args,capture_output=True,timeout=15)
        if result.returncode:raise RuntimeError('In-game update worker could not be enabled. The manual updater remains available; rerun setup in Desktop Mode.')
    return True

def pending_directory(game):
    return ROOT/'staged'/hashlib.sha256(str(Path(game).resolve()).encode()).hexdigest()[:24]

def update_status(game,state):
    atomic(game/'GTAIV-QoL-update-status.json',json.dumps({'schema':1,'state':state}).encode())

def stage_game_update(game):
    if not update_opt_in(game):return False
    config=json.loads(CONFIG.read_text());channel=config.get('channel','beta')
    if selected_game()!=game.resolve():raise RuntimeError('Configured game does not match update worker.')
    select={'dev':dev_candidate,'beta':beta_candidate,'release':release_candidate}.get(channel)
    if select is None:raise RuntimeError('Unsupported channel.')
    token='' if channel=='beta' else config.get('token','')
    candidate=select(token)
    if candidate is None:update_status(game,'No published update available');return False
    saved=json.loads((ROOT/'installed.json').read_text()) if (ROOT/'installed.json').is_file() else {}
    if candidate.get('sha') and candidate['sha']==saved.get('sha'):
        update_status(game,'Already up to date');return False
    current,next_version=version_key(saved.get('version')),version_key(candidate.get('version'))
    if channel=='beta' and current and next_version and current>=next_version:
        update_status(game,'Already up to date');return False
    data=request(token,candidate['route'],binary=True)
    manifest,files=validate_archive(data,candidate)
    if manifest['sha']==saved.get('sha'):
        update_status(game,'Already up to date');return False
    if not update_opt_in(game):return False
    folder=pending_directory(game)
    atomic(folder/'payload.zip',data)
    candidate=dict(candidate,installed_sha=saved.get('sha',''))
    atomic(folder/'candidate.json',json.dumps(candidate).encode())
    update_status(game,'Update staged; applies after GTA IV exits')
    return True

def apply_game_update(game):
    folder=pending_directory(game);candidate_file=folder/'candidate.json';payload=folder/'payload.zip'
    if not candidate_file.is_file():return False
    if game_running():return False
    if not update_opt_in(game):
        candidate_file.unlink();payload.unlink(missing_ok=True);update_status(game,'Update cancelled: option Off');return False
    if candidate_file.is_symlink() or payload.is_symlink() or payload.stat().st_size>MAX_ARCHIVE:raise RuntimeError('Invalid staged payload.')
    candidate=json.loads(candidate_file.read_text())
    saved=json.loads((ROOT/'installed.json').read_text()) if (ROOT/'installed.json').is_file() else {}
    config=json.loads(CONFIG.read_text())
    if candidate.get('installed_sha','')!=saved.get('sha','') or candidate.get('channel')!=config.get('channel','beta'):
        candidate_file.unlink();payload.unlink(missing_ok=True);update_status(game,'Staged update cancelled: installation/channel changed');return False
    manifest,files=validate_archive(payload.read_bytes(),candidate)
    if game_running():return False
    # apply_update checks the running process again and uses the existing
    # installer session lock, backups, settings preservation and rollback.
    apply_update(manifest,files,candidate['channel'])
    candidate_file.unlink();payload.unlink(missing_ok=True)
    update_status(game,'Update installed; ready for next launch')
    return True

def game_update_tick(game):
    import fcntl
    ROOT.mkdir(parents=True,exist_ok=True,mode=0o700)
    with (ROOT/'update.lock').open('a') as lock:
        try:fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
        except BlockingIOError:return False
        if selected_game()!=game.resolve() or not (game/'GTAIV.exe').is_file():return False
        request_file=game/'GTAIV-QoL-update-request.txt'
        if update_opt_in(game) and request_file.is_file() and not request_file.is_symlink():
            if request_file.stat().st_size>96:raise RuntimeError('Invalid update request.')
            request_file.unlink() # One network check per startup / Off->On edge.
            stage_game_update(game)
        return apply_game_update(game)

def watch_game(game):
    import time
    os.umask(0o077);game=Path(game).resolve()
    while True:
        try:
            if game_update_tick(game):return 0 # systemd restarts the new worker.
        except Exception as exc:
            # No credentials, API bodies or private file paths in status output.
            update_status(game,'Check/apply deferred ('+type(exc).__name__+'); manual updater remains available')
        time.sleep(3)


def main(window=None):
    global PROGRESS
    if window is not None:
        PROGRESS=window
        perform_update();return 0
    PROGRESS=make_progress()
    try:
        perform_update()
        if PROGRESS and PROGRESS.alive():PROGRESS.finish(PROGRESS.data.get('phase','Ready to play'));PROGRESS.wait()
        else:dialog('info','GTA IV QoL is ready to play.')
        return 0
    except Exception as exc:
        message=str(exc) if isinstance(exc,RuntimeError) else 'Update could not finish ('+type(exc).__name__+'). Please try again.'
        if PROGRESS and PROGRESS.alive():PROGRESS.finish(message,error=True);PROGRESS.wait()
        else:dialog('error',message)
        return 1
    finally:
        if PROGRESS and not PROGRESS.alive():PROGRESS.wait()
        PROGRESS=None


if __name__=='__main__':
    try:sys.exit(watch_game(sys.argv[2]) if len(sys.argv)==3 and sys.argv[1]=='--watch-game' else main())
    except Exception as exc:
        message=str(exc) if isinstance(exc,RuntimeError) else 'The updater could not finish ('+type(exc).__name__+'). Check the connection and saved authorization; existing backups and logs remain on the Deck.'
        dialog('error',message);sys.exit(1)
