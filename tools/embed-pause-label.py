"""Embed the supplied fixed Paused artwork; no font file is distributed."""
from pathlib import Path
import struct
import zlib
root=Path(__file__).resolve().parents[1]
raw=(root/'assets/paused-pricedown.label').read_bytes()
if raw[:4]!=b'QPL1':raise ValueError('Unexpected fixed-label format')
w,h=struct.unpack('<II',raw[4:12])
if not (1<=w<=1024 and 1<=h<=256):raise ValueError('Invalid fixed-label dimensions')
alpha=zlib.decompress(raw[12:])
if len(alpha)!=w*h:raise ValueError('Invalid fixed-label pixel count')
source='#pragma once\n// Fixed Paused artwork rendered in Pricedown Black. No reusable font data.\n'
source+=f'constexpr unsigned pausedLabelWidth={w},pausedLabelHeight={h};\nconstexpr unsigned char pausedLabelAlpha[]={{\n'
source+='\n'.join(','.join(str(x) for x in alpha[i:i+32])+',' for i in range(0,len(alpha),32))+'\n};\n'
(root/'src/pause_label.hpp').write_text(source)
