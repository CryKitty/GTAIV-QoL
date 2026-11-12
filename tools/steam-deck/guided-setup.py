"""QoL wrapper around the proven RoadNav guided dependency installer."""
import hashlib
import io
import json
import os
from pathlib import Path
import sys
import types
import zipfile
import importlib.util
import ast
from contextlib import contextmanager

FUSIONFIX = {'url':'https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix/releases/download/v5.1.2/GTAIV.EFLC.FusionFix.zip',
             'archive_sha256':'138f4067a2d2f09df2df80c4378407ad7c742be0e3b99f090633d50820bb2450'}


# Required asset paths read from the checksum-verified official 5.1.2 archive.
FUSIONFIX_ASSETS = (
    'update/common/data/frontend_menus.xml',
    'update/common/data/frontend_pc.dat',
    'update/common/data/visualsettings.dat',
    'update/common/shaders/db/gta_trees_extended.sps',
    'update/common/shaders/dcl/gta_trees_extended.dcl',
    'update/common/shaders/win32_30/cascadeConvert.fxc',
    'update/common/shaders/win32_30/cascadeGen.fxc',
    'update/common/shaders/win32_30/deferred_lighting.fxc',
    'update/common/shaders/win32_30/gpuptfx_simplerender.fxc',
    'update/common/shaders/win32_30/gpuptfx_update.fxc',
    'update/common/shaders/win32_30/gta_atmoscatt_clouds.fxc',
    'update/common/shaders/win32_30/gta_cubemap_reflect.fxc',
    'update/common/shaders/win32_30/gta_cutout_fence.fxc',
    'update/common/shaders/win32_30/gta_decal.fxc',
    'update/common/shaders/win32_30/gta_decal_amb_only.fxc',
    'update/common/shaders/win32_30/gta_decal_dirt.fxc',
    'update/common/shaders/win32_30/gta_decal_glue.fxc',
    'update/common/shaders/win32_30/gta_decal_normal_only.fxc',
    'update/common/shaders/win32_30/gta_default.fxc',
    'update/common/shaders/win32_30/gta_diffuse_instance.fxc',
    'update/common/shaders/win32_30/gta_emissive.fxc',
    'update/common/shaders/win32_30/gta_emissivenight.fxc',
    'update/common/shaders/win32_30/gta_emissivestrong.fxc',
    'update/common/shaders/win32_30/gta_glass.fxc',
    'update/common/shaders/win32_30/gta_glass_emissive.fxc',
    'update/common/shaders/win32_30/gta_glass_emissivenight.fxc',
    'update/common/shaders/win32_30/gta_glass_normal_spec_reflect.fxc',
    'update/common/shaders/win32_30/gta_glass_reflect.fxc',
    'update/common/shaders/win32_30/gta_glass_spec.fxc',
    'update/common/shaders/win32_30/gta_grass.fxc',
    'update/common/shaders/win32_30/gta_hair_sorted_alpha.fxc',
    'update/common/shaders/win32_30/gta_hair_sorted_alpha_exp.fxc',
    'update/common/shaders/win32_30/gta_im.fxc',
    'update/common/shaders/win32_30/gta_normal.fxc',
    'update/common/shaders/win32_30/gta_normal_cubemap_reflect.fxc',
    'update/common/shaders/win32_30/gta_normal_decal.fxc',
    'update/common/shaders/win32_30/gta_normal_reflect.fxc',
    'update/common/shaders/win32_30/gta_normal_reflect_alpha.fxc',
    'update/common/shaders/win32_30/gta_normal_reflect_decal.fxc',
    'update/common/shaders/win32_30/gta_normal_spec.fxc',
    'update/common/shaders/win32_30/gta_normal_spec_cubemap_reflect.fxc',
    'update/common/shaders/win32_30/gta_normal_spec_decal.fxc',
    'update/common/shaders/win32_30/gta_normal_spec_reflect.fxc',
    'update/common/shaders/win32_30/gta_normal_spec_reflect_decal.fxc',
    'update/common/shaders/win32_30/gta_normal_spec_reflect_emissive.fxc',
    'update/common/shaders/win32_30/gta_normal_spec_reflect_emissivenight.fxc',
    'update/common/shaders/win32_30/gta_parallax.fxc',
    'update/common/shaders/win32_30/gta_parallax_specmap.fxc',
    'update/common/shaders/win32_30/gta_parallax_steep.fxc',
    'update/common/shaders/win32_30/gta_ped.fxc',
    'update/common/shaders/win32_30/gta_ped_face.fxc',
    'update/common/shaders/win32_30/gta_ped_reflect.fxc',
    'update/common/shaders/win32_30/gta_ped_skin.fxc',
    'update/common/shaders/win32_30/gta_ped_skin_blendshape.fxc',
    'update/common/shaders/win32_30/gta_projtex.fxc',
    'update/common/shaders/win32_30/gta_projtex_steep.fxc',
    'update/common/shaders/win32_30/gta_radar.fxc',
    'update/common/shaders/win32_30/gta_reflect.fxc',
    'update/common/shaders/win32_30/gta_reflect_decal.fxc',
    'update/common/shaders/win32_30/gta_rmptfx_gpurender.fxc',
    'update/common/shaders/win32_30/gta_rmptfx_litsprite.fxc',
    'update/common/shaders/win32_30/gta_rmptfx_mesh.fxc',
    'update/common/shaders/win32_30/gta_rmptfx_raindrops.fxc',
    'update/common/shaders/win32_30/gta_spec.fxc',
    'update/common/shaders/win32_30/gta_spec_decal.fxc',
    'update/common/shaders/win32_30/gta_spec_reflect.fxc',
    'update/common/shaders/win32_30/gta_spec_reflect_decal.fxc',
    'update/common/shaders/win32_30/gta_terrain_va_2lyr.fxc',
    'update/common/shaders/win32_30/gta_terrain_va_3lyr.fxc',
    'update/common/shaders/win32_30/gta_terrain_va_4lyr.fxc',
    'update/common/shaders/win32_30/gta_trees.fxc',
    'update/common/shaders/win32_30/gta_trees_extended.fxc',
    'update/common/shaders/win32_30/gta_vehicle_badges.fxc',
    'update/common/shaders/win32_30/gta_vehicle_basic.fxc',
    'update/common/shaders/win32_30/gta_vehicle_chrome.fxc',
    'update/common/shaders/win32_30/gta_vehicle_disc.fxc',
    'update/common/shaders/win32_30/gta_vehicle_generic.fxc',
    'update/common/shaders/win32_30/gta_vehicle_interior.fxc',
    'update/common/shaders/win32_30/gta_vehicle_interior2.fxc',
    'update/common/shaders/win32_30/gta_vehicle_lightsemissive.fxc',
    'update/common/shaders/win32_30/gta_vehicle_mesh.fxc',
    'update/common/shaders/win32_30/gta_vehicle_paint1.fxc',
    'update/common/shaders/win32_30/gta_vehicle_paint2.fxc',
    'update/common/shaders/win32_30/gta_vehicle_paint3.fxc',
    'update/common/shaders/win32_30/gta_vehicle_rims1.fxc',
    'update/common/shaders/win32_30/gta_vehicle_rims2.fxc',
    'update/common/shaders/win32_30/gta_vehicle_rubber.fxc',
    'update/common/shaders/win32_30/gta_vehicle_shuts.fxc',
    'update/common/shaders/win32_30/gta_vehicle_tire.fxc',
    'update/common/shaders/win32_30/gta_vehicle_vehglass.fxc',
    'update/common/shaders/win32_30/gta_wire.fxc',
    'update/common/shaders/win32_30/mirror.fxc',
    'update/common/shaders/win32_30/rage_atmoscatt_clouds.fxc',
    'update/common/shaders/win32_30/rage_billboard_nobump.fxc',
    'update/common/shaders/win32_30/rage_bink.fxc',
    'update/common/shaders/win32_30/rage_default.fxc',
    'update/common/shaders/win32_30/rage_fastmipmap.fxc',
    'update/common/shaders/win32_30/rage_im.fxc',
    'update/common/shaders/win32_30/rage_perlinnoise.fxc',
    'update/common/shaders/win32_30/rage_postfx.fxc',
    'update/common/shaders/win32_30/rmptfx_collision.fxc',
    'update/common/shaders/win32_30/rmptfx_default.fxc',
    'update/common/shaders/win32_30/rmptfx_litsprite.fxc',
    'update/common/shaders/win32_30/shadowSmartBlit.fxc',
    'update/common/shaders/win32_30/shadowZ.fxc',
    'update/common/shaders/win32_30/shadowZDir.fxc',
    'update/common/shaders/win32_30/water.fxc',
    'update/common/shaders/win32_30/waterTex.fxc',
    'update/common/text/americanFF.gxt',
    'update/common/text/frenchFF.gxt',
    'update/common/text/germanFF.gxt',
    'update/common/text/italianFF.gxt',
    'update/common/text/japaneseFF.gxt',
    'update/common/text/russianFF.gxt',
    'update/common/text/spanishFF.gxt',
    'update/GTAIV.EFLC.FusionFix/GTAIV.EFLC.FusionFix.img',
    'update/GTAIV.EFLC.FusionFix/IV/GTAIV.FusionFix.img',
    'update/GTAIV.EFLC.FusionFix/TBOGT/TBOGT.FusionFix.img',
    'update/GTAIV.EFLC.FusionFix/TLAD/TLAD.FusionFix.img',
    'update/pc/data/effects/gtarainemitter.xml',
    'update/pc/data/effects/gta_core.wpfl',
    'update/pc/data/halloween.dat',
    'update/pc/data/halloweenext.dat',
    'update/pc/data/snow.dat',
    'update/pc/data/snowext.dat',
    'update/pc/data/timecyc.dat',
    'update/pc/data/timecycext.dat',
    'update/pc/data/timecyclemodifiersext.dat',
    'update/pc/textures/buttons_360.wtd',
    'update/pc/textures/coronas.wtd',
    'update/pc/textures/fx_rain.wtd',
    'update/pc/textures/lights_occluders.wtd',
    'update/pc/textures/stipple.wtd',
    'update/pc/textures/title_no_site.wtd',
    'update/pc/textures/title_no_site_e1.wtd',
    'update/pc/textures/title_no_site_e2.wtd',
    'update/TBoGT/common/data/carcols.dat',
    'update/TBoGT/common/data/fonts.dat',
    'update/TBoGT/common/data/frontend_menus.xml',
    'update/TBoGT/common/data/visualsettings.dat',
    'update/TBoGT/pc/data/timecyc.dat',
    'update/TBoGT/pc/data/timecycext.dat',
    'update/TBoGT/pc/models/cdimages/playerped.rpf',
    'update/TBoGT/pc/textures/dancing.wtd',
    'update/TLAD/common/data/frontend_menus.xml',
    'update/TLAD/common/data/frontend_pc.dat',
    'update/TLAD/pc/data/effects/gta_ep1.wpfl',
    'update/TLAD/pc/data/timecyc.dat',
    'update/TLAD/pc/data/timecycext.dat',
    'update/TLAD/pc/models/cdimages/playerped.rpf',
    'update/update.txt',
)


