"""Windows-only front-end adapters; never rewrite the verified mod payload."""
import base64
from contextlib import contextmanager
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import types
import time

NO_WINDOW = 0x08000000

def steam_roots(registry, environment=os.environ):
    result=[]
    for hive,key,value in [(registry.HKEY_CURRENT_USER,r'Software\Valve\Steam','SteamPath'),
                           (registry.HKEY_LOCAL_MACHINE,r'SOFTWARE\WOW6432Node\Valve\Steam','InstallPath')]:
        try:
            with registry.OpenKey(hive,key) as handle:result.append(Path(registry.QueryValueEx(handle,value)[0]))
        except OSError:pass
    for name in ('ProgramFiles(x86)','ProgramFiles'):
        if environment.get(name):result.append(Path(environment[name])/'Steam')
    return list(dict.fromkeys(p.resolve() for p in result if p.is_dir()))

def install_lock_api():
    import msvcrt
    api=types.ModuleType('fcntl');api.LOCK_EX=1;api.LOCK_NB=2;api.LOCK_UN=4
    def flock(stream,flags):
        if not os.fstat(stream.fileno()).st_size:stream.write('\0');stream.flush()
        stream.seek(0)
        try:msvcrt.locking(stream.fileno(),msvcrt.LK_UNLCK if flags&api.LOCK_UN else msvcrt.LK_NBLCK,1)
        except OSError as exc:raise BlockingIOError('Another QoL operation is active.') from exc
    api.flock=flock;sys.modules['fcntl']=api

def process_ids(image='GTAIV.exe'):
    # In-process snapshot avoids spawning tasklist/PowerShell every idle poll.
    import ctypes as c
    from ctypes import wintypes as w
    class Entry(c.Structure):
        _fields_=[('dwSize',w.DWORD),('cntUsage',w.DWORD),('th32ProcessID',w.DWORD),
                  ('th32DefaultHeapID',c.c_size_t),('th32ModuleID',w.DWORD),
                  ('cntThreads',w.DWORD),('th32ParentProcessID',w.DWORD),
                  ('pcPriClassBase',w.LONG),('dwFlags',w.DWORD),('szExeFile',w.WCHAR*260)]
    kernel=c.WinDLL('kernel32',use_last_error=True)
    kernel.CreateToolhelp32Snapshot.argtypes=[w.DWORD,w.DWORD];kernel.CreateToolhelp32Snapshot.restype=w.HANDLE
    for name in ('Process32FirstW','Process32NextW'):
        fn=getattr(kernel,name);fn.argtypes=[w.HANDLE,c.POINTER(Entry)];fn.restype=w.BOOL
    kernel.CloseHandle.argtypes=[w.HANDLE]
    handle=kernel.CreateToolhelp32Snapshot(2,0)
    if handle==c.c_void_p(-1).value:raise RuntimeError('Cannot verify whether GTA IV is running.')
    try:
        entry=Entry();entry.dwSize=c.sizeof(entry);result=[]
        ok=kernel.Process32FirstW(handle,c.byref(entry))
        while ok:
            if entry.szExeFile.lower()==image.lower():result.append(entry.th32ProcessID)
            ok=kernel.Process32NextW(handle,c.byref(entry))
        if c.get_last_error()!=18:raise RuntimeError('Cannot finish checking running processes.')
        return result
    finally:kernel.CloseHandle(handle)


def running_pid():
    return next(iter(process_ids()),None)

@contextmanager
def game_lock(root):
    import fcntl
    root.mkdir(parents=True,exist_ok=True)
    with (root/'session.lock').open('a') as stream:
        fcntl.flock(stream,fcntl.LOCK_EX|fcntl.LOCK_NB)
        if running_pid() is not None:raise RuntimeError('Close GTA IV before installing or updating.')
        yield

def native_runtime(args):
    # Dependency installer alone may request UAC. The QoL launcher is asInvoker.
    try:return subprocess.run(list(args),capture_output=True,timeout=240,creationflags=NO_WINDOW)
    except OSError as exc:
        if getattr(exc,'winerror',None)!=740:raise
    import ctypes as c
    from ctypes import wintypes as w
    class Execute(c.Structure):
        _fields_=[('cbSize',w.DWORD),('fMask',w.ULONG),('hwnd',w.HWND),('lpVerb',w.LPCWSTR),('lpFile',w.LPCWSTR),('lpParameters',w.LPCWSTR),('lpDirectory',w.LPCWSTR),('nShow',c.c_int),('hInstApp',w.HINSTANCE),('lpIDList',c.c_void_p),('lpClass',w.LPCWSTR),('hkeyClass',w.HKEY),('dwHotKey',w.DWORD),('hIcon',w.HANDLE),('hProcess',w.HANDLE)]
    info=Execute();info.cbSize=c.sizeof(info);info.fMask=0x40;info.lpVerb='runas';info.lpFile=str(args[0]);info.lpParameters=subprocess.list2cmdline(list(args[1:]));info.nShow=1
    shell=c.WinDLL('shell32',use_last_error=True);kernel=c.WinDLL('kernel32',use_last_error=True)
    shell.ShellExecuteExW.argtypes=[c.POINTER(Execute)];shell.ShellExecuteExW.restype=w.BOOL
    kernel.WaitForSingleObject.argtypes=[w.HANDLE,w.DWORD];kernel.WaitForSingleObject.restype=w.DWORD
    kernel.GetExitCodeProcess.argtypes=[w.HANDLE,c.POINTER(w.DWORD)];kernel.CloseHandle.argtypes=[w.HANDLE]
    if not shell.ShellExecuteExW(c.byref(info)):raise RuntimeError('Microsoft runtime setup was cancelled or could not start.')
    try:
        if kernel.WaitForSingleObject(info.hProcess,240000)!=0:raise RuntimeError('Microsoft runtime setup did not finish in time; retry after it closes.')
        code=w.DWORD();assert kernel.GetExitCodeProcess(info.hProcess,c.byref(code))
        return types.SimpleNamespace(returncode=code.value)
    finally:kernel.CloseHandle(info.hProcess)

