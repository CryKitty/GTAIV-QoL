"""Validate release identity and canonical ancestry; does not create tags/releases."""
from pathlib import Path
import re
import subprocess
import sys
import ast

def check(root,tag):
    root=Path(root);version=(root/'VERSION').read_text().strip()
    if not re.fullmatch(r'\d+\.\d+\.\d+-beta\.\d+',version) or tag!='v'+version:
        raise RuntimeError('Beta tag must match VERSION exactly.')
    tree=ast.parse((Path(__file__).parent/'steam-deck/qol-updater.py').read_text(encoding='utf-8'))
    source=next(ast.literal_eval(n.value) for n in tree.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='UPDATE_SOURCE' for t in n.targets))
    subprocess.run(['git','merge-base','--is-ancestor','HEAD','refs/remotes/origin/'+source['branch']],cwd=root,check=True)
    print('Beta tag matches VERSION and is reachable from canonical '+source['branch']+'.')

if __name__=='__main__':check(Path(__file__).resolve().parents[1],sys.argv[1])
