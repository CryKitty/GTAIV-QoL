"""CI build only. Frozen Python/Tk is a bootstrap, never part of mod payload."""
import hashlib
import importlib.metadata
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import zipfile

PAYLOAD_SHA256='132c19079346dea95bed66135347308d9403d9631cc3cb43be7e0c9c01daf9a3'
SOURCE_SHA='a182069aaf24fb5bc462e449efc4b147d2c9db69'

def prepare(root,payload):
    root=Path(root);payload=Path(payload);raw=payload.read_bytes()
    if hashlib.sha256(raw).hexdigest()!=PAYLOAD_SHA256:raise RuntimeError('Only the released 1.0.0 payload may be packaged.')
    with zipfile.ZipFile(payload) as z:manifest=json.loads(z.read('manifest.json'))
    if manifest['sha']!=SOURCE_SHA or manifest['release_version']!='1.0.0':raise RuntimeError('Released payload identity mismatch.')
    folder=root/'tools/steam-deck'
    sources={key:(folder/name).read_text(encoding='utf-8') for key,name in [('dependency','dependency_setup.py'),('guide','guided-setup.py'),('updater','qol-updater.py')]}
    candidate={'channel':'bundle','sha':manifest['sha'],'branch':manifest['branch'],'run_id':manifest['run_id'],'digest':PAYLOAD_SHA256}
    out=root/'build/windows-source';out.mkdir(parents=True,exist_ok=True)
    for name in ('bootstrap.py','platform_adapter.py','progress_ui.py'):(out/name).write_bytes((root/'tools/windows'/name).read_bytes())
    (out/'embedded_sources.py').write_text('SOURCES='+repr(sources)+'\nCANDIDATE='+repr(candidate)+'\n',encoding='utf-8')
    return out

def main():
    root=Path(__file__).resolve().parents[2];payload=Path(sys.argv[1]).resolve();out=prepare(root,payload)
    subprocess.run([sys.executable,'-m','PyInstaller','--noconfirm','--clean','--onefile','--windowed','--name','Install-GTAIV-QoL','--distpath',str(root/'build/windows-launcher'),'--workpath',str(root/'build/windows-pyinstaller'),'--specpath',str(root/'build'),str(out/'bootstrap.py')],check=True)
    target=root/'build/windows-launcher';licences=[]
    python_license=Path(sys.base_prefix)/'LICENSE.txt';licences.append(python_license.read_text())
    distribution=importlib.metadata.distribution('pyinstaller')
    found=[distribution.locate_file(p) for p in distribution.files if p.name.lower() in ('copying.txt','license','license.txt')]
    if not found:raise RuntimeError('PyInstaller licence notice missing.')
    licences.extend(p.read_text() for p in found)
    tk=list((Path(sys.base_prefix)/'tcl').glob('*/license.terms'))
    if len(tk)<2:raise RuntimeError('Tcl/Tk licence notices missing.')
    licences.extend(p.read_text() for p in tk)
    (target/'WINDOWS-RUNTIME-LICENSES.txt').write_text('\n\n'.join(licences),encoding='utf-8')
    spec=importlib.util.spec_from_file_location('nexus_package',root/'tools/steam-deck/package-nexus.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    print(module.package_windows(root,target/'Install-GTAIV-QoL.exe',payload))
if __name__=='__main__':main()