def powershell():
    return str(Path(os.environ['WINDIR'])/'System32/WindowsPowerShell/v1.0/powershell.exe')

def game_start_time():
    pid=running_pid()
    if pid is None:return None
    import ctypes as c
    from ctypes import wintypes as w
    kernel=c.WinDLL('kernel32',use_last_error=True)
    kernel.OpenProcess.argtypes=[w.DWORD,w.BOOL,w.DWORD];kernel.OpenProcess.restype=w.HANDLE
    kernel.GetProcessTimes.argtypes=[w.HANDLE,c.POINTER(w.FILETIME),c.POINTER(w.FILETIME),c.POINTER(w.FILETIME),c.POINTER(w.FILETIME)]
    kernel.CloseHandle.argtypes=[w.HANDLE]
    handle=kernel.OpenProcess(0x1000,False,pid)
    if not handle:raise RuntimeError('Cannot verify GTA IV process creation time; retry after exit.')
    try:
        created=w.FILETIME();ended=w.FILETIME();system=w.FILETIME();user=w.FILETIME()
        if not kernel.GetProcessTimes(handle,c.byref(created),c.byref(ended),c.byref(system),c.byref(user)):raise RuntimeError('Cannot verify GTA IV process creation time; retry after exit.')
        return ((created.dwHighDateTime<<32)|created.dwLowDateTime)/10000000-11644473600
    finally:kernel.CloseHandle(handle)

def create_shortcut(target, destination):
    def quote(value):return "'"+str(value).replace("'","''")+"'"
    script="$s=(New-Object -ComObject WScript.Shell).CreateShortcut("+quote(destination)+");$s.TargetPath="+quote(target)+";$s.Arguments='--update';$s.WorkingDirectory="+quote(target.parent)+";$s.Save()"
    subprocess.run([powershell(),'-NoProfile','-NonInteractive','-Command',script],check=True,capture_output=True,creationflags=NO_WINDOW)

