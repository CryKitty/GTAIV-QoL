"""Build a self-contained beta download; never upload or publish it."""
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import re
import zipfile

COMPONENTS = ['Road Navigation Line', 'RoadNav turn guidance', 'Map Autozoom',
              'Pause Cutscenes', 'Cutscene Audio', 'Compass HUD', 'Taxi FOV Slider']

def load(name, path):
    spec=importlib.util.spec_from_file_location(name,path)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module

def package(root):
    root=Path(root);folder=root/'tools/steam-deck'
    version=(root/'VERSION').read_text().strip()
    if not re.fullmatch(r'\d+\.\d+\.\d+-beta\.\d+',version):raise RuntimeError('Expected a beta version.')
    data=(root/'build/GTAIV-QoL-update.zip').read_bytes()
    updater=load('nexus_updater',folder/'qol-updater.py')
    if len(data)>updater.MAX_ARCHIVE:raise RuntimeError('Bundled update exceeds the updater size limit.')
    with zipfile.ZipFile(io.BytesIO(data)) as archive:manifest=json.loads(archive.read('manifest.json'))
    candidate={'channel':'bundle','sha':manifest['sha'],'branch':manifest['branch'],'run_id':manifest['run_id'],'digest':hashlib.sha256(data).hexdigest()}
    updater.validate_archive(data,candidate)
    if manifest.get('release_version')!=version:raise RuntimeError('Payload version does not match VERSION.')
    guided=load('nexus_guided',folder/'package-guided-setup.py')
    entries={'Install-GTAIV-QoL.desktop':guided.launcher(folder,data).encode(), 'GTAIV-QoL-update.zip':data}
    for name,source in {'README.md':'README.md','CHANGELOG.md':'CHANGELOG.md','LICENSE':'LICENSE',
                        'BUNDLE-INVENTORY.md':'docs/BUNDLE_INVENTORY.md',
                        'KNOWN-ISSUES.md':'docs/KNOWN_ISSUES.md',
                        'THIRD-PARTY-NOTICES.txt':'build/FusionFix-GPL-3.0.txt'}.items():
        entries[name]=(root/source).read_bytes()
    entries['START-HERE.txt']=(
        'GTAIV-QoL '+version+' — Steam Deck beta bundle\n\n'
        'Close GTA IV. Extract this entire ZIP into one folder, then run Install-GTAIV-QoL.desktop '
        'in Desktop Mode (Execute/Trust if prompted). Keep GTAIV-QoL-update.zip beside the launcher. '
        'The guided installer finds the game, checks dependencies and installs this exact bundled beta. '
        'No private-repository or log-upload token is needed for this installation. '
        'The GUI can download verified dependencies or import your completed ZIP/EXE downloads on Retry. '
        'It places the files, runs the verified Microsoft X86 installer in GTA IV\'s selected Proton prefix, '
        'and re-checks everything before enabling Install. No manual game-file or INI editing is required.\n\n'
        'After installation use the integrated Update GTA IV QoL shortcut for public GitHub beta checks. '
        'Re-running this installed-version launcher also hands off to the updater. '
        'Existing development users migrate once to the new public main build; their channel/menu choices remain. '
        'The old GTAIV-QoL-dev archive stays private. No beta release is published yet; until a public beta update asset exists, '
        'the verified bundled archive can install without private credentials. See README.md.\n' ).encode()
    entries['release-info.json']=json.dumps({'version':version,'commit':manifest['sha'],'run_id':manifest['run_id'],
        'canonical_branch':updater.BRANCH,'source_branch':manifest['branch'],'components':COMPONENTS,
        'payload_sha256':candidate['digest']},indent=2).encode()
    out=root/'build'/('GTAIV-QoL-'+version+'-Deck.zip')
    with zipfile.ZipFile(out,'w',zipfile.ZIP_DEFLATED) as archive:
        for name,raw in entries.items():
            info=zipfile.ZipInfo(name);info.external_attr=(0o100755 if name.endswith('.desktop') else 0o100644)<<16
            info.compress_type=zipfile.ZIP_DEFLATED;archive.writestr(info,raw)
    with zipfile.ZipFile(out) as archive:
        if archive.testzip() is not None or set(archive.namelist())!=set(entries):raise RuntimeError('Nexus archive verification failed.')
        # Inspect the finished downloadable ZIP, not only its staging inputs.
        info=json.loads(archive.read('release-info.json'));nested=archive.read('GTAIV-QoL-update.zip')
        if info['components']!=COMPONENTS or info['commit']!=manifest['sha'] or info['version']!=version:
            raise RuntimeError('Nexus archive identity/inventory mismatch.')
        if hashlib.sha256(nested).hexdigest()!=info['payload_sha256']:raise RuntimeError('Nexus payload read-back mismatch.')
        updater.validate_archive(nested,candidate)
        if archive.read('LICENSE')!=(root/'LICENSE').read_bytes():raise RuntimeError('Nexus licence read-back mismatch.')
    print('Nexus ZIP read-back verified: seven components, both ASIs and matching payload/file hashes; '+version+' @ '+manifest['sha'])
    print(out);return out

if __name__=='__main__':package(Path(__file__).resolve().parents[2])
