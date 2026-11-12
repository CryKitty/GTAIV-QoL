"""Execute the bounded x86 menu helpers against their actual stack contracts."""
import importlib.util,hashlib,struct,unittest
from pathlib import Path
from unittest.mock import patch
try:
 import unicorn
 from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDI,UC_X86_REG_EBP,UC_X86_REG_ESI,UC_X86_REG_ESP
except ImportError:
 unicorn=None
ROOT=Path(__file__).resolve().parents[1]
sp=importlib.util.spec_from_file_location('menu',ROOT/'tools/restore-verified-menu-host.py');menu=importlib.util.module_from_spec(sp);sp.loader.exec_module(menu)
@unittest.skipUnless(unicorn,'x86 emulator not installed')
class SliderMachineTests(unittest.TestCase):
 def fixture(self):
  raw=bytearray(0x273000)
  sites={0xd7094:'0f848f010000',0xd70ea:'83ff040f82e000000083ff060f84d7000000',0xd712e:'c74424100000a040',0xd719a:'c74424100000a040',0xd7139:'c744240c0000c843c744240800000000',0xd71a5:'c744240c0000c843c744240800000000',0xd715d:'8b4dec6a07',0xd7986:'8bc46a6556c7402400000000',0x230:'ca661e00'}
  for offset,value in sites.items():raw[offset:offset+len(bytes.fromhex(value))]=bytes.fromhex(value)
  for offset,text in [(0x2724b8,b'QoLSettings_GetApi'),(0x272500,b'QOL_GAMEVOICE'),(0x2725b4,'Gameplay Voices (%)'.encode('utf-16le')),(0x272650,'Cutscene Voice Volume'.encode('utf-16le')),(0x272700,'Cutscene Voices (%)'.encode('utf-16le')),(0x272780,b'QOL_VOICES')]:raw[offset:offset+len(text)]=text
  with patch.object(menu,'VERIFIED_SHA',hashlib.sha256(raw).hexdigest()):return menu.base_navigation_menu(bytes(raw))
 def machine(self,row):
  u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32);u.mem_map(0x1000000,0x300000);u.mem_write(0x1000000,self.fixture());u.mem_map(0x800000,0x10000)
  for reg,value in [(UC_X86_REG_EDI,row),(UC_X86_REG_ESP,0x804000),(UC_X86_REG_EBP,0x808000),(UC_X86_REG_EAX,0x11223344)]:u.reg_write(reg,value)
  return u
 def test_ranges_and_register_stack_preservation(self):
  for site in [0xd712e,0xd719a]:
   for row,expected in [(3,(0,120,1)),(4,(0,2,1)),(5,(0,1600,20))]:
    u=self.machine(row);u.emu_start(0x1000000+site,0x1000000+site+8,count=100)
    self.assertEqual(u.reg_read(UC_X86_REG_ESP),0x804000);self.assertEqual(u.reg_read(UC_X86_REG_EAX),0x11223344)
    self.assertEqual(struct.unpack('<fff',u.mem_read(0x804008,12)),expected)
 def test_taxi_uses_qol_screen_and_audio_keeps_audio_screen(self):
  for row,screen in [(3,88),(4,88),(5,7)]:
   u=self.machine(row);u.mem_write(0x808000-0x18,struct.pack('<II',88,0x12345678));u.emu_start(0x10d715d,0x10d7162,count=100)
   self.assertEqual(u.reg_read(UC_X86_REG_ESP),0x803ffc);self.assertEqual(struct.unpack('<I',u.mem_read(0x803ffc,4))[0],screen);self.assertEqual(u.reg_read(UC_X86_REG_ECX),0x12345678)
 def test_road_uses_boolean_path_and_only_taxi_audio_are_sliders(self):
  for row in range(7):
   u=self.machine(row);reached=[]
   def stop(machine,address,size,unused):
    if address in [0x10d70fc,0x10d71d3]:reached.append(address);machine.emu_stop()
   u.hook_add(unicorn.UC_HOOK_CODE,stop);u.emu_start(0x10d70ea,0x10d71d4,count=20)
   self.assertEqual(reached,[0x10d70fc if row in [3,5] else 0x10d71d3])

 def test_numeric_taxi_display_keeps_audio_as_a_bar(self):
  for maximum,display in [(120,108),(2,108),(1600,101)]:
   u=self.machine(3);u.reg_write(UC_X86_REG_ESI,77)
   u.mem_write(0x808018,struct.pack('<f',maximum))
   u.emu_start(0x10d7986,0x10d7992,count=100)
   self.assertEqual(u.reg_read(UC_X86_REG_ESP),0x803ff8)
   self.assertEqual(struct.unpack('<II',u.mem_read(0x803ff8,8)),(77,display))
   self.assertEqual(struct.unpack('<I',u.mem_read(0x804024,4))[0],0)
