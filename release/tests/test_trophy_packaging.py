"""Test an extracted preparation kit, independent of source-checkout imports."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import zipfile

RELEASE=Path(__file__).resolve().parents[1]


class PreparationKit(unittest.TestCase):
    @unittest.skipUnless((RELEASE/'build_release.py').is_file(),'source packager only')
    def test_actual_preparation_kit(self):
        sys.path.insert(0,str(RELEASE))
        import build_release
        members=[]
        for path in RELEASE.rglob('*'):
            if path.is_file() and '__pycache__' not in path.parts:
                members.append(('release/'+str(path.relative_to(RELEASE)),'100644',path.read_bytes()))
        for name in ['LICENSE','LICENSES/GoldenBalloon-MIT.txt']:
            members.append((name,'100644',(RELEASE.parent/name).read_bytes()))
        with tempfile.TemporaryDirectory() as directory:
            kit=Path(directory)/'kit.zip';out=Path(directory)/'kit'
            build_release.build_kit(members,str(kit))
            with zipfile.ZipFile(kit) as archive:
                self.assertTrue({'trophies/build_pack.py','trophies/mapping.json','trophies/metadata.json',
                                 'LICENSES/GoldenBalloon-MIT.txt'} <= set(archive.namelist()))
                archive.extractall(out)
            env=dict(os.environ)
            for key in ['AODD_ASSETS','AODD_TROPHY_CORE']:
                env.pop(key,None)
            # This runs the different trophy test module, not this packager test.
            check=subprocess.run([sys.executable,'-m','unittest','discover','-s','tests','-p','test_trophies.py','-v'],
                                 cwd=out,env=env,text=True,capture_output=True)
            self.assertEqual(check.returncode,0,check.stdout+check.stderr)
            self.assertNotIn('ModuleNotFoundError',check.stderr)


if __name__=='__main__': unittest.main(verbosity=2)
