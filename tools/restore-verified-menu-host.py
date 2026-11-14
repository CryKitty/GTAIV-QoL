"""Restore the exact working build91 menu host from the vendored verified seed.

The compressed seed contains only the host; no private artifact, installer or
credentials are required. Legacy fixture parsing remains available for tests.
"""
import ast
import base64
import hashlib
from pathlib import Path
import sys
import zlib
import struct
VERIFIED_SHA='29b726c2efca00de185c7afe00f763601979bccd4bee7dca996be77a8c33a29d'
SOURCE_SHA='01cdd2d6e330637b632bff707471b960aa500d62'
def base_navigation_menu(raw):
    """Extend only the reserved menu row in the exact verified build91 PE.

    d7094: remove the reserved id4 skip (six-byte JE -> NOPs).
    d70ea: select only row3/5 sliders; road row4 uses the existing boolean path.
    All startup code, sections, relocation entries, pointers and exports stay put.
    The UI-only API maps row4 to the live road-line toggle, keeping the public ABI intact.
    """
    if hashlib.sha256(raw).hexdigest()!=VERIFIED_SHA:raise RuntimeError('Menu adaptation requires the exact working host')
    data=bytearray(raw)
    # Reuse the former audio-enable UI row for a live taxi FOV slider. The
    # public setting ID remains unchanged; UiApi translates this menu-only row.
    # New argument helpers occupy zero-filled .text tail padding, without moving
    # any section, export, relocation, or startup routine.
    cave=0x1e6ad0
    # Verified stack contracts: row4 is 0/1/2, row3 remains taxi, row5 audio.
    helpers=bytes.fromhex(
        '50 83 ff 04 74 15 83 ff 03 75 20 b8 00 00 80 3f 89 44 24 18 b8 00 00 f0 42 eb 1e b8 00 00 80 3f'
        '89 44 24 18 b8 00 00 00 40 eb 0e b8 00 00 a0 41 89 44 24 18 b8 00 00 c8 44 89 44 24 14 31 c0 89'
        '44 24 10 58 c3 cc cc cc cc cc cc cc cc cc cc cc cc cc cc cc cc cc cc cc cc cc cc cc cc cc cc cc'
        '58 83 ff 05 74 05 ff 75 e8 eb 02 6a 07 50 8b 4d ec c3 cc cc cc cc cc cc cc cc cc cc cc cc cc cc'
        '58 c7 44 24 24 00 00 00 00 81 7d 18 00 00 f0 42 74 09 81 7d 18 00 00 00 40 75 04 6a 6c eb 02 6a'
        '65 56 50 c3'
    )
    call=lambda site,target:b'\xe8'+struct.pack('<i',target-site-5)
    branches=bytes.fromhex('83ff03 0f8409000000 83ff05 0f85d7000000')
    patches=[(0xd7094,bytes.fromhex('0f848f010000'),b'\x90'*6),
             (0xd70ea,bytes.fromhex('83ff040f82e000000083ff060f84d7000000'),branches),
             (0xd712e,bytes.fromhex('c74424100000a040'),call(0xd712e,cave)+b'\x90'*3),
             (0xd719a,bytes.fromhex('c74424100000a040'),call(0xd719a,cave)+b'\x90'*3),
             (0xd7139,bytes.fromhex('c744240c0000c843c744240800000000'),b'\x90'*16),
             (0xd71a5,bytes.fromhex('c744240c0000c843c744240800000000'),b'\x90'*16),
             (0xd715d,bytes.fromhex('8b4dec6a07'),call(0xd715d,cave+96)),
             (0xd7986,bytes.fromhex('8bc46a6556c7402400000000'),call(0xd7986,cave+128)+b'\x90'*7),
             (cave,b'\0'*len(helpers),helpers),
             (0x230,struct.pack('<I',0x1e66ca),struct.pack('<I',cave+len(helpers)-0x400))]
    for old,new,encoding in [('QoLSettings_GetApi','QoLSettings_UiApi','ascii'),
                             ('QOL_GAMEVOICE','QOL_GPSLINE','ascii'),
                             ('Gameplay Voices (%)','Road Line','utf-16le'),
                             ('Cutscene Voice Volume','Taxi FOV (degrees)','utf-16le'),
                             ('QOL_VOICES','QOL_TAXI','ascii'),
                             ('Cutscene Voices (%)','Cutscene Audio (%)','utf-16le')]:
        a=old.encode(encoding);b=new.encode(encoding)
        if raw.count(a)!=1 or len(b)>len(a):raise RuntimeError('Reserved menu text differs from verified host')
        patches.append((raw.index(a),a,b+b'\0'*(len(a)-len(b))))
    for offset,before,after in patches:
        if bytes(data[offset:offset+len(before)])!=before:raise RuntimeError('Reserved menu branch differs from verified host')
        data[offset:offset+len(before)]=after
    if len(data)!=len(raw):raise RuntimeError('Menu adaptation changed PE layout')
    return bytes(data)

