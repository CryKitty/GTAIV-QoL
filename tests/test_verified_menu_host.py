import importlib.util,base64,hashlib,tempfile,zlib
from pathlib import Path
import unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
s=importlib.util.spec_from_file_location('restore_menu',ROOT/'tools/restore-verified-menu-host.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
class MenuRecoveryTests(unittest.TestCase):
 def test_authentic_data_is_parsed_not_executed_and_hash_is_enforced(self):
  with tempfile.TemporaryDirectory() as tmp:
   root=Path(tmp);raw=b'fixture menu';digest=hashlib.sha256(raw).hexdigest()
   data={'data':base64.b64encode(zlib.compress(raw)).decode(),'sha256':digest,'notice':'GNU GENERAL PUBLIC LICENSE\nfixture'}
   helper=root/'helper.py';helper.write_text('raise RuntimeError("must never execute")\nFUSIONFIX_BRIDGE = '+repr(data))
   m.restore(helper,root/'out',digest);self.assertEqual((root/'out/GTAIV.EFLC.FusionFix.asi').read_bytes(),raw)
   with self.assertRaises(RuntimeError):m.restore(helper,root/'wrong','0'*64)
   self.assertFalse((root/'wrong').exists())

 def test_public_tree_seed_restores_exact_menu_host_without_private_artifact(self):
  with tempfile.TemporaryDirectory() as tmp:
   out=Path(tmp)/'build';seed=ROOT/'vendor/fusionfix-menu'
   raw=zlib.decompress((seed/'verified-build91.asi.zlib').read_bytes())
   self.assertEqual(hashlib.sha256(raw).hexdigest(),m.VERIFIED_SHA)
   m.restore_vendored(seed,out)
   self.assertEqual((out/'GTAIV.EFLC.FusionFix.asi').read_bytes(),m.navigation_menu(raw))
   self.assertIn('GNU GENERAL PUBLIC LICENSE',(out/'FusionFix-GPL-3.0.txt').read_text())

 def test_corrupted_public_seed_is_rejected_before_writing(self):
  with tempfile.TemporaryDirectory() as tmp:
   seed=Path(tmp)/'seed';seed.mkdir();(seed/'verified-build91.asi.zlib').write_bytes(zlib.compress(b'wrong host'))
   (seed/'LICENSE.txt').write_text('GNU GENERAL PUBLIC LICENSE')
   with self.assertRaisesRegex(RuntimeError,'checksum mismatch'):m.restore_vendored(seed,Path(tmp)/'out')
   self.assertFalse((Path(tmp)/'out').exists())

 def test_menu_extension_is_bounded_and_keeps_the_rest_of_the_host_identical(self):
  raw=bytearray(b'\xcc'*0x273000)
  raw[0xd7094:0xd709a]=bytes.fromhex('0f848f010000')
  raw[0xd70ea:0xd70ed]=bytes.fromhex('83ff04')
  fields=[(0x2724b8,'QoLSettings_GetApi'.encode()),(0x272500,'QOL_GAMEVOICE'.encode()),(0x2725b4,'Gameplay Voices (%)'.encode('utf-16le')),(0x272650,'Cutscene Voice Volume'.encode('utf-16le')),(0x272700,'Cutscene Voices (%)'.encode('utf-16le')),(0x272780,b'QOL_VOICES')]
  for offset,text in fields:raw[offset:offset+len(text)]=text
  sites={0xd70ea:bytes.fromhex('83ff040f82e000000083ff060f84d7000000'),0xd712e:bytes.fromhex('c74424100000a040'),0xd719a:bytes.fromhex('c74424100000a040'),0xd7139:bytes.fromhex('c744240c0000c843c744240800000000'),0xd71a5:bytes.fromhex('c744240c0000c843c744240800000000'),0xd715d:bytes.fromhex('8b4dec6a07'),0xd7986:bytes.fromhex('8bc46a6556c7402400000000'),0x230:(0x1e66ca).to_bytes(4,'little'),0x1e6ad0:bytes(164)}
  for offset,value in sites.items():raw[offset:offset+len(value)]=value
  raw=bytes(raw)
  with patch.object(m,'VERIFIED_SHA',hashlib.sha256(raw).hexdigest()):
   changed=m.base_navigation_menu(raw)
   self.assertEqual(len(changed),len(raw))
   self.assertIn(b'QoLSettings_UiApi\0',changed)
   self.assertIn('Road Line'.encode('utf-16le'),changed)
   self.assertEqual(changed[0xd70ea:0xd70ed],bytes.fromhex('83ff03'))
   self.assertIn('Taxi FOV (degrees)'.encode('utf-16le'),changed)
   allowed=set(range(0xd7094,0xd709a))|set(range(0xd70ea,0xd70ed))
   for offset,text in fields:allowed.update(range(offset,offset+len(text)))
   for offset,value in sites.items():allowed.update(range(offset,offset+len(value)))
   self.assertTrue(all(i in allowed for i,(a,b) in enumerate(zip(raw,changed)) if a!=b))
   with self.assertRaises(RuntimeError):m.navigation_menu(changed)
