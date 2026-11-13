"""Run the actual public host's extra-row helpers at preferred/rebased addresses."""
import importlib.util,struct,unittest,zlib
from pathlib import Path
try:
 import unicorn
 from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_EDI,UC_X86_REG_EBP,UC_X86_REG_ESI,UC_X86_REG_ESP
except ImportError:
 unicorn=None
ROOT=Path(__file__).resolve().parents[1]
s=importlib.util.spec_from_file_location('extended_menu',ROOT/'tools/restore-verified-menu-host.py');menu=importlib.util.module_from_spec(s);s.loader.exec_module(menu)
@unittest.skipUnless(unicorn,'x86 emulator not installed')
class LineTypeMenuTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.original=zlib.decompress((ROOT/'vendor/fusionfix-menu/verified-build91.asi.zlib').read_bytes())
  cls.image=menu.navigation_menu(cls.original)
 def machine(self,base,row):
  raw=self.image;pe=struct.unpack_from('<I',raw,0x3c)[0];optional=pe+24
  size=struct.unpack_from('<I',raw,optional+56)[0]
  u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32);u.mem_map(base,size);u.mem_write(base,raw[:0x400])
  table=optional+struct.unpack_from('<H',raw,pe+20)[0]
  for i in range(struct.unpack_from('<H',raw,pe+6)[0]):
   virtual,rva,length,offset=struct.unpack_from('<IIII',raw,table+i*40+8)
   u.mem_write(base+rva,raw[offset:offset+length])
  u.mem_map(0x800000,0x10000)
  for reg,value in [(UC_X86_REG_EDI,row),(UC_X86_REG_ESP,0x804000),(UC_X86_REG_EBP,0x808000),(UC_X86_REG_EAX,0x809000)]:u.reg_write(reg,value)
  u.mem_write(0x809000,struct.pack('<I',0x12345678))
  # Original rows must continue using the original caller's stack arrays.
  for i in range(7):
   u.mem_write(0x808000-0x54+i*4,struct.pack('<I',0x80a000+i*0x100))
   u.mem_write(0x808000-0x38+i*4,struct.pack('<I',0x80b000+i*0x100))
  return u
 def test_all_rows_keys_labels_and_original_stack_contracts_after_rebase(self):
  keys=['QOL_ESCAPE','QOL_MISSION','QOL_FRIEND','QOL_ENEMY','QOL_CUSTOM','QOL_UPDATES','QOL_CAMHEIGHT']
  labels=['Police Escape Line','Mission Line','Friend Line','Enemy Line','Custom Line','Check for QoL Updates','Driving Camera Height (cm)']
  for base in [0x10000000,0x30000000]:
   for row in range(14):
    u=self.machine(base,row);site=base+0xd7c9a
    u.emu_start(site,site+5,count=100)
    key=u.reg_read(UC_X86_REG_ESI)
    self.assertEqual(u.reg_read(UC_X86_REG_ESP),0x803ffc)
    self.assertEqual(struct.unpack('<I',u.mem_read(0x803ffc,4))[0],key)
    if row<7:self.assertEqual(key,0x80a000+row*0x100)
    else:self.assertEqual(bytes(u.mem_read(key,16)).split(b'\0')[0].decode(),keys[row-7])
    u=self.machine(base,row);site=base+0xd7cbc
    u.emu_start(site,site+6,count=100);label=u.reg_read(UC_X86_REG_EDX)
    self.assertEqual(u.reg_read(UC_X86_REG_ECX),0x12345678)
    self.assertEqual(u.reg_read(UC_X86_REG_EAX),0x809000)
    self.assertEqual(u.reg_read(UC_X86_REG_ESP),0x804000)
    if row==0:self.assertEqual(bytes(u.mem_read(label,24)).decode('utf-16le').split('\0')[0],'Turn Arrows')
    elif row<7:self.assertEqual(label,0x80b000+row*0x100)
    else:self.assertEqual(bytes(u.mem_read(label,64)).decode('utf-16le').rstrip('\0'),labels[row-7])
    # Replace PUSH label / CALL assign: assign receives exactly [return,label,length].
    u=self.machine(base,row);u.reg_write(UC_X86_REG_ECX,0x12345678);u.mem_write(0x804000,struct.pack('<I',17))
    observed=[]
    def stop(machine,address,size,unused):
     if address==base+0x1c690:observed.extend(struct.unpack('<III',machine.mem_read(machine.reg_read(UC_X86_REG_ESP),12)));machine.emu_stop()
    u.hook_add(unicorn.UC_HOOK_CODE,stop);u.emu_start(base+0xd7ce1,base+0xd7cea,count=100)
    self.assertEqual(observed,[base+0xd7ce6,label,17]);self.assertEqual(u.reg_read(UC_X86_REG_ECX),0x12345678)
    self.assertEqual(u.reg_read(UC_X86_REG_EDI),row)
 def test_height_slider_range_screen_and_numeric_display_after_rebase(self):
  for base in [0x10000000,0x30000000]:
   for row,expected in [(3,(0,120,1)),(5,(0,1600,20)),(13,(0,200,5))]:
    for site in [0xd7d2e,0xd7d9a]:
     u=self.machine(base,row);u.emu_start(base+site,base+site+8,count=100)
     self.assertEqual(u.reg_read(UC_X86_REG_ESP),0x804000)
     self.assertEqual(struct.unpack('<fff',u.mem_read(0x804008,12)),expected)
    u=self.machine(base,row);u.reg_write(UC_X86_REG_ESI,77);u.mem_write(0x808018,struct.pack('<f',expected[1]))
    u.emu_start(base+0xd8586,base+0xd8592,count=100)
    self.assertEqual(struct.unpack('<II',u.mem_read(0x803ff8,8)),(77,108 if row!=5 else 101))
 def test_extra_rows_use_toggle_path_not_sliders(self):
  base=0x10000000
  for row in range(14):
   u=self.machine(base,row);reached=[]
   def stop(machine,address,size,unused):
    if address in [base+0xd7cfc,base+0xd7dd3]:reached.append(address);machine.emu_stop()
   u.hook_add(unicorn.UC_HOOK_CODE,stop);u.emu_start(base+0xd7cea,base+0xd7dd4,count=20)
   self.assertEqual(reached,[base+0xd7cfc if row in [3,5,13] else base+0xd7dd3])
 def test_append_only_section_keeps_loader_and_relocations_in_place(self):
  old=menu.base_navigation_menu(self.original);new=self.image
  pe=struct.unpack_from('<I',old,0x3c)[0];optional=pe+24
  table=optional+struct.unpack_from('<H',old,pe+20)[0];count=struct.unpack_from('<H',old,pe+6)[0]
  self.assertEqual(struct.unpack_from('<H',new,pe+6)[0],count+2)
  self.assertEqual(new[table:table+count*40],old[table:table+count*40])
  self.assertEqual(new[optional+96:optional+224],old[optional+96:optional+224]) # all directories incl relocations/exports
  allowed=set(range(pe+6,pe+8))|set(range(optional+4,optional+12))|set(range(optional+56,optional+60))|set(range(optional+64,optional+68))|set(range(table+count*40,table+(count+2)*40))
  for site,length in [(0xd709a,5),(0xd70bc,6),(0xd70e1,9),(0xd7229,10),(0xd6fc8,5),(0xd8268,5),(0xdf570,6),(0xd70ea,18),(0xd712e,8),(0xd719a,8),(0xd7986,12)]:allowed.update(range(site,site+length))
  self.assertTrue(all(i in allowed for i,(a,b) in enumerate(zip(old,new)) if a!=b))
  self.assertEqual(new[0xd7229],0xe9)
 def test_registration_order_nests_all_children_immediately_beneath_master(self):
  for base in [0x10000000,0x30000000]:
   u=self.machine(base,0);order=[0]
   for _ in range(14):
    u.emu_start(base+0xd7e29,base+(0xd7c80 if len(order)<14 else 0xd7e33),count=100)
    row=u.reg_read(UC_X86_REG_EDI);order.append(row)
   self.assertEqual(order,[0,1,2,3,13,4,7,8,9,10,11,5,6,12,14])
 def test_optional_label_bridge_and_old_core_fallback_preserve_x86_contract(self):
  for base in [0x10000000,0x30000000]:
   for api_size,handled in [(20,False),(24,False),(24,True)]:
    u=self.machine(base,7)
    # Registration capture keeps EAX and the displaced MOV/TEST contract.
    u.reg_write(UC_X86_REG_ESI,0x80c000)
    u.emu_start(base+0xd7bc8,base+0xd7bcd,count=100)
    self.assertEqual(struct.unpack('<I',u.mem_read(0x807ff0,4))[0],0x80c000)
    self.assertEqual(u.reg_read(UC_X86_REG_EAX),0x809000)
    u.mem_write(0x80c000,struct.pack('<6I',api_size,1,0,0,0,0x80d000))
    called=[]
    def inspect(machine,address,size,unused):
     if address==0x80d000:
      args=struct.unpack('<4I',machine.mem_read(machine.reg_read(UC_X86_REG_ESP),16))
      called.append(args[1:]);self.assertEqual(args[1:],(0x80a000,0x80b000,60))
    u.hook_add(unicorn.UC_HOOK_CODE,inspect)
    # Callback stub, returning its boolean result, cdecl (caller pops args).
    u.mem_write(0x80d000,b'\xb8'+struct.pack('<I',int(handled))+b'\xc3')
    u.mem_write(0x808000,struct.pack('<4I',0x808100,0x80e000,0x80a000,0x80b000))
    u.reg_write(UC_X86_REG_EBP,0x808000);u.reg_write(UC_X86_REG_ESP,0x807ffc)
    u.reg_write(UC_X86_REG_ESI,0x11223344);u.reg_write(UC_X86_REG_EDI,0x55667788)
    expect_handled=api_size>=24 and handled
    u.emu_start(base+0xd8e68,0x80e000 if expect_handled else base+0xd8e6d,count=100)
    self.assertEqual(len(called),int(api_size>=24))
    if expect_handled:
     self.assertEqual(u.reg_read(UC_X86_REG_EAX),0)
     self.assertEqual(u.reg_read(UC_X86_REG_EBP),0x808100)
     self.assertEqual(u.reg_read(UC_X86_REG_ESP),0x808008)
     self.assertEqual(u.reg_read(UC_X86_REG_EDI),0x55667788)
    else:
     self.assertEqual(u.reg_read(UC_X86_REG_EDI),0x80a000)
     self.assertEqual(u.reg_read(UC_X86_REG_ESP),0x807ff4)
     self.assertEqual(struct.unpack('<2I',u.mem_read(0x807ff4,8)),(0x55667788,0x11223344))

 def test_master_setter_refreshes_cached_labels_without_changing_callback_ids(self):
  for base in [0x10000000,0x30000000]:
   for row in range(14):
    for value in [0,1]:
     u=self.machine(base,row)
     u.mem_write(0x809100,struct.pack('<3I',0,0x80c000,row))
     u.mem_write(0x80c000,struct.pack('<4I',24,1,0,0x80d000))
     u.mem_write(0x80d000,bytes.fromhex('b801000000c3'))
     u.mem_write(base+0xd90d0,bytes.fromhex('b816000000c3'))
     u.mem_write(0x80a000,bytes([value]));u.mem_write(0x804000,struct.pack('<2I',0x80e000,0x80a000))
     u.reg_write(UC_X86_REG_ECX,0x809100);u.reg_write(UC_X86_REG_ESI,0x11223344)
     calls=[]
     def inspect(machine,address,size,unused):
      if address in [0x80d000,base+0xd90d0]:
       args=struct.unpack('<3I',machine.mem_read(machine.reg_read(UC_X86_REG_ESP),12))
       calls.append((address,args[1:]))
     u.hook_add(unicorn.UC_HOOK_CODE,inspect);u.emu_start(base+0xe0170,0x80e000,count=100)
     self.assertEqual(calls[0],(0x80d000,(row,value)))
     self.assertEqual(len(calls),2 if row==4 else 1)
     if row==4:self.assertEqual(calls[1][1][0],0)
     self.assertEqual(u.reg_read(UC_X86_REG_EAX),1)
     self.assertEqual(u.reg_read(UC_X86_REG_ESP),0x804008)
     self.assertEqual(u.reg_read(UC_X86_REG_ESI),0x11223344)