def line_type_menu(raw):
    """Append position-independent row helpers; leave startup/old sections in place.

    The five extra rows cannot index the original seven-element stack arrays.
    Redirect those three reads to bounded helpers and extend the existing loop.
    No new absolute addresses or base relocations are introduced.
    """
    data=bytearray(raw)
    pe=struct.unpack_from('<I',data,0x3c)[0]
    count=struct.unpack_from('<H',data,pe+6)[0]
    optional=pe+24; table=optional+struct.unpack_from('<H',data,pe+20)[0]
    if data[pe:pe+4]!=b'PE\0\0' or struct.unpack_from('<H',data,optional)[0]!=0x10b:
        raise RuntimeError('Expected verified PE32 menu host')
    align=lambda n,a:(n+a-1)//a*a
    sa,fa=struct.unpack_from('<II',data,optional+32)
    rva=align(struct.unpack_from('<I',data,optional+56)[0],sa)
    raw_at=align(len(data),fa)
    first_raw=min(struct.unpack_from('<I',data,table+i*40+20)[0] for i in range(count))
    if table+(count+2)*40>first_raw:raise RuntimeError('No room for menu section header')
    code=bytearray(); fixes=[]; turn_fixes=[]
    # Registration order is independent of persistent setting/callback IDs.
    # Road Line's children are contiguous before Compass HUD (audio is elsewhere).
    order=[0,1,2,3,13,4,7,8,9,10,11,5,6,12]
    next_rows=[14]*14
    for a,b in zip(order,order[1:]):next_rows[a]=b
    next_row=len(code)
    code.extend(bytes.fromhex('50 e800000000 58'))
    row_anchor=len(code)-1
    code.extend(bytes.fromhex('0fb6bc38'));row_fix=len(code);code.extend(b'\0'*4)
    code.extend(bytes.fromhex('58 83ff0e 0f82'))
    j=len(code);code.extend(struct.pack('<i',0xd7c80-(rva+j+4)))
    code.extend(b'\xe9');j=len(code);code.extend(struct.pack('<i',0xd7e33-(rva+j+4)))
    row_table=len(code);code.extend(bytes(next_rows))
    struct.pack_into('<i',code,row_fix,row_table-row_anchor)
    def lookup(register,old):
        # EAX preserved; ESI/EDX alone receives the selected string pointer.
        start=len(code)
        if register=='edx':
            code.extend(bytes.fromhex('85ff 7510 50 e800000000 58 8d90'))
            turn_anchor=len(code)-3
            turn_fix=len(code);code.extend(b'\0'*4+bytes.fromhex('58 eb22'))
            turn_fixes.append((turn_fix,turn_anchor))
        code.extend(b'\x83\xff\x07\x73\x06'+old+b'\xeb\x17')
        code.extend(b'\x50\xe8\0\0\0\0\x58')
        anchor=len(code)-1 # popped return address points at POP EAX
        if register=='esi':code.extend(bytes.fromhex('8bf7 83ee07 c1e604 8db430'))
        else:code.extend(bytes.fromhex('8bd7 83ea07 c1e206 8d9410'))
        displacement=len(code);code.extend(b'\0'*4+b'\x58')
        fixes.append((displacement,anchor,register))
        return start
    key=lookup('esi',bytes.fromhex('8b74bdac'))
    # Return to caller leaving the key pushed, as the replaced instructions did.
    code.extend(bytes.fromhex('873424 56 8b742404 c3'))
    label=lookup('edx',bytes.fromhex('8b54bdc8'))
    code.extend(bytes.fromhex('8b08 c3'))
    push_label=lookup('edx',bytes.fromhex('8b54bdc8'))
    code.extend(bytes.fromhex('58 52 50 e9'))
    jump=len(code);code.extend(struct.pack('<i',0x1c690-(rva+jump+4)))
    # Capture the optional label callback on the same game-thread registration
    # path. A separate RW (non-executable) slot holds the runtime API pointer.
    capture=len(code);code.extend(bytes.fromhex('50 e800000000 58'))
    capture_anchor=len(code)-1
    code.extend(bytes.fromhex('89b0'));capture_fix=len(code);code.extend(b'\0'*4)
    code.extend(bytes.fromhex('58 60'))
    refresh_skips=[]
    for check in ('85f6','833e1c','8b4618 85c0'):
        code.extend(bytes.fromhex(check));code.extend(bytes.fromhex('72' if check=='833e1c' else '74'));refresh_skips.append(len(code));code.append(0)
    code.extend(bytes.fromhex('e800000000 5a 81c2'));refresh_anchor=len(code)-3
    refresh_fix=len(code);code.extend(b'\0'*4)
    code.extend(bytes.fromhex('52 ffd0 83c404'))
    for j in refresh_skips:code[j]=len(code)-j-1
    code.extend(bytes.fromhex('61 8975f0 85f6 c3'))
    decorate=len(code);code.extend(bytes.fromhex('60 e800000000 58'))
    decorate_anchor=len(code)-1
    code.extend(bytes.fromhex('8b80'));decorate_fix=len(code);code.extend(b'\0'*4)
    fallback_jumps=[]
    def fallback_if(op):
        code.extend(bytes.fromhex(op));fallback_jumps.append(len(code));code.append(0)
    code.extend(bytes.fromhex('85c0'));fallback_if('74')
    code.extend(bytes.fromhex('833818'));fallback_if('72')
    code.extend(bytes.fromhex('8b4014 85c0'));fallback_if('74')
    code.extend(bytes.fromhex('6a3c ff750c ff7508 ffd0 83c40c 85c0'));fallback_if('74')
    # Callback handled the row: bypass the stock copy, unwind the original
    # prologue and return nullptr. Other rows execute the displaced pushes.
    code.extend(bytes.fromhex('61 83c404 31c0 c9 c3'))
    fallback=len(code)
    for j in fallback_jumps:code[j]=fallback-j-1
    code.extend(bytes.fromhex('61 5a 56 57 8b7d08 52 c3'))
    # The native frontend caches copied row labels. Refill after the master
    # setter so grey/normal labels update immediately without leaving QoL.
    setter=len(code)
    code.extend(bytes.fromhex('55 8bec 56 8bf1 8b4508 0fb610 85d2 0f95c2 0fb6d2 52 ff7608 8b4604 ff500c 83c408 837e0804 750c 50 6a00 e8'))
    j=len(code);code.extend(struct.pack('<i',0xd90d0-(rva+j+4)))
    code.extend(bytes.fromhex('83c404 58 5e 5d c20400'))
    # Appended height row uses the existing live slider callbacks, with its
    # own discrete 0..2 / step 1 preset selector. No old row IDs move.
    height_branch=len(code);code.extend(bytes.fromhex('83c404'))
    for row in (3,5,13):
        code.extend(bytes.fromhex('83ff')+bytes([row])+bytes.fromhex('0f84'))
        j=len(code);code.extend(struct.pack('<i',0xd7cfc-(rva+j+4)))
    code.extend(b'\xe9');j=len(code);code.extend(struct.pack('<i',0xd7dd3-(rva+j+4)))
    old_range=0xd712e+0xc00+5+struct.unpack_from('<i',data,0xd712f)[0]
    height_range=len(code);code.extend(bytes.fromhex('83ff0d 7405 e9'))
    j=len(code);code.extend(struct.pack('<i',old_range-(rva+j+4)))
    code.extend(bytes.fromhex('50 c74424180000803f c744241400000040 c744241000000000 58c3'))
    old_numeric=0xd7986+0xc00+5+struct.unpack_from('<i',data,0xd7987)[0]
    height_numeric=len(code);code.extend(bytes.fromhex('817d1800000040 7405 e9'))
    j=len(code);code.extend(struct.pack('<i',old_numeric-(rva+j+4)))
    code.extend(bytes.fromhex('58 c744242400000000 6a6c 56 50 c3'))
    refresh_thunk=len(code);code.extend(bytes.fromhex('6a00 e8'))
    j=len(code);code.extend(struct.pack('<i',0xd90d0-(rva+j+4)))
    code.extend(bytes.fromhex('83c404 c3'))
    struct.pack_into('<i',code,refresh_fix,refresh_thunk-refresh_anchor)
    # Extend ExtraInfo's final native string directly, rather than patching
    # the game renderer signature that FusionFix itself needs during startup.
    footer_fixes=[];footer_thunks=[]
    for offset in (0x98,0x8c):
        footer_thunks.append(len(code))
        code.extend(bytes.fromhex('9c 60 8bec 81ec10020000 83e4f0 0fae0424 fc e800000000 58'))
        anchor=len(code)-1
        code.extend(bytes.fromhex('8b80'));fix=len(code);code.extend(b'\0'*4);footer_fixes.append((fix,anchor))
        skips=[]
        for check,branch in [('85c0','74'),('833820','72'),('8b401c 85c0','74')]:
            code.extend(bytes.fromhex(check+' '+branch));skips.append(len(code));code.append(0)
        code.extend(bytes.fromhex('ff751c ffd0 83c404 85c0 7403 89451c'))
        for j in skips:code[j]=len(code)-j-1
        code.extend(bytes.fromhex('0fae0c24 8be5 61 9d 8987')+struct.pack('<I',offset)+b'\xc3')
    keys_at=len(code)
    for key_text in ['QOL_ESCAPE','QOL_MISSION','QOL_FRIEND','QOL_ENEMY','QOL_CUSTOM','QOL_UPDATES','QOL_CAMHEIGHT']:
        code.extend(key_text.encode().ljust(16,b'\0'))
    labels_at=len(code)
    for label_text in ['Police Escape Line','Mission Line','Friend Line','Enemy Line','Custom Line','Check for QoL Updates','Driving Camera Height']:
        code.extend(label_text.encode('utf-16le').ljust(64,b'\0'))
    turns_at=len(code);code.extend('Turn Arrows\0'.encode('utf-16le'))
    for displacement,anchor in turn_fixes:struct.pack_into('<i',code,displacement,turns_at-anchor)
    for displacement,anchor,register in fixes:
        struct.pack_into('<i',code,displacement,(keys_at if register=='esi' else labels_at)-anchor)
    def patch(site,before,target):
        expected=bytes.fromhex(before)
        if data[site:site+len(expected)]!=expected:raise RuntimeError('Extra menu row contract differs from verified host')
        site_rva=site+0xc00
        data[site:site+len(expected)]=b'\xe8'+struct.pack('<i',rva+target-site_rva-5)+b'\x90'*(len(expected)-5)
    patch(0xd70ea,bytes(data[0xd70ea:0xd70fc]).hex(),height_branch)
    patch(0xd712e,bytes(data[0xd712e:0xd7136]).hex(),height_range)
    patch(0xd719a,bytes(data[0xd719a:0xd71a2]).hex(),height_range)
    patch(0xd7986,bytes(data[0xd7986:0xd7992]).hex(),height_numeric)
    patch(0xd709a,'8b74bdac56',key)
    patch(0xd70bc,'8b54bdc88b08',label)
    patch(0xd70e1,'ff74bdc8e8a649f4ff',push_label)
    if data[0xd722a:0xd722d]!=bytes.fromhex('83ff07'):raise RuntimeError('Menu row count differs')
    data[0xd7229:0xd7233]=b'\xe9'+struct.pack('<i',rva+next_row-0xd7e29-5)+b'\x90'*5
    state_rva=align(rva+len(code),sa)
    for displacement,anchor in [(capture_fix,capture_anchor),(decorate_fix,decorate_anchor)]:
        struct.pack_into('<i',code,displacement,state_rva-(rva+anchor))
    patch(0xd6fc8,'8975f085f6',capture)
    patch(0xd8268,'56578b7d08',decorate)
    patch(0xdf570,'558bec8b4508',setter)
    data[0xdf570]=0xe9 # entry tail-jump, not a call-site shim
    for site,thunk,expected in [(0x72b1e,footer_thunks[0],'898798000000'),(0x72b66,footer_thunks[1],'89878c000000')]:
        patch(site,expected,thunk)
    for displacement,anchor in footer_fixes:
        struct.pack_into('<i',code,displacement,state_rva-(rva+anchor))
    size=align(len(code),fa)
    header=struct.pack('<8sIIIIIIHHI',b'.qolmenu',len(code),rva,size,raw_at,0,0,0,0,0x60000020)
    data[table+count*40:table+(count+1)*40]=header
    state_header=struct.pack('<8sIIIIIIHHI',b'.qoldata',4,state_rva,fa,raw_at+size,0,0,0,0,0xc0000040)
    data[table+(count+1)*40:table+(count+2)*40]=state_header
    struct.pack_into('<H',data,pe+6,count+2)
    struct.pack_into('<I',data,optional+4,struct.unpack_from('<I',data,optional+4)[0]+size)
    struct.pack_into('<I',data,optional+8,struct.unpack_from('<I',data,optional+8)[0]+fa)
    struct.pack_into('<I',data,optional+56,align(state_rva+4,sa))
    struct.pack_into('<I',data,optional+64,0) # no stale optional checksum
    data.extend(b'\0'*(raw_at-len(data))+code+b'\0'*(size-len(code))+b'\0'*fa)
    return bytes(data)

