"""Self-contained Deck installer: exact CI binaries + INI + uploader upgrade."""
import base64
import hashlib
import os
from pathlib import Path
import importlib.util
_asset_spec=importlib.util.spec_from_file_location('qol_bundle_assets',Path(__file__).with_name('bundle_assets.py'))
_assets=importlib.util.module_from_spec(_asset_spec);_asset_spec.loader.exec_module(_assets)
import zipfile

def make_launcher(helper):
    payload=base64.b64encode(helper.encode()).decode()
    # Read embedded payload from our own desktop file; keep Exec below Linux's
    # single-argument size limit even when both ASIs are embedded.
    code="import base64,pathlib,sys,urllib.parse,urllib.request;u=urllib.parse.urlparse(sys.argv[1]);p=urllib.request.url2pathname(u.path) if u.scheme=='file' else sys.argv[1];s=pathlib.Path(p).read_text();exec(base64.b64decode(s.split(chr(10)+'#QOLPAYLOAD=',1)[1]))"
    return ('[Desktop Entry]\nType=Application\nName=Install GTA IV QoL native pause test\nIcon=applications-games\nTerminal=false\nExec=/usr/bin/python3 -c "'+code+'" "%k"\n#QOLPAYLOAD='+payload+'\n')

def main():
    root=Path(__file__).resolve().parents[2]
    helper=_assets.render_helper(root)
    bundle={}
    for name,path in [('GTAIV-QoL.asi',root/'build/GTAIV-QoL.asi'),('GTAIV-QoL-Pause.asi',root/'build/GTAIV-QoL-Pause.asi'),('GTAIV-QoL.ini',root/'GTAIV-QoL.ini.example')]:
        data=path.read_bytes()
        bundle[name]={'data':base64.b64encode(data).decode(),'sha256':hashlib.sha256(data).hexdigest()}
    helper=helper.replace('BUNDLE = {}','BUNDLE = '+repr(bundle)).replace("INSTALL_BUILD = ''",'INSTALL_BUILD = '+repr(os.environ.get('GITHUB_SHA','local')[:12]))
    out=root/'tools/steam-deck/dist';out.mkdir(exist_ok=True)
    path=out/'Install-QoL-Pause-Test.desktop';path.write_text(make_launcher(helper));path.chmod(0o755)
    instructions = 'Close GTA IV. In Desktop Mode extract this ZIP anywhere and run Install-QoL-Pause-Test.desktop (Execute/Trust if KDE asks). It installs the exact ASIs and INI into your saved RoadNav game folder, backs up previous QoL files and disables duplicate QoL ASIs. No manual copying, token or INI edits. Launch normally; during a cutscene press Esc or controller Start, to attempt pause. Repeat to resume. Pause stays active until you resume it; there is no time limit. Observe BOTH motion and dialogue. Exit normally to upload the logs. This is an experimental native pause attempt, not a proven synchronized pauser.\n'
    (out/'START-HERE.txt').write_text(instructions)
    with zipfile.ZipFile(out/'GTAIV-QoL-Pause-Test-Deck.zip','w',zipfile.ZIP_DEFLATED) as archive:
        entry=zipfile.ZipInfo(path.name);entry.create_system=3;entry.external_attr=0o100755<<16
        archive.writestr(entry,path.read_bytes(),compress_type=zipfile.ZIP_DEFLATED)
        archive.writestr('START-HERE.txt',instructions)
    print(out/'GTAIV-QoL-Pause-Test-Deck.zip')

if __name__=='__main__':
    main()
