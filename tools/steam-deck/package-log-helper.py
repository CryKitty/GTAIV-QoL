import base64
from pathlib import Path
import zipfile
root = Path(__file__).resolve().parent
payload = base64.b64encode((root/'enable-qol-logs.py').read_bytes()).decode()
launcher = ('[Desktop Entry]\nType=Application\nName=Upload GTA IV QoL diagnostic logs\n'
            'Comment=Extend existing RoadNav private log uploads and collect the current QoL log\n'
            'Icon=applications-games\nTerminal=false\n'
            'Exec=/usr/bin/python3 -c "import base64;exec(base64.b64decode(\''+payload+'\'))"\n')
out = root/'dist'
out.mkdir(exist_ok=True)
path = out/'Upload-QoL-Log.desktop'
path.write_text(launcher); path.chmod(0o755)
with zipfile.ZipFile(out/'GTAIV-QoL-Deck-Log-Helper.zip', 'w', zipfile.ZIP_DEFLATED) as z:
    z.write(path, path.name)
    z.writestr('START-HERE.txt', 'Close GTA IV. In Steam Deck Desktop Mode, extract this ZIP and double-click Upload-QoL-Log.desktop. Choose Execute/Trust if KDE asks. This reuses your existing RoadNav log-upload setup, adds both QoL logs with full diagnostic capture in numbered parts, and uploads existing logs without launching GTA IV. No token is bundled. Run this helper once before testing the diagnostic build. After a test, exit normally; uploads run automatically. Use the helper again to retry a manual upload.\n')
print(out/'GTAIV-QoL-Deck-Log-Helper.zip')