def navigation_menu(raw):
    return line_type_menu(base_navigation_menu(raw))

def restore(source,out,expected=VERIFIED_SHA):
    source=Path(source)
    if source.stat().st_size>16*1024*1024:raise RuntimeError('Menu-host fixture exceeds its bound')
    tree=ast.parse(source.read_text(encoding='utf-8'))
    bridge=next(ast.literal_eval(n.value) for n in tree.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='FUSIONFIX_BRIDGE' for t in n.targets))
    raw=zlib.decompress(base64.b64decode(bridge['data'],validate=True))
    if hashlib.sha256(raw).hexdigest()!=expected or bridge['sha256']!=expected:raise RuntimeError('Known-working menu-host checksum mismatch')
    if expected==VERIFIED_SHA:raw=navigation_menu(raw)
    notice=bridge['notice'];start=notice.find('GNU GENERAL PUBLIC LICENSE')
    if start<0:raise RuntimeError('Pinned menu host is missing its license')
    out=Path(out);out.mkdir(parents=True,exist_ok=True)
    (out/'GTAIV.EFLC.FusionFix.asi').write_bytes(raw)
    (out/'FusionFix-GPL-3.0.txt').write_text(notice[start:],encoding='utf-8')
    print('Verified working FusionFix base:',expected,'; navigation-menu host:',hashlib.sha256(raw).hexdigest())
