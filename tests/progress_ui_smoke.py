"""Exercise the actual native Linux window, collapsed notes, expansion and close."""
import importlib.util
from pathlib import Path
import subprocess
import time
import os
from PIL import Image,ImageChops
root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('qol_ui',root/'tools/steam-deck/progress_ui.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
os.environ["SteamAppId"]="12345"
gui=m.Progress()
try:
    time.sleep(1)
    assert gui.alive(),gui.process.stderr.read().decode('utf-8','replace')
    gui.update(percent=42,phase='Downloading update',version='Development build 90',size='Download: 2.4 MB / 5.7 MB',changelog='Compass icons and health/armor meters are twice their previous size.\nDamage flashes red briefly.\nRoadNav avoids touching turn clusters.\nMission markers are easier to see.\nLog uploads continue in the background.')
    out=root/'build';out.mkdir(exist_ok=True)
    time.sleep(.4)
    subprocess.run(['import','-window','root',str(out/'updater-collapsed.png')],check=True)
    identifier=subprocess.check_output(['xdotool','search','--name','GTA IV QoL'],text=True).splitlines()[0]
    assert 'steam_app_12345' in subprocess.check_output(['xprop','-id',identifier,'WM_CLASS'],text=True)
    subprocess.run(['xdotool','mousemove','--window',identifier,'140','313','click','1'],check=True)
    time.sleep(.4)
    subprocess.run(['import','-window','root',str(out/'updater-expanded.png')],check=True)
    a=Image.open(out/'updater-collapsed.png').convert('RGB');b=Image.open(out/'updater-expanded.png').convert('RGB')
    assert ImageChops.difference(a.crop((80,390,940,580)),b.crop((80,390,940,580))).getbbox(),'Notes did not expand'
    gui.finish();time.sleep(.3)
    subprocess.run(['xdotool','mousemove','--window',identifier,'820','590','click','1'],check=True)
    gui.process.wait(timeout=5)
    assert gui.process.returncode==0
    gui.wait()
    print('Native updater window, collapsed/expanded changelog and completion tested')
finally:
    if gui.alive():gui.process.terminate()