def fusionfix_hosts(game):
    return [p for folder in (Path(game),Path(game)/'plugins',Path(game)/'scripts') if folder.is_dir()
            for p in folder.iterdir() if p.name.lower()=='gtaiv.eflc.fusionfix.asi']

def accepted_hosts(game,bridge=None):
    hashes={'14f6ac879bcc7df5fb18ce7b410bc436e16675b0dad640378dcb1e6dd3be98b3'}
    if bridge:hashes.update(bridge.get('supported_sha256',[]));hashes.add(bridge.get('sha256',''))
    state=Path(game)/'GTAIV-QoL-bundle-state.json'
    if state.is_file() and not state.is_symlink() and state.stat().st_size<65536:
        try:hashes.add(json.loads(state.read_text()).get('fusionfix_sha256',''))
        except (ValueError,OSError):pass
    return hashes

def asset_path(game,relative,dep):
    path=Path(game)
    for part in Path(relative).parts:
        path=dep.find_file(path,part) if path.is_dir() else path/part
        if path.is_symlink():raise dep.SetupError('FusionFix destination contains a symbolic link.')
    return path

def check_fusionfix(game,dep,bridge=None):
    hosts=fusionfix_hosts(game)
    if len(hosts)!=1:return False,'FusionFix 5.1.2: expected one supported host; found '+str(len(hosts))+'.'
    if hosts[0].is_symlink() or not hosts[0].is_file() or hashlib.sha256(hosts[0].read_bytes()).hexdigest() not in accepted_hosts(game,bridge):
        return False,'FusionFix host is unsupported; existing files are preserved.'
    config=dep.find_file(hosts[0].parent,hosts[0].stem+'.ini')
    if config.is_symlink() or not config.is_file() or config.stat().st_size==0:return False,'FusionFix configuration is missing; prepare FusionFix before installation.'
    missing=[]
    for name in FUSIONFIX_ASSETS:
        path=asset_path(game,name,dep)
        if not path.is_file() or path.stat().st_size==0:missing.append(name)
    if missing:return False,'FusionFix assets missing: '+str(len(missing))+'; use automatic setup or download its ZIP and Retry.'
    return True,'Supported FusionFix host and all '+str(len(FUSIONFIX_ASSETS))+' required assets found; existing custom assets kept.'

