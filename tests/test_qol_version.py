import importlib.util,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
s=importlib.util.spec_from_file_location('version_label',ROOT/'tools/write-qol-version.py');v=importlib.util.module_from_spec(s);s.loader.exec_module(v)
class VersionTests(unittest.TestCase):
 def test_installed_binary_label_is_regenerated_for_each_version(self):
  with tempfile.TemporaryDirectory() as tmp:
   root=Path(tmp)
   for version in ['0.1.0-beta.1','0.1.0-beta.2','0.2.0']:
    (root/'VERSION').write_text(version);self.assertEqual(v.generate(root),version)
    self.assertIn('QOL_VERSION_WIDE L"'+version+'"',(root/'src/qol_version.hpp').read_text())
   (root/'VERSION').write_text('bad"code')
   with self.assertRaises(RuntimeError):v.generate(root)
