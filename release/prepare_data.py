#!/usr/bin/env python3
"""Prepare the ux0:data/aodd/ data tree for the Army of Darkness Defense PS Vita port
from YOUR OWN copy of the Android game, version 1.1.1.

The port ships no game code or game data. This tool reads your APK and writes the data
tree the Vita port needs, entirely offline (Python 3.8+, standard library only):

  1. Checks the APK. Either the whole file has the known 1.1.1 sha256, or every file the port
     needs (2 native libraries + 1497 asset files) has its known sha256 ("inner-hash" mode,
     for an APK that was re-packed but whose game files are unchanged). Anything else fails.
  2. Rejects unsafe or unexpected archives: absolute paths, "..", backslashes, drive letters,
     symlinks, encrypted or duplicate entries, and oversized or suspiciously compressed members.
  3. Extracts only libgame.so, libfmodex.so and assets/.
  4. Applies the port's offline profile (offline/aod_offline.py): ads, analytics and social
     tie-ins removed, and coin-store purchases approved locally. Every edit checks the input
     file's sha256 and a unique anchor first.
  5. Writes MANIFEST.sha256, then checks the complete result, file for file, against the
     known-good tree (expected/data-offline.sha256). Only then does it rename the staging
     directory to OUT/aodd.

    python3 prepare_data.py YOUR.apk OUT          -> OUT/aodd/
    python3 prepare_data.py --check OUT/aodd      re-verify a prepared tree

It never writes outside OUT and never modifies the APK. OUT/aodd must not exist yet.
Copy OUT/aodd/* to ux0:data/aodd/ on the Vita; see README.md for keeping your saves.
"""
import argparse
import hashlib
import json
import os
import shutil
import stat
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
OFFLINE_DIR = os.path.join(HERE, 'offline')
EXPECTED_INPUTS = os.path.join(HERE, 'expected', 'apk-members.sha256')
EXPECTED_OUTPUT = os.path.join(HERE, 'expected', 'data-offline.sha256')

APK_SHA256 = '4800eac5d52807a7c0d0b4f3e41489a1ce68de7baa0d50b8aa79c4207ebf6bf4'
LIBS = {'lib/armeabi-v7a/libgame.so': 'libgame.so', 'lib/armeabi-v7a/libfmodex.so': 'libfmodex.so'}
MANIFEST = 'MANIFEST.sha256'
REPORT = 'ablation-report.json'

# Bounds (the real 1.1.1 APK: 2056 entries, 65.2 MB uncompressed, largest member 9.2 MB).
MAX_APK_BYTES = 512 * 1024 * 1024
MAX_ENTRIES = 20000
MAX_MEMBER_BYTES = 64 * 1024 * 1024
MAX_TOTAL_BYTES = 256 * 1024 * 1024
MAX_RATIO = 200            # uncompressed / compressed, for members over 1 MiB
MAX_NAME = 255
CHUNK = 1 << 20


