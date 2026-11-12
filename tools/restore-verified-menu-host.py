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
def navigation_menu(raw):
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
