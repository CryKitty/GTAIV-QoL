"""Compile the installed VERSION into the native pause-menu label."""
from pathlib import Path
import re

def generate(root):
    root=Path(root);version=(root/'VERSION').read_text().strip()
    if not re.fullmatch(r'\d+\.\d+\.\d+(?:-beta\.\d+)?',version):raise RuntimeError('Invalid VERSION')
    target=root/'src/qol_version.hpp';target.parent.mkdir(parents=True,exist_ok=True)
    target.write_text('#pragma once\n#define QOL_VERSION "'+version+'"\n#define QOL_VERSION_WIDE L"'+version+'"\n')
    return version
if __name__=='__main__':generate(Path(__file__).resolve().parents[1])