def restore_vendored(root,out):
    root=Path(root)
    packed=(root/'verified-build91.asi.zlib').read_bytes()
    if len(packed)>16*1024*1024:raise RuntimeError('Menu seed exceeds its bound')
    decoder=zlib.decompressobj();raw=decoder.decompress(packed,16*1024*1024+1)
    if not decoder.eof or decoder.unused_data or len(raw)>16*1024*1024:raise RuntimeError('Invalid compressed menu seed')
    if hashlib.sha256(raw).hexdigest()!=VERIFIED_SHA:raise RuntimeError('Known-working menu-host checksum mismatch')
    notice=(root/'LICENSE.txt').read_text(encoding='utf-8')
    if 'GNU GENERAL PUBLIC LICENSE' not in notice:raise RuntimeError('Menu seed is missing its license')
    adapted=navigation_menu(raw)
    out=Path(out);out.mkdir(parents=True,exist_ok=True)
    (out/'GTAIV.EFLC.FusionFix.asi').write_bytes(adapted)
    (out/'FusionFix-GPL-3.0.txt').write_text(notice,encoding='utf-8')
    print('Verified vendored FusionFix base:',VERIFIED_SHA,'; navigation-menu host:',hashlib.sha256(adapted).hexdigest())

if __name__=='__main__':restore_vendored(Path(__file__).resolve().parents[1]/'vendor/fusionfix-menu',sys.argv[1])