def downloaded_fusionfix(dep):
    for path in dep.completed_downloads(lambda n:n.endswith('.zip') and n.startswith('gtaiv.eflc.fusionfix'),256*1024*1024):
        raw=path.read_bytes()
        if hashlib.sha256(raw).hexdigest()==FUSIONFIX['archive_sha256']:return raw
    return None

def install_fusionfix(game, dep, archive=None, bridge=None):
    """Install missing pinned menu assets; preserve existing wrappers and preferences."""
    game=Path(game)
    hosts=fusionfix_hosts(game)
    if hosts and (len(hosts)!=1 or hosts[0].is_symlink() or not hosts[0].is_file() or hashlib.sha256(hosts[0].read_bytes()).hexdigest() not in accepted_hosts(game,bridge)):
        raise dep.SetupError('Unsupported/ambiguous FusionFix host; existing files were preserved.')
    if archive is None:
        old=dep.MAX_DOWNLOAD
        try:
            dep.MAX_DOWNLOAD=256*1024*1024
            archive=downloaded_fusionfix(dep) or dep.download_author_archive(FUSIONFIX)
        finally:dep.MAX_DOWNLOAD=old
    if hashlib.sha256(archive).hexdigest()!=FUSIONFIX['archive_sha256']:
        raise dep.SetupError('FusionFix download failed verification.')
    created=[]
    with zipfile.ZipFile(io.BytesIO(archive)) as z:
        infos=z.infolist()
        if len(infos)>1000 or sum(i.file_size for i in infos)>512*1024*1024:
            raise dep.SetupError('FusionFix archive exceeds its bounds.')
        pending=[];names=set()
        for i in infos:
            relative=Path(i.filename.replace('\\','/'))
            if relative.is_absolute() or '..' in relative.parts or ':' in i.filename or (i.external_attr>>16)&0o170000==0o120000:
                raise dep.SetupError('Invalid FusionFix archive entry.')
            if i.is_dir():continue
            key=relative.as_posix().lower()
            if key in names:raise dep.SetupError('Duplicate FusionFix archive entry.')
            names.add(key)
            # The dependency guide owns the ASI loader. Never install optional
            # DXVK/Vulkan graphics wrappers into an existing Proton renderer.
            if len(relative.parts)==1 and relative.suffix.lower()=='.dll':continue
            if hosts and relative.name.lower()=='gtaiv.eflc.fusionfix.asi':continue
            target=asset_path(game,relative,dep)
            if hosts and relative.name.lower()=='gtaiv.eflc.fusionfix.ini':target=dep.find_file(hosts[0].parent,hosts[0].stem+'.ini')
            if game.is_symlink() or any((game/Path(*relative.parts[:n])).is_symlink() for n in range(1,len(relative.parts)+1)):
                raise dep.SetupError('FusionFix destination contains a symbolic link.')
            data=z.read(i)
            if target.exists():
                if relative.suffix.lower() in ('.ini','.cfg') or len(relative.parts)==1:
                    continue # Existing renderer/loader wrappers and personal settings stay intact.
                if not target.is_file() or target.stat().st_size==0:
                    raise dep.SetupError('FusionFix asset conflicts with an existing file: '+relative.as_posix())
                continue
            pending.append((target,data))
        try:
            for target,data in pending:
                target.parent.mkdir(parents=True,exist_ok=True)
                with target.open('xb') as stream:stream.write(data)
                created.append(target)
                if target.read_bytes()!=data:raise dep.SetupError('FusionFix installation verification failed.')
        except Exception:
            for target in reversed(created):target.unlink(missing_ok=True)
            raise
    return 'Installed verified FusionFix 5.1.2 assets and menu host.'


