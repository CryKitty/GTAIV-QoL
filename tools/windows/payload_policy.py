"""Keep installer-only CI commits out of the mod updater candidate set."""
from fnmatch import fnmatchcase
import os
from pathlib import Path
import subprocess

BASELINE='v1.0.0'
INSTALLER_ONLY=('tools/windows/*','tools/steam-deck/dependency_setup.py','tools/steam-deck/package-nexus.py',
                'docs/*','README.md','CHANGELOG.md','tests/*',
                '.github/workflows/windows-installer.yml','.github/workflows/build.yml')
def publish_payload(changed):
    return not changed or any(not any(fnmatchcase(path,pattern) for pattern in INSTALLER_ONLY) for path in changed)
def main():
    changed=subprocess.check_output(['git','diff','--name-only',BASELINE,'HEAD'],text=True).splitlines()
    publish=publish_payload(changed)
    if os.environ.get('GITHUB_OUTPUT'):
        with Path(os.environ['GITHUB_OUTPUT']).open('a') as stream:stream.write('publish='+str(publish).lower()+'\n')
    print('Publish mod payload:',publish,'; installer-only commits retain the released/deployed payload identity.')
if __name__=='__main__':main()
