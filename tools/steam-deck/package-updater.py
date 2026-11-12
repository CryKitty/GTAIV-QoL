"""Build data payload plus a small, reusable graphical updater bootstrap."""
import base64
import hashlib
import json
import re
import os
from pathlib import Path
import importlib.util
_asset_spec=importlib.util.spec_from_file_location('qol_bundle_assets',Path(__file__).with_name('bundle_assets.py'))
_assets=importlib.util.module_from_spec(_asset_spec);_asset_spec.loader.exec_module(_assets)
import shutil

def render_updater(folder):
    folder=Path(folder);source=(folder/'qol-updater.py').read_text(encoding='utf-8')
    ui=folder/'progress_ui.py'
    if ui.is_file():source=source.replace("PROGRESS_UI = ''",'PROGRESS_UI = '+repr(ui.read_text(encoding='utf-8')))
    return source.encode()

def launcher(source):
    revision=int((re.search(rb'^BOOTSTRAP_REVISION=([0-9]+)',source,re.M) or [None,b'0'])[1])
    code="import base64,pathlib,re,sys,urllib.parse,urllib.request,runpy;u=urllib.parse.urlparse(sys.argv[1]);p=urllib.request.url2pathname(u.path) if u.scheme=='file' else sys.argv[1];s=pathlib.Path(p).read_text();r=pathlib.Path.home()/'.local/share/gtaiv-qol-updater';r.mkdir(parents=True,exist_ok=True,mode=448);d=r/'qol-updater.py';d.write_bytes(base64.b64decode(s.split(chr(10)+'#QOLPAYLOAD=',1)[1])) if not d.exists() or int((re.search('^BOOTSTRAP_REVISION=([0-9]+)',d.read_text(),re.M) or [None,'0'])[1])<"+str(revision)+" else None;runpy.run_path(str(d),run_name='__main__')"
    return '[Desktop Entry]\nType=Application\nName=Update GTA IV QoL\nIcon=system-software-update\nTerminal=false\nExec=/usr/bin/python3 -c "'+code+'" "%k"\n#QOLPAYLOAD='+base64.b64encode(source).decode()+'\n'

def main():
    root=Path(__file__).resolve().parents[2]
    out=root/'build/update-payload';out.mkdir(parents=True,exist_ok=True)
    sources={'GTAIV-QoL.asi':root/'build/GTAIV-QoL.asi','GTAIV-QoL-Pause.asi':root/'build/GTAIV-QoL-Pause.asi','GTAIV-QoL.ini':root/'GTAIV-QoL.ini.example','install-helper.py':root/'tools/steam-deck/enable-qol-logs.py','qol-updater.py':root/'tools/steam-deck/qol-updater.py'}
    files={}
    for name,path in sources.items():
        raw=_assets.render_helper(root).encode() if name=='install-helper.py' else render_updater(path.parent) if name=='qol-updater.py' else path.read_bytes();(out/name).write_bytes(raw);files[name]=hashlib.sha256(raw).hexdigest()
    if sum((out/name).stat().st_size for name in files)>16*1024*1024-4096:raise RuntimeError('Bundle exceeds installed updater size bound')
    source=_assets.update_source(root)
    manifest={'schema':1,'repository':source['repository'],'sha':os.environ.get('GITHUB_SHA','0'*40),'branch':os.environ.get('GITHUB_REF_NAME',source['branch']),'run_id':int(os.environ.get('GITHUB_RUN_ID','0')),'files':files}
    if os.environ.get('GITHUB_REF_TYPE')=='tag':manifest['branch']=source['branch']
    if (root/'VERSION').is_file():manifest['release_version']=(root/'VERSION').read_text().strip()
    notes=root/'CHANGELOG.md'
    if notes.is_file():manifest['changelog']=notes.read_text(encoding='utf-8')[:20000]
    manifest['version']=os.environ.get('GITHUB_RUN_NUMBER','development')
    (out/'manifest.json').write_text(json.dumps(manifest,indent=2))
    # Future release publishing can attach this exact schema as GTAIV-QoL-update.zip.
    shutil.make_archive(str(root/'build/GTAIV-QoL-update'),'zip',out)
    dist=root/'tools/steam-deck/dist';dist.mkdir(exist_ok=True)
    path=dist/'Update-GTAIV-QoL.desktop';path.write_text(launcher(render_updater(sources['qol-updater.py'].parent)));path.chmod(0o755)
    (dist/'UPDATER-START-HERE.txt').write_text('Close GTA IV, extract the ZIP and run Update-GTAIV-QoL.desktop in Desktop Mode (Execute/Trust if asked). This installs or refreshes the persistent updater, including migration from the old development branch. It checks the newest successful non-PR build.yml run on CryKitty/GTAIV-QoL main. Existing channel/menu choices remain; new Nexus installs use the published public beta channel instead. GitHub Actions artifact downloads require a signed-in account/read-only token even for public development builds: select the new public GTAIV-QoL repository with Actions: Read and Contents: Read. Public beta release downloads need no token; none has been published yet. Never change the separate RoadNav upload token. Future checks use Update GTA IV QoL on your Desktop; no file/INI editing. See docs/UPDATER.md for migration and channel details.\n')
    print(dist/'Update-GTAIV-QoL.desktop')

if __name__=='__main__':main()
