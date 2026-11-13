"""Compile the installed VERSION into the native pause-menu label."""
from pathlib import Path
import re
import os

def generate(root):
    root=Path(root);version=(root/'VERSION').read_text().strip()
    if not re.fullmatch(r'\d+\.\d+\.\d+(?:-beta\.\d+)?',version):raise RuntimeError('Invalid VERSION')
    target=root/'src/qol_version.hpp';target.parent.mkdir(parents=True,exist_ok=True)
    sha=os.environ.get('GITHUB_SHA','');sha=sha if re.fullmatch('[0-9a-f]{40}',sha) else ''
    run=os.environ.get('GITHUB_RUN_NUMBER','');build='b'+run if re.fullmatch('[0-9]{1,6}',run) else '@'+sha[:7] if sha else 'local'
    target.write_text('#pragma once\n#define QOL_VERSION "'+version+'"\n#define QOL_VERSION_WIDE L"'+version+'"\n#define QOL_BUILD_WIDE L"'+build+'"\n#define QOL_SOURCE_SHA "'+sha+'"\n')
    return version
if __name__=='__main__':generate(Path(__file__).resolve().parents[1])
