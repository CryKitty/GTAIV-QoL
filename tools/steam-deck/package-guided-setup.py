"""Package the guided installer as one desktop launcher, with no nested archive."""
import base64
from pathlib import Path
import zlib
import hashlib
import json
import io
import zipfile


def launcher(folder, bundle=None):
    folder=Path(folder)
    dependency=(folder/'dependency_setup.py').read_text(encoding='utf-8')
    updater=(folder/'qol-updater.py').read_text(encoding='utf-8')
    ui=folder/'progress_ui.py'
    if ui.is_file():updater=updater.replace("PROGRESS_UI = ''",'PROGRESS_UI = '+repr(ui.read_text(encoding='utf-8')))
    wrapper=(folder/'guided-setup.py').read_text(encoding='utf-8')
    extra=''
    if bundle is not None:
        with zipfile.ZipFile(io.BytesIO(bundle)) as archive:manifest=json.loads(archive.read('manifest.json'))
        candidate={'channel':'bundle','sha':manifest['sha'],'branch':manifest['branch'],'run_id':manifest['run_id'],'digest':hashlib.sha256(bundle).hexdigest()}
        extra=", (Path(__file__).with_name('GTAIV-QoL-update.zip'), "+repr(candidate)+')'
    code=wrapper.split("if __name__=='__main__':")[0]+ '\nraise SystemExit(run('+repr(dependency)+','+repr(updater)+extra+'))\n'
    payload=base64.b64encode(zlib.compress(code.encode(),9)).decode()
    if bundle is not None:
        # Keep Exec below Linux's per-argument limit. Code lives in the launcher,
        # payload beside it; spaces and file:// desktop paths are supported.
        command="import base64,pathlib,sys,urllib.parse,zlib;u=urllib.parse.urlparse(sys.argv[1]);p=pathlib.Path(urllib.request.url2pathname(u.path) if u.scheme=='file' else sys.argv[1]);exec(zlib.decompress(base64.b64decode(p.read_text().split(chr(10)+'#QOLGUIDED=',1)[1])),{'__name__':'__main__','__file__':str(p)})"
        command=command.replace('import base64,pathlib,sys,urllib.parse,zlib','import base64,pathlib,sys,urllib.parse,urllib.request,zlib')
        return '[Desktop Entry]\nType=Application\nName=Install GTA IV QoL Beta\nIcon=applications-games\nTerminal=false\nExec=/usr/bin/python3 -c "'+command+'" "%k"\n#QOLGUIDED='+payload+'\n'
    return '[Desktop Entry]\nType=Application\nName=Install GTA IV QoL\nComment=Find GTA IV, set up dependencies and install the updating QoL bundle\nIcon=applications-games\nTerminal=false\nExec=/usr/bin/python3 -c "import base64,zlib;exec(zlib.decompress(base64.b64decode(\''+payload+'\')))"\n'


if __name__=='__main__':
    here=Path(__file__).parent;out=here/'dist';out.mkdir(exist_ok=True)
    p=out/'Install-GTAIV-QoL.desktop';p.write_text(launcher(here),encoding='utf-8');p.chmod(0o755)
    (out/'INSTALL-START-HERE.txt').write_text('Close GTA IV. Extract this one artifact ZIP and run Install-GTAIV-QoL.desktop in Desktop Mode. The installer finds GTA IV in Steam libraries and SD cards, checks dependencies, downloads missing ASI loader/ScriptHook from their authors, opens the Complete Edition patch page, and imports its completed ZIP from Downloads when you click Retry. It installs the matching FusionFix assets/menu host and the newest successful QoL build, then creates Update GTA IV QoL on your Desktop. Existing mod settings and automatic logging are kept. Fresh installs do not require a log-upload token; the public main development updater needs read-only GitHub authorization for Actions artifact downloads. Setup can run and re-check the verified Microsoft X86 installer in the selected GTA IV Proton prefix. This compatibility development bootstrap remains available; Nexus users use the integrated bundled installer instead of downloading a separate updater.\n',encoding='utf-8')
    print(p)
