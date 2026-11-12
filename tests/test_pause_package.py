import importlib.util
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('package_pause',ROOT/'tools/steam-deck/package-pause-test.py')
package=importlib.util.module_from_spec(spec);spec.loader.exec_module(package)
class PackageTests(unittest.TestCase):
    def test_large_payload_reads_own_file_with_spaces(self):
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'Install test.desktop'
            path.write_text(package.make_launcher('#'+('x'*500000)+'\nprint("PAYLOAD_OK")\n'))
            command=next(line[5:] for line in path.read_text().splitlines() if line.startswith('Exec='))
            self.assertLess(len(command),1024)
            for location in (str(path),path.as_uri()):
                args=shlex.split(command);args[0]=sys.executable;args[-1]=location
                result=subprocess.run(args,capture_output=True,text=True,check=True)
                self.assertEqual(result.stdout.strip(),'PAYLOAD_OK')
    def test_zip_embeds_exact_files_and_executable_permission(self):
        import ast,base64,hashlib,zipfile
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);(root/'tools/steam-deck').mkdir(parents=True);(root/'build').mkdir()
            (root/'tools/steam-deck/enable-qol-logs.py').write_text("BUNDLE = {}\nINSTALL_BUILD = ''\n")
            for name,data in [('GTAIV-QoL.asi',b'main-binary'),('GTAIV-QoL-Pause.asi',b'pause-binary')]:
                (root/'build'/name).write_bytes(data)
            (root/'GTAIV-QoL.ini.example').write_bytes(b'ZoomPercent=300')
            previous=package.__file__;package.__file__=str(root/'tools/steam-deck/package-pause-test.py')
            try:package.main()
            finally:package.__file__=previous
            with zipfile.ZipFile(root/'tools/steam-deck/dist/GTAIV-QoL-Pause-Test-Deck.zip') as archive:
                entry=archive.getinfo('Install-QoL-Pause-Test.desktop')
                self.assertEqual((entry.external_attr>>16)&0o777,0o755)
                desktop=archive.read(entry).decode()
            script=base64.b64decode(desktop.split('\n#QOLPAYLOAD=',1)[1]).decode()
            tree=ast.parse(script)
            bundle=ast.literal_eval(tree.body[0].value)
            self.assertEqual(set(bundle),{'GTAIV-QoL.asi','GTAIV-QoL-Pause.asi','GTAIV-QoL.ini'})
            for item in bundle.values():
                self.assertEqual(hashlib.sha256(base64.b64decode(item['data'])).hexdigest(),item['sha256'])

if __name__=='__main__':unittest.main()
