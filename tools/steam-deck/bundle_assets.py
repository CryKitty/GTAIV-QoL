"""Embed the verified, pinned FusionFix menu bridge into the existing payload schema."""
import base64
import hashlib
import os
from pathlib import Path
import zlib
import ast

def update_source(root):
    tree=ast.parse((Path(root)/'tools/steam-deck/qol-updater.py').read_text(encoding='utf-8'))
    return next(ast.literal_eval(n.value) for n in tree.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='UPDATE_SOURCE' for t in n.targets))


def render_helper(root):
    source = (root/'tools/steam-deck/enable-qol-logs.py').read_text(encoding='utf-8')
    if 'FUSIONFIX_BRIDGE = {}' not in source:
        return source  # Packaging fixtures or older source, without a bridge declaration.
    data = (root/'build/GTAIV.EFLC.FusionFix.asi').read_bytes()
    license_text = (root/'build/FusionFix-GPL-3.0.txt').read_text(encoding='utf-8')
    sha = os.environ.get('GITHUB_SHA', 'local')
    menu_source = sha
    notice = ('FusionFix 5.1.2 with CryKitty QoL menu bridge (modified; not an official FusionFix binary).\n'
              'Copyright: FusionFix contributors. License: GPL-3.0.\n'
              'Unmodified base source: https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix/tree/cad0970e2c82712d86c849974b62d078331ddd88\n'
              'Corresponding bridge source and build instructions: https://github.com/CryKitty/GTAIV-QoL/blob/'+menu_source+'/vendor/fusionfix-menu/README.md\n'
              'Navigation-menu adaptation source: https://github.com/CryKitty/GTAIV-QoL/blob/'+sha+'/tools/restore-verified-menu-host.py\n'
              'Full bundle source: https://github.com/CryKitty/GTAIV-QoL/tree/'+sha+'\n'
              'RoadNav is included from CryKitty/GTAIV-Road-Nav commit 6b9b66f540d8139cb1303087cf282c6b89bf3abd, with shared settings and bundle initialization adaptations.\n'
              'Native ScriptHook SDK: Copyright (C) 2009 Aru; license in vendor/roadnav/sdk/LICENSE.md and vendor/scripthook/LICENSE.md.\n\n'+license_text)
    for relative in ('vendor/roadnav/release/THIRD-PARTY-NOTICES.txt', 'vendor/roadnav/sdk/LICENSE.md', 'vendor/minhook/LICENSE.txt'):
        path = root/relative
        if path.is_file():
            notice += '\n\n'+relative+'\n'+path.read_text(encoding='utf-8')
    bundle = {'data': base64.b64encode(zlib.compress(data, 9)).decode(),
              'sha256': hashlib.sha256(data).hexdigest(),
              'supported_sha256': ['14f6ac879bcc7df5fb18ce7b410bc436e16675b0dad640378dcb1e6dd3be98b3'],
              'notice': notice}
    return source.replace('FUSIONFIX_BRIDGE = {}', 'FUSIONFIX_BRIDGE = '+repr(bundle), 1)