def load_source(name, source):
    module=types.ModuleType(name);exec(compile(source,'<'+name+'>','exec'),module.__dict__);return module


@contextmanager
def game_lock(dep):
    import fcntl
    proc=Path('/proc')
    if proc.is_dir():
        for path in proc.glob('[0-9]*/comm'):
            try:running=path.read_text().strip().lower() in ('gtaiv.exe','gtaiv')
            except OSError:continue
            if running:raise dep.SetupError('Close GTA IV before installing.')
    root=Path.home()/'.local/share/roadnav-uploader';root.mkdir(parents=True,exist_ok=True,mode=0o700)
    with (root/'session.lock').open('a') as lock:
        try:fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
        except BlockingIOError:raise dep.SetupError('Close GTA IV before installing.') from None
        yield


def payload_bridge(files):
    tree=ast.parse(files['install-helper.py'].decode('utf-8'))
    return next((ast.literal_eval(n.value) for n in tree.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='FUSIONFIX_BRIDGE' for t in n.targets)),{})


def configure_dependencies(dep,bridge):
    if getattr(dep,'GUIDE_PROTOCOL',0)!=2:raise dep.SetupError('Installer dependency guide is stale; use the rebuilt installer.')
    dep.REQUIREMENTS['fusionfix']='FusionFix 5.1.2 host and assets'
    dep.URLS['fusionfix']='https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix/releases/tag/v5.1.2'
    def status(game,prefix):
        try:return check_fusionfix(game,dep,bridge)
        except (OSError,dep.SetupError) as exc:return False,str(exc)
    def prepare(game,prefix,ui,manual=False):
        if status(game,prefix)[0]:return 'Existing FusionFix host/assets kept.'
        raw=downloaded_fusionfix(dep) if manual else None
        if manual and raw is None:return None
        with game_lock(dep):return install_fusionfix(game,dep,raw,bridge)
    dep.EXTRA_CHECKS['fusionfix']=status
    dep.EXTRA_DOWNLOADERS['fusionfix']=lambda game,prefix,ui:prepare(game,prefix,ui)
    dep.EXTRA_IMPORTERS['fusionfix']=lambda game,prefix,ui:prepare(game,prefix,ui,True)
    original_install=dep.install_dependency_bytes
    def guarded_dependency(game,key,data):
        with game_lock(dep):return original_install(game,key,data)
    dep.install_dependency_bytes=guarded_dependency
    original_run=dep.run_in_prefix
    def guarded_prefix(prefix,ui,args):
        with game_lock(dep):return original_run(prefix,ui,args)
    dep.run_in_prefix=guarded_prefix


