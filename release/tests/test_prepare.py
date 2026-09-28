"""Tests for release/prepare_data.py (standard library only).

    AODD_APK=/path/to/your-1.1.1.apk python3 -m unittest discover -s release/tests -v
Optional: AODD_REFERENCE=/path/to/a/known-good/aodd  (compared file for file as well).

Tests that need the real APK are skipped without AODD_APK; the archive-safety tests don't need it.
"""
import io
import os
import shutil
import sys
import tempfile
import unittest
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import prepare_data as pd  # noqa: E402

APK = os.environ.get('AODD_APK')
REFERENCE = os.environ.get('AODD_REFERENCE')


def run(argv):
    """main() with stdout/stderr captured; returns (rc, stderr)."""
    old = sys.stdout, sys.stderr
    sys.stdout, sys.stderr = io.StringIO(), io.StringIO()
    try:
        rc = pd.main(argv)
        return rc, sys.stderr.getvalue()
    finally:
        sys.stdout, sys.stderr = old


def repack(src, dst, mutate=None, extra=(), drop=(), compression=zipfile.ZIP_DEFLATED):
    """Rewrite an APK (reversed order, other compression); mutate(name, data) may edit a member."""
    with zipfile.ZipFile(src) as zi, zipfile.ZipFile(dst, 'w', compression) as zo:
        for info in reversed(zi.infolist()):
            if info.filename in drop:
                continue
            data = zi.read(info)
            if mutate:
                data = mutate(info.filename, data)
            zo.writestr(info.filename, data)
        for name, data in extra:
            zo.writestr(name, data)


class Tmp(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix='aodd-prep-test-')

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def path(self, *p):
        return os.path.join(self.tmp, *p)

    def assert_refused(self, apk, needle):
        out = self.path('out')
        rc, err = run([apk, out])
        self.assertEqual(rc, 2, err)
        self.assertIn(needle, err)
        self.assertFalse(os.path.exists(os.path.join(out, 'aodd')), 'no output after a refusal')
        self.assertFalse(os.path.exists(os.path.join(out, 'aodd.partial')), 'staging removed')
        return err


class ArchiveSafety(Tmp):
    """Crafted archives: refused before anything is extracted (no APK needed)."""

    def crafted(self, entries, name='bad.apk'):
        p = self.path(name)
        with zipfile.ZipFile(p, 'w', zipfile.ZIP_DEFLATED) as z:
            for n, data in entries:
                if isinstance(n, zipfile.ZipInfo):
                    z.writestr(n, data)
                else:
                    z.writestr(n, data)
        return p

    def test_traversal_names_refused(self):
        for bad in ('../evil.txt', 'assets/../../evil.txt', '/abs/evil.txt', 'C:/evil.txt',
                    'assets\\..\\evil.txt', 'assets/./x', 'assets//x'):
            with self.subTest(name=bad):
                apk = self.crafted([('assets/ok.txt', b'x'), (bad, b'evil')])
                self.assert_refused(apk, 'archive entry')
                self.assertFalse(os.path.exists(self.path('evil.txt')))
                self.assertFalse(os.path.exists(os.path.join(os.path.dirname(self.tmp), 'evil.txt')))

    def test_symlink_refused(self):
        info = zipfile.ZipInfo('assets/link')
        info.external_attr = (0o120777 << 16)
        apk = self.crafted([(info, b'/etc/passwd')])
        self.assert_refused(apk, 'symlink')

    def test_case_duplicate_refused(self):
        apk = self.crafted([('assets/A.txt', b'1'), ('assets/a.txt', b'2')])
        self.assert_refused(apk, 'duplicate')

    def test_compression_bomb_refused(self):
        apk = self.crafted([('assets/zeros.bin', b'\0' * (4 * 1024 * 1024))])
        self.assert_refused(apk, 'compression ratio')

    def test_unexpected_asset_refused(self):
        apk = self.crafted([('assets/something_else.txt', b'x')])
        self.assert_refused(apk, 'unexpected asset')

    def test_not_a_zip_refused(self):
        p = self.path('junk.apk')
        with open(p, 'wb') as f:
            f.write(b'not a zip' * 100)
        self.assert_refused(p, 'not a readable APK')

    def test_bad_name_unit(self):
        for ok in ('assets/a/b.lua', 'lib/armeabi-v7a/libgame.so'):
            pd.check_member_name(ok)
        for bad in ('', 'a/../b', '..', 'x\x00y', 'a' * 300):
            with self.assertRaises(pd.PrepError):
                pd.check_member_name(bad)

    def test_out_path_confined(self):
        root = self.path('root')
        self.assertTrue(pd.out_path(root, 'assets/x').startswith(root))
        with self.assertRaises(pd.PrepError):
            pd.out_path(root, '../x')