def adapt(updater, exe, progress_class):
    root=Path(os.environ['LOCALAPPDATA'])/'GTAIV-QoL';updater.ROOT=root;updater.CONFIG=root/'config.json'
    updater.game_running=lambda:running_pid() is not None
    updater.game_start_time=game_start_time
    updater.uploader_files_intact=lambda files=None:True # No Linux upload service or credentials created.
    updater.make_progress=lambda title='GTA IV QoL':progress_class(title)
    def dialog(kind,text):
        from tkinter import messagebox,simpledialog
        if kind=='password':return simpledialog.askstring('GTA IV QoL',text,show='*') or ''
        (messagebox.showerror if kind=='error' else messagebox.showinfo)('GTA IV QoL',text);return ''
    updater.dialog=dialog
    def shortcut():
        import winreg
        root.mkdir(parents=True,exist_ok=True)
        if not getattr(updater,'WINDOWS_WORKER',False):stop_worker_for_refresh(root)
        # Content-addressed executables avoid replacing a mapped Windows EXE.
        # Old workers finish under their lock; their successor loads this path.
        target=root/('Update-GTAIV-QoL-'+hashlib.sha256(Path(exe).read_bytes()).hexdigest()[:16]+'.exe')
        if not target.exists():
            temporary=target.with_suffix('.exe.tmp');shutil.copyfile(exe,temporary);temporary.replace(target)
        updater.atomic(root/'worker-target.json',json.dumps({'exe':str(target)}).encode())
        script="[Environment]::GetFolderPath('Desktop')"
        desktop=subprocess.run([powershell(),'-NoProfile','-NonInteractive','-Command',script],check=True,capture_output=True,text=True,creationflags=NO_WINDOW).stdout.strip()
        if not desktop:raise RuntimeError('Windows Desktop location is unavailable.')
        create_shortcut(target,Path(desktop)/'Update GTA IV QoL.lnk')
        register_worker(target,winreg)
        if not getattr(updater,'WINDOWS_WORKER',False):
            # Successful manual installs invalidate even a same-SHA old stage.
            game=updater.selected_game()
            if game:
                folder=updater.pending_directory(game)
                for name in ('candidate.json','payload.zip'):(folder/name).unlink(missing_ok=True)
            start_worker(target)
    updater.shortcut=shortcut
    original_perform=updater._perform_update
    def perform():
        updater.WINDOWS_MANUAL=True
        try:return original_perform()
        finally:updater.WINDOWS_MANUAL=False
    updater._perform_update=perform
    @contextmanager
    def transaction_lock():
        if getattr(updater,'WINDOWS_WORKER',False) or getattr(updater,'WINDOWS_MANUAL',False):yield
        else:
            import fcntl
            root.mkdir(parents=True,exist_ok=True)
            with (root/'update.lock').open('a') as stream:
                fcntl.flock(stream,fcntl.LOCK_EX|fcntl.LOCK_NB)
                yield
    def apply_update(manifest,files,channel):
        game=updater.selected_game()
        if game is None:raise RuntimeError('Select GTAIV.exe before installing.')
        with transaction_lock(),game_lock(root):
            helper=types.ModuleType('verified_windows_installer')
            exec(compile(files['install-helper.py'],'<verified installer>','exec'),helper.__dict__)
            helper.BUNDLE={name:{'data':base64.b64encode(files[name]).decode(),'sha256':manifest['files'][name]} for name in ('GTAIV-QoL.asi','GTAIV-QoL-Pause.asi','GTAIV-QoL.ini')}
            helper.INSTALL_BUILD=manifest['sha'][:12];helper.progress=updater.progress
            helper.INGAME_UPDATER=None # Windows uses the frozen worker, never a Linux service.
            helper.install_bundle(game,helper.BUNDLE) # Shared verified backup/rollback/settings engine.
            updater.atomic(root/'install-helper.py',files['install-helper.py'])
            updater.atomic(root/'qol-updater.py',files['qol-updater.py'])
            shortcut()
            updater.atomic(root/'installed.json',json.dumps({'sha':manifest['sha'],'files':manifest['files'],'channel':channel,'version':manifest['release_version'],'repository':updater.REPO,'branch':updater.BRANCH,'build':str(manifest['version']),'run_id':manifest['run_id'],'updated_at':datetime.now(timezone.utc).isoformat(),'updated_local':datetime.now().astimezone().strftime('%Y-%m-%d %H:%M')}).encode())
            updater.update_status(game,'updated')
    updater.apply_update=apply_update
    return root


def register_worker(target, registry):
    """Per-user logon registration, refreshed on every successful install."""
    command=subprocess.list2cmdline([str(target),'--watch'])
    with registry.CreateKey(registry.HKEY_CURRENT_USER,r'Software\Microsoft\Windows\CurrentVersion\Run') as key:
        registry.SetValueEx(key,'GTAIV QoL Updates',0,registry.REG_SZ,command)


def start_worker(target):
    return subprocess.Popen([str(target),'--watch'],creationflags=NO_WINDOW,
                            stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)


def worker_target(root):
    return Path(json.loads((root/'worker-target.json').read_text())['exe'])


def windows_tick(updater,game):
    applied=updater.game_update_tick(game)
    if not applied and not updater.game_running():
        # Shared Deck recovery covers live games. Also recover an abandoned
        # busy receipt at idle Windows logon, under the same manual-update lock.
        import fcntl
        with (updater.ROOT/'update.lock').open('a') as lock:
            try:fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
            except BlockingIOError:return False
            if updater.game_running():return False
            try:busy=json.loads((game/'GTAIV-QoL-update-status.json').read_text()).get('code') in ('checking','downloading','applying')
            except (OSError,ValueError):busy=False
            if busy:updater.update_status(game,'failed')
    return applied


def watch(updater, executable, sleep=time.sleep):
    """Windowless singleton; no network unless the shared request path asks."""
    import fcntl
    root=updater.ROOT;root.mkdir(parents=True,exist_ok=True)
    restart=False
    updater.WINDOWS_WORKER=True
    with (root/'watch.lock').open('a') as lock:
        try:fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
        except BlockingIOError:return 0
        while True:
            if (root/'stop-watcher').exists():break
            try:
                if worker_target(root).resolve()!=Path(executable).resolve():
                    restart=True;break
                game=updater.selected_game()
                if game and windows_tick(updater,game):
                    restart=True;break
            except Exception:
                # game_update_tick already publishes a safe failed state.
                # Never display an auth/error dialog over gameplay.
                pass
            sleep(3)
    # Release singleton only after the transaction completed, then hand off.
    if restart:start_worker(worker_target(root))
    return 0


def stop_worker_for_refresh(root):
    # Also understood by the original PR #37 watcher, allowing a one-install
    # migration from its fixed-name EXE and otherwise non-reloading loop.
    import fcntl
    signal=root/'stop-watcher';signal.touch()
    deadline=time.monotonic()+15
    while True:
        try:
            with (root/'watch.lock').open('a') as lock:fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
            break
        except BlockingIOError:
            if time.monotonic()>=deadline:raise RuntimeError('The old updater is busy. Retry setup after it finishes.')
            time.sleep(.2)
    signal.unlink(missing_ok=True)