def run(dependency_source, updater_source, local_payload=None):
    dep=load_source('qol_dependencies',dependency_source)
    updater=load_source('qol_update',updater_source)
    window=updater.make_progress('Install / Update GTA IV QoL')
    dep.QUIET_COMPLETION=bool(window and window.alive())
    installed=False
    try:
        bundled=None
        if local_payload is not None:
            path,candidate=local_payload
            if Path(path).stat().st_size>updater.MAX_ARCHIVE:raise dep.SetupError('Bundled update exceeds its size limit.')
            bundled=updater.validate_archive(Path(path).read_bytes(),candidate)
        config=json.loads(updater.CONFIG.read_text()) if updater.CONFIG.exists() else {'channel':'beta' if bundled else 'dev'}
        state=updater.ROOT/'installed.json'
        if bundled and state.is_file() and config.get('game'):
            saved=json.loads(state.read_text())
            local_version=updater.version_key(bundled[0].get('release_version'))
            saved_version=updater.version_key(saved.get('version'))
            current=saved.get('sha')==bundled[0]['sha'] or local_version and saved_version and saved_version>=local_version
            game=Path(config['game']);prefix=config.get('prefix')
            if not prefix:
                candidates=dep.prefix_candidates(game)
                if len(candidates)==1:prefix=candidates[0]
            checks=dep.inspect_setup(game,prefix)
            checks['fusionfix']=check_fusionfix(game,dep,payload_bridge(bundled[1]))
            if current and all(ok for ok,_ in checks.values()) and updater.bundle_files_intact(game,saved) and updater.updater_files_intact(saved):
                result=updater.main(window=window)
                if window and window.alive():window.finish('Update check complete');window.wait()
                return result
        if bundled and config.get('channel')=='beta':
            if window:window.update(percent=3,phase='Checking published public betas')
            try:
                candidate=updater.beta_candidate()
                if candidate and updater.version_key(candidate['version'])>=updater.version_key(bundled[0]['release_version']):
                    updater.PROGRESS=window
                    bundled=updater.validate_archive(updater.request('',candidate['route'],binary=True),candidate)
            except (RuntimeError,OSError):
                dep.SETUP_NOTE='Public update check unavailable; setup will install the verified beta supplied in this archive. No private token is needed.'
        configure_dependencies(dep,payload_bridge(bundled[1]) if bundled else {})
        if window:
            window.update(percent=5,phase='Finding GTA IV and checking dependencies')
            dep.report_progress=lambda phase,received,total:window.update(percent=10+15*min(1,received/total) if total else 12,phase=phase,size='Dependency download: %.2f MB'%(received/1048576))
        def finish(game,payload,prefix=None):
            nonlocal installed
            game=Path(game).resolve()
            with game_lock(dep):
                checks=dep.inspect_setup(game,prefix)
                if not all(ok for ok,_ in checks.values()):raise dep.SetupError('Dependency re-check failed; return to checks before installation.')
            config['game']=str(game)
            if prefix:config['prefix']=str(prefix)
            updater.atomic(updater.CONFIG,json.dumps(config).encode())
            try:
                if bundled:
                    updater.PROGRESS=window;updater.apply_update(*bundled,config['channel'])
                elif updater.main(window=window)!=0:raise dep.SetupError('The updater did not finish. Existing backups remain available.')
            except RuntimeError as exc:raise dep.SetupError(str(exc)) from None
            installed=True
            return 'Installed the complete GTA IV QoL bundle and its integrated updater.'
        dep.install_mod=finish
        result=dep.main()
        if window and window.alive():window.finish('Installation complete — ready to play' if installed else 'Setup closed',error=result not in (None,0));window.wait()
        return result
    except Exception as exc:
        if window and window.alive():window.finish(str(exc),error=True);window.wait()
        raise


if __name__=='__main__':
    here=Path(__file__).parent
    raise SystemExit(run((here/'dependency_setup.py').read_text(),(here/'qol-updater.py').read_text()))