@unittest.skipUnless(APK and os.path.isfile(APK), 'set AODD_APK to your 1.1.1 APK')
class RealApk(Tmp):
    def test_end_to_end_matches_known_good(self):
        out = self.path('out')
        rc, err = run([APK, out])
        self.assertEqual(rc, 0, err)
        root = os.path.join(out, 'aodd')
        self.assertEqual(pd.verify_tree(root), 1448)
        self.assertEqual(os.listdir(os.path.join(root, 'files')), [])
        for forbidden in ('ads.json', 'flurry.json', 'localytics.json'):
            self.assertFalse(os.path.exists(os.path.join(root, 'assets', 'serviceconfigs', forbidden)))
        rc, err = run(['--check', root])
        self.assertEqual(rc, 0, err)
        if REFERENCE:
            got = pd.tree_files(root)
            self.assertEqual(got, pd.tree_files(REFERENCE))
            for rel in got:
                with open(os.path.join(root, rel), 'rb') as a, open(os.path.join(REFERENCE, rel), 'rb') as b:
                    self.assertEqual(a.read(), b.read(), rel)

    def test_repacked_apk_accepted_by_inner_hashes(self):
        apk = self.path('repacked.apk')
        repack(APK, apk, drop=('META-INF/CERT.RSA',), compression=zipfile.ZIP_STORED)
        self.assertNotEqual(pd.sha256_file(apk), pd.APK_SHA256)
        out = self.path('out')
        old = sys.stdout
        sys.stdout = buf = io.StringIO()
        try:
            rc = pd.main([apk, out])
        finally:
            sys.stdout = old
        self.assertEqual(rc, 0)
        self.assertIn('inner-hash', buf.getvalue())
        pd.verify_tree(os.path.join(out, 'aodd'))

    def test_modified_asset_refused(self):
        target = 'assets/scripts.archondb/globals-qkjgbvmvahhiiwj5ogycgsvtg7ebksch.lua'
        apk = self.path('mod.apk')
        repack(APK, apk, mutate=lambda n, d: d + b'\n-- changed\n' if n == target else d)
        self.assert_refused(apk, target)

    def test_modified_unpatched_asset_refused_at_extraction(self):
        """An asset the offline profile never touches: only the inner-hash check sees it first."""
        with zipfile.ZipFile(APK) as z:
            target = sorted(n for n in z.namelist() if n.endswith('.png'))[0]
        apk = self.path('png.apk')
        repack(APK, apk, mutate=lambda n, d: d + b'\0' if n == target else d)
        err = self.assert_refused(apk, 'not the 1.1.1 file')
        self.assertIn(target, err)

    def test_modified_libgame_refused(self):
        apk = self.path('lib.apk')
        repack(APK, apk, mutate=lambda n, d: d[:-1] + bytes([d[-1] ^ 1]) if n.endswith('libgame.so') else d)
        self.assert_refused(apk, 'libgame.so')

    def test_missing_member_refused(self):
        apk = self.path('missing.apk')
        repack(APK, apk, drop=('lib/armeabi-v7a/libfmodex.so',))
        self.assert_refused(apk, 'lacks 1 required file')

    def test_extra_asset_refused(self):
        apk = self.path('extra.apk')
        repack(APK, apk, extra=(('assets/newer_version.lua', b'x'),))
        self.assert_refused(apk, 'unexpected asset')

    def test_traversal_member_in_real_apk_refused(self):
        apk = self.path('trav.apk')
        repack(APK, apk, extra=(('assets/../../escape.txt', b'x'),))
        self.assert_refused(apk, 'archive entry')
        self.assertFalse(os.path.exists(self.path('escape.txt')))

    def test_existing_output_refused_and_saves_untouched(self):
        out = self.path('out')
        save = os.path.join(out, 'aodd', 'files', 'UserData.dat')
        os.makedirs(os.path.dirname(save))
        with open(save, 'wb') as f:
            f.write(b'my progress')
        rc, err = run([APK, out])
        self.assertEqual(rc, 2)
        self.assertIn('already exists', err)
        with open(save, 'rb') as f:
            self.assertEqual(f.read(), b'my progress')
        self.assertEqual(os.listdir(out), ['aodd'])

    def test_check_detects_tamper_and_saves_in_tree(self):
        out = self.path('out')
        self.assertEqual(run([APK, out])[0], 0)
        root = os.path.join(out, 'aodd')
        lua = os.path.join(root, 'assets', 'scripts.archondb', 'coinstore-cdzrszrqzhyuifgcbh3xptld2a5xkfzm.lua')
        with open(lua, 'ab') as f:
            f.write(b' ')
        rc, err = run(['--check', root])
        self.assertEqual(rc, 2)
        self.assertIn('differ', err)

    def test_prepared_tree_has_no_save_files(self):
        """Copying the prepared tree onto ux0:data/aodd cannot overwrite saves: files/ is empty."""
        out = self.path('out')
        self.assertEqual(run([APK, out])[0], 0)
        root = os.path.join(out, 'aodd')
        os.makedirs(os.path.join(root, 'files', 'x'))
        rc, err = run(['--check', root])
        self.assertEqual(rc, 2)
        self.assertIn('files/', err)


if __name__ == '__main__':
    unittest.main()
