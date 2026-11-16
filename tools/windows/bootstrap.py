"""Frozen Windows installer/updater. Build-time sources are data, not downloads."""
import importlib
import json
import os
from pathlib import Path
import sys
import time
import tkinter
import tkinter.ttk
import tkinter.messagebox
import tkinter.filedialog
import tkinter.simpledialog
import winreg
import msvcrt
from platform_adapter import adapt,game_lock,install_lock_api,native_runtime,steam_roots,create_shortcut,watch,windows_tick
from progress_ui import Progress
from embedded_sources import SOURCES,CANDIDATE

def modules():
    import types
    guide=types.ModuleType('windows_guide');exec(compile(SOURCES['guide'],'<guide>','exec'),guide.__dict__)
    original=guide.load_source
    def load(name,source):
        module=original(name,source)
        if name=='qol_dependencies':
            module.steam_roots=lambda:steam_roots(winreg)
            def run_native(prefix,ui,args):
                result=native_runtime(args)
                if result.returncode not in (0,1638,3010):raise module.SetupError('Microsoft runtime setup failed or was cancelled; retry after it closes.')
                return result
            module.run_in_prefix=run_native
        if name=='qol_update':
            root=adapt(module,sys.executable,Progress)
            from contextlib import contextmanager
            @contextmanager
            def dependency_lock(dep):
                try:
                    with game_lock(root):yield
                except RuntimeError as exc:raise dep.SetupError(str(exc)) from None
            guide.game_lock=dependency_lock
        return module
    guide.load_source=load
    return guide,load

def main():
    install_lock_api()
    if len(sys.argv)==4 and sys.argv[1]=='--self-test':
        import ctypes,types
        guide,load=modules()
        dependency=load('qol_dependencies',SOURCES['dependency'])
        updater=load('qol_update',SOURCES['updater'])
        manifest,files=updater.validate_archive(Path(sys.argv[3]).read_bytes(),CANDIDATE)
        guide.configure_dependencies(dependency,guide.payload_bridge(files))
        helper=types.ModuleType('frozen_verified_helper')
        exec(compile(files['install-helper.py'],'<verified frozen helper>','exec'),helper.__dict__)
        assert callable(helper.install_bundle)
        root=tkinter.Tk();root.withdraw();root.update()
        runtime=Path(sys._MEIPASS).resolve()
        tcl_bundled=runtime in Path(root.tk.eval('info library')).resolve().parents
        tk_bundled=runtime in Path(root.tk.eval('set tk_library')).resolve().parents
        root.destroy()
        shortcut=Path(sys.argv[2]).with_suffix('.lnk');create_shortcut(Path(sys.executable),shortcut)
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            state=Path(tmp);game=state/'Game';game.mkdir();(game/'GTAIV.exe').write_bytes(b'fixture')
            (game/'GTAIV-QoL.ini').write_text('[Updates]\nCheckForUpdates=0\n')
            updater.ROOT=state/'state';updater.ROOT.mkdir();updater.CONFIG=updater.ROOT/'config.json'
            updater.CONFIG.write_text(json.dumps({'game':str(game),'channel':'beta'}))
            updater.game_running=lambda:False
            request=game/'GTAIV-QoL-update-request.txt';request.write_text('stale')
            updater.update_status(game,'downloading');windows_tick(updater,game)
            idle_cleanup=not request.exists() and json.loads((game/'GTAIV-QoL-update-status.json').read_text())['code']=='failed'
        Path(sys.argv[2]).write_text(json.dumps({'worker_idle_cleanup':idle_cleanup,'shared_sources_loaded':True,'payload_verified':manifest['sha']==CANDIDATE['sha'],'tcl_bundled':tcl_bundled,'tk_bundled':tk_bundled,'shortcut':shortcut.is_file(),'frozen':bool(getattr(sys,'frozen',False)),'tk':tkinter.TkVersion,'console':bool(ctypes.windll.kernel32.GetConsoleWindow())}))
        return 0
    guide,load=modules()
    if '--watch' in sys.argv:
        updater=load('qol_update',SOURCES['updater']);updater.ROOT.mkdir(parents=True,exist_ok=True)
        return watch(updater,sys.executable)
    if '--update' in sys.argv:return load('qol_update',SOURCES['updater']).main()
    return guide.run(SOURCES['dependency'],SOURCES['updater'],(Path(sys.executable).with_name('GTAIV-QoL-update.zip'),CANDIDATE))

if __name__=='__main__':
    try:raise SystemExit(main())
    except Exception as exc:
        if '--self-test' in sys.argv:
            Path(sys.argv[2]).write_text(json.dumps({'error':type(exc).__name__+': '+str(exc)}));raise SystemExit(1)
        if '--watch' in sys.argv:raise SystemExit(1)
        tkinter.messagebox.showerror('GTA IV QoL setup',str(exc) if isinstance(exc,RuntimeError) else 'Setup could not finish. Existing backups remain available.')
        raise SystemExit(1)
