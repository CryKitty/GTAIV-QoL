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

 def test_build_identity_changes_without_changing_release_version(self):
  from unittest.mock import patch
  import os
  with tempfile.TemporaryDirectory() as tmp:
   root=Path(tmp);(root/'VERSION').write_text('0.1.0-beta.1')
   labels=[]
   for run,sha in [('47','a'*40),('48','b'*40)]:
    with patch.dict(os.environ,{'GITHUB_RUN_NUMBER':run,'GITHUB_SHA':sha}):v.generate(root)
    labels.append((root/'src/qol_version.hpp').read_text())
    self.assertIn('QOL_BUILD_WIDE L"b'+run+'"',labels[-1]);self.assertIn('QOL_SOURCE_SHA "'+sha+'"',labels[-1])
   self.assertNotEqual(labels[0],labels[1]);self.assertEqual((root/'VERSION').read_text(),'0.1.0-beta.1')
   with patch.dict(os.environ,{'GITHUB_RUN_NUMBER':'bad','GITHUB_SHA':'c'*40}):v.generate(root)
   self.assertIn('QOL_BUILD_WIDE L"@ccccccc"',(root/'src/qol_version.hpp').read_text())