class PrepError(Exception):
    """A refusal: the input or output is not what the port supports."""


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for _ in range(MAX_APK_BYTES // CHUNK + 2):
            b = f.read(CHUNK)
            if not b:
                return h.hexdigest()
            h.update(b)
    raise PrepError('%s is larger than %d bytes' % (path, MAX_APK_BYTES))


def load_hash_list(path):
    """'<sha256>  <relative/path>' lines -> ordered dict path -> sha256."""
    out = {}
    with open(path, 'r', encoding='utf-8', newline='\n') as f:
        lines = f.read().split('\n')
    if lines[-1] != '':
        raise PrepError('%s: missing final newline' % path)
    for ln in lines[:-1]:
        digest, sep, rel = ln.partition('  ')
        if len(digest) != 64 or not sep or not rel or rel in out:
            raise PrepError('%s: malformed line %r' % (path, ln[:80]))
        out[rel] = digest
    return out


def check_member_name(name):
    """Refuse any archive name that could escape the output or collide on Windows/macOS."""
    if not name or len(name) > MAX_NAME or '\x00' in name or '\\' in name:
        raise PrepError('unsafe archive entry name %r' % name[:80])
    if name.startswith('/') or (len(name) > 1 and name[1] == ':'):
        raise PrepError('absolute archive entry name %r' % name[:80])
    parts = name.rstrip('/').split('/')
    if any(p in ('', '.', '..') for p in parts):
        raise PrepError('archive entry with relative path components %r' % name[:80])


def check_member_info(info):
    mode = (info.external_attr >> 16) & 0o170000
    if mode == stat.S_IFLNK:
        raise PrepError('archive entry is a symlink: %r' % info.filename)
    if info.flag_bits & 0x1:
        raise PrepError('archive entry is encrypted: %r' % info.filename)
    if info.file_size > MAX_MEMBER_BYTES:
        raise PrepError('archive entry too large: %r (%d bytes)' % (info.filename, info.file_size))
    if info.file_size > CHUNK and info.file_size > MAX_RATIO * max(info.compress_size, 1):
        raise PrepError('archive entry compression ratio too high: %r' % info.filename)


def scan_archive(z, wanted):
    """Validate every entry; return {name: ZipInfo} for the wanted members."""
    infos = z.infolist()
    if len(infos) > MAX_ENTRIES:
        raise PrepError('archive has %d entries (limit %d)' % (len(infos), MAX_ENTRIES))
    seen = set()
    total = 0
    for info in infos:                      # pass 1: every entry must be safe, whatever it is
        check_member_name(info.filename)
        key = info.filename.lower()
        if key in seen:
            raise PrepError('duplicate archive entry (case-insensitive): %r' % info.filename)
        seen.add(key)
        check_member_info(info)
        total += info.file_size
    if total > MAX_TOTAL_BYTES:
        raise PrepError('archive expands to %d bytes (limit %d)' % (total, MAX_TOTAL_BYTES))
    found = {}
    for info in infos:                      # pass 2: exactly the 1.1.1 file set under assets/
        if info.filename in wanted:
            found[info.filename] = info
        elif info.filename.startswith('assets/') and not info.filename.endswith('/'):
            raise PrepError('unexpected asset %r: this is not Army of Darkness Defense 1.1.1' % info.filename)
    missing = sorted(set(wanted) - set(found))
    if missing:
        raise PrepError('APK lacks %d required file(s), e.g. %s: not version 1.1.1?' % (len(missing), missing[:3]))
    return found


def out_path(root, rel):
    """Map a verified relative name under root; refuse anything that resolves outside it."""
    dest = os.path.normpath(os.path.join(root, *rel.split('/')))
    base = os.path.normpath(root)
    if os.path.commonpath([base, dest]) != base or dest == base:
        raise PrepError('refusing to write outside the output: %r' % rel)
    return dest


def extract_member(z, info, dest, want):
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    h = hashlib.sha256()
    n = 0
    with z.open(info) as src, open(dest, 'xb') as dst:
        for _ in range(MAX_MEMBER_BYTES // CHUNK + 2):
            b = src.read(CHUNK)
            if not b:
                break
            n += len(b)
            if n > MAX_MEMBER_BYTES:
                raise PrepError('archive entry grew past its limit: %r' % info.filename)
            h.update(b)
            dst.write(b)
    if h.hexdigest() != want:
        raise PrepError('%s sha256 %s != expected %s (not the 1.1.1 file)' % (info.filename, h.hexdigest(), want))


def extract_inputs(apk, root):
    """Extract and hash-check exactly the members the port needs. Returns the verification mode."""
    wanted = load_hash_list(EXPECTED_INPUTS)
    if set(LIBS) - set(wanted):
        raise PrepError('expected/apk-members.sha256 lacks the libraries (damaged tool?)')
    size = os.path.getsize(apk)
    if size > MAX_APK_BYTES:
        raise PrepError('%s is larger than %d bytes' % (apk, MAX_APK_BYTES))
    mode = 'exact-apk' if sha256_file(apk) == APK_SHA256 else 'inner-hash'
    try:
        z = zipfile.ZipFile(apk)
    except (zipfile.BadZipFile, OSError) as e:
        raise PrepError('%s is not a readable APK/zip: %s' % (apk, e))
    with z:
        found = scan_archive(z, wanted)
        for name in sorted(found):
            rel = LIBS.get(name, name)
            extract_member(z, found[name], out_path(root, rel), wanted[name])
    return mode, len(found)


def run_offline_profile(root):
    """The profile exactly as aod_offline.py's default ('full', web links removed, offline IAP)."""
    sys.path.insert(0, OFFLINE_DIR)
    import aod_offline as ao  # noqa: E402  (vendored, see offline/PROVENANCE.md)
    key = ao.read_key(os.path.join(root, 'libgame.so'))
    t = ao.Tree(os.path.join(root, 'assets'))
    ao.assert_inputs(t, key)
    ao.apply_lua(t, ao.LUA_CORE, 'ads/social/telemetry call sites removed')
    import aod_trophies
    aod_trophies.apply(t)  # before manifest rebuild/resign; external GameCenter stays disabled
    ao.apply_lua(t, ao.LUA_WEBLINKS, 'external web-link buttons removed (dead offline)')
    ao.assert_tier_tables(t)
    ao.apply_lua(t, ao.LUA_OFFLINE_IAP, 'local offline in-app purchase approval (owner request)')
    paths = ao.remove_texture_defs(t, ao.TEX_CORE + ao.TEX_WEBLINKS, 'texture definitions for removed UI')
    removed = ao.remove_textures(t, paths, 'art used only by removed ad/social/web-link UI')
    ao.apply_configs(t, key)
    ao.rebuild_asset_manifest(t, key)
    errs = ao.validate(t, key, None)
    report = {'tool': 'aod_offline.py', 'profile': 'full', 'keep_weblinks': False, 'offline_iap': True,
              'apk_sha256': ao.APK_SHA256, 'libgame_sha256': ao.LIBGAME_SHA256,
              'luac_checked': False, 'changes': t.log, 'removed_texture_files': removed,
              'validation_errors': errs}
    with open(os.path.join(root, REPORT), 'w', newline='\n') as fo:
        json.dump(report, fo, indent=2)
    if errs:
        raise PrepError('offline profile validation failed: %s' % errs[:3])
    return len(t.log), len(removed)


def tree_files(root):
    """Relative '/'-separated paths of all regular files; refuses symlinks."""
    out = []
    for dirpath, dirnames, files in os.walk(root):
        for d in dirnames:
            if os.path.islink(os.path.join(dirpath, d)):
                raise PrepError('symlink in tree: %s' % os.path.join(dirpath, d))
        for fn in files:
            p = os.path.join(dirpath, fn)
            if os.path.islink(p) or not os.path.isfile(p):
                raise PrepError('not a regular file: %s' % p)
            out.append(os.path.relpath(p, root).replace(os.sep, '/'))
    return sorted(out)


def write_manifest(root):
    lines = ['%s  %s' % (sha256_file(os.path.join(root, *rel.split('/'))), rel)
             for rel in tree_files(root) if rel != MANIFEST]
    lines.sort(key=lambda l: l.split('  ', 1)[1])
    with open(os.path.join(root, MANIFEST), 'w', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')
    return len(lines)


def verify_tree(root):
    """The tree must equal the known-good offline tree: same file set, same bytes, same manifest."""
    want = load_hash_list(EXPECTED_OUTPUT)
    have = [r for r in tree_files(root) if r != MANIFEST]
    extra = sorted(set(have) - set(want))
    missing = sorted(set(want) - set(have))
    if extra or missing:
        raise PrepError('tree differs from the expected file set: extra %s missing %s' % (extra[:3], missing[:3]))
    bad = [r for r in have if sha256_file(os.path.join(root, *r.split('/'))) != want[r]]
    if bad:
        raise PrepError('%d file(s) differ from the expected output, e.g. %s' % (len(bad), bad[:3]))
    with open(os.path.join(root, MANIFEST), 'rb') as a, open(EXPECTED_OUTPUT, 'rb') as b:
        if a.read() != b.read():
            raise PrepError('MANIFEST.sha256 differs from expected/data-offline.sha256')
    if not os.path.isdir(os.path.join(root, 'files')):
        raise PrepError('files/ (save directory) missing from the tree')
    if os.listdir(os.path.join(root, 'files')):
        raise PrepError('files/ must be empty in a prepared tree (it would overwrite saves)')
    return len(have)


def prepare(apk, outdir):
    final = os.path.join(outdir, 'aodd')
    staging = os.path.join(outdir, 'aodd.partial')
    for p in (final, staging):
        if os.path.lexists(p):
            raise PrepError('%s already exists; choose an empty output directory' % p)
    os.makedirs(staging)
    try:
        mode, n = extract_inputs(apk, staging)
        print('APK verified (%s): %d files extracted' % (mode, n))
        changes, removed = run_offline_profile(staging)
        print('offline profile applied: %d changes, %d unused textures removed' % (changes, removed))
        os.makedirs(os.path.join(staging, 'files'))
        write_manifest(staging)
        count = verify_tree(staging)
    except BaseException:
        shutil.rmtree(staging, ignore_errors=True)
        raise
    os.rename(staging, final)
    print('OK: %s (%d files) matches the known-good data tree' % (final, count + 1))
    return final


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('apk', nargs='?', help='your Army of Darkness Defense 1.1.1 APK')
    ap.add_argument('outdir', nargs='?', help='output directory (OUT/aodd is created)')
    ap.add_argument('--check', metavar='AODD', help='only verify an already prepared aodd directory')
    a = ap.parse_args(argv)
    try:
        if a.check and not a.apk:
            n = verify_tree(a.check)
            print('OK: %s matches the known-good data tree (%d files + manifest)' % (a.check, n))
        elif a.apk and a.outdir and not a.check:
            prepare(a.apk, a.outdir)
        else:
            ap.error('give APK OUTDIR, or --check AODD')
    except PrepError as e:
        sys.stderr.write('error: %s\n' % e)
        return 2
    except SystemExit as e:           # aod_offline.die() reports its own FAIL line
        if e.code not in (0, None):
            sys.stderr.write('error: offline profile refused the input\n')
            return 2
        raise
    return 0


if __name__ == '__main__':
    sys.exit(main())
