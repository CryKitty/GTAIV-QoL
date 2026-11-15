"""Windows-only front-end adapters; never rewrite the verified mod payload."""
import base64
from contextlib import contextmanager
import csv
from datetime import datetime, timezone
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import types

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

def running_pid():
    result=subprocess.run(['tasklist.exe','/FI','IMAGENAME eq GTAIV.exe','/FO','CSV','/NH'],capture_output=True,text=True,creationflags=NO_WINDOW,check=True)
    for row in csv.reader(io.StringIO(result.stdout)):
        if len(row)>1 and row[0].lower()=='gtaiv.exe':return int(row[1])
    return None

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
    if not handle:return None
    try:
        created=w.FILETIME();ended=w.FILETIME();system=w.FILETIME();user=w.FILETIME()
        if not kernel.GetProcessTimes(handle,c.byref(created),c.byref(ended),c.byref(system),c.byref(user)):return None
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
        target=root/'Update-GTAIV-QoL.exe'
        if Path(exe).resolve()!=target.resolve() and (not target.exists() or hashlib.sha256(target.read_bytes()).digest()!=hashlib.sha256(Path(exe).read_bytes()).digest()):
            # A running frozen watcher maps its EXE. Ask it to stop before a
            # front-end replacement; a same-binary rerun never rewrites it.
            import time,fcntl
            signal=root/'stop-watcher';signal.touch()
            deadline=time.monotonic()+15
            while True:
                try:
                    with (root/'watch.lock').open('a') as watch:fcntl.flock(watch,fcntl.LOCK_EX|fcntl.LOCK_NB)
                    break
                except BlockingIOError:
                    if time.monotonic()>=deadline:raise RuntimeError('The old updater is busy. Close GTA IV and retry setup.')
                    time.sleep(.2)
            temporary=target.with_suffix('.exe.tmp');shutil.copyfile(exe,temporary);temporary.replace(target)
            (root/'stop-watcher').unlink(missing_ok=True)
        # WScript uses the actual redirected/localised Desktop, not a guessed path.
        script="[Environment]::GetFolderPath('Desktop')"
        desktop=subprocess.run([powershell(),'-NoProfile','-NonInteractive','-Command',script],check=True,capture_output=True,text=True,creationflags=NO_WINDOW).stdout.strip()
        if not desktop:raise RuntimeError('Windows Desktop location is unavailable.')
        create_shortcut(target,Path(desktop)/'Update GTA IV QoL.lnk')
        command=subprocess.list2cmdline([str(target),'--watch'])
        with winreg.CreateKey(winreg.HKEY_CURRENT_USER,r'Software\Microsoft\Windows\CurrentVersion\Run') as key:winreg.SetValueEx(key,'GTAIV QoL Updates',0,winreg.REG_SZ,command)
        subprocess.Popen([str(target),'--watch'],creationflags=NO_WINDOW,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    updater.shortcut=shortcut
    def apply_update(manifest,files,channel):
        game=updater.selected_game()
        if game is None:raise RuntimeError('Select GTAIV.exe before installing.')
        with game_lock(root):
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
