#!/usr/bin/env python3
"""Assemble the release files from a committed source tree and an already built VPK (runtime validation is described in the release notes).

    build_release.py --repo SRC --vpk TESTED.vpk --expect-eboot SHA256 --cover COVER.png --out DIR

Writes, deterministically (fixed timestamps, sorted entries):
  DIR/public/aod_vita.vpk                 byte-identical copy of TESTED.vpk (its eboot hash is checked)
  DIR/public/aodd-prepare-kit-1.1.zip     prepare_data.py + offline profile + expected hashes + tests
  DIR/public/aod-vita-1.1-source.tar.gz   clean source snapshot: HEAD + submodules, no git history
  DIR/public/aod-vita-1.1-dependency-sources.tar.gz   exact sources of the linked VitaSDK libraries
  DIR/public/README.md, CHANGELOG.md, LICENSE.txt, THIRD_PARTY_NOTICES.md, LICENSES/, cover.png
  DIR/public/SHA256SUMS
  DIR/clean-repo/                         one-commit repository of the snapshot (author PiSCES), tag v1.1.0
Nothing is uploaded or pushed. DIR must not exist.
"""
import argparse
import gzip
import hashlib
import io
import os
import re
import shutil
import subprocess
import sys
import tarfile
import zipfile
import zlib

VERSION = '1.1'
TAG = 'v1.1.0'
AUTHOR = 'PiSCES'
AUTHOR_EMAIL = 'piranesi.ai@outlook.com'
COVER_SHA256 = '621762b2f19646719e4a78b2e03f80ed99bd6303ba39c1b43c326303e39c91c7'
SNAPSHOT_EXCLUDE = ('lib/vitagl/samples/',)       # upstream demo media, not part of the build
KIT_DIRS = ('release/offline/', 'release/expected/', 'release/tests/', 'release/trophies/')
KIT_FILES = ('release/prepare_data.py',)
PUBLIC_DOCS = (('README.md', 'README.md'), ('CHANGELOG.md', 'CHANGELOG.md'), ('LICENSE', 'LICENSE.txt'),
               ('THIRD_PARTY_NOTICES.md', 'THIRD_PARTY_NOTICES.md'))
FORBIDDEN_NAME = re.compile(r'(\.apk|\.so|\.psp2dmp|aod_log\.txt|\.ogg|\.mp3)$|(^|/)(files|saves?)/', re.I)
GENERIC_PRIVATE = [rb'/home/[a-z]', rb'/Users/[A-Za-z]', rb'[A-Z]:[\\/]+Users[\\/]']   # user home paths
PRIVATE_TEXT = None                                   # set in main(): generic + --private-patterns file
MAX_FILES = 20000


def die(msg):
    sys.stderr.write('build_release: FAIL: %s\n' % msg)
    sys.exit(2)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def git(repo, *args):
    r = subprocess.run(('git', '-C', repo) + args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if r.returncode:
        die('git %s: %s' % (' '.join(args), r.stderr.decode(errors='replace').strip()))
    return r.stdout


def tree_members(repo, prefix=''):
    """(path, mode, bytes) of every file in HEAD of repo (and recursively its submodules)."""
    out = []
    raw = git(repo, 'ls-tree', '-r', '-z', '--full-tree', 'HEAD')
    for rec in raw.split(b'\0'):
        if not rec:
            continue
        meta, path = rec.split(b'\t', 1)
        mode, kind, obj = meta.decode().split()
        path = path.decode()
        if kind == 'commit':
            sub = os.path.join(repo, path)
            if git(sub, 'rev-parse', 'HEAD').decode().strip() != obj:
                die('submodule %s is not at the committed %s' % (path, obj))
            out.extend(tree_members(sub, prefix + path + '/'))
        elif kind == 'blob':
            if mode == '120000':
                die('symlink in source: %s%s' % (prefix, path))
            out.append((prefix + path, mode, git(repo, 'cat-file', 'blob', obj)))
        if len(out) > MAX_FILES:
            die('too many files')
    return out


def select_snapshot(members):
    keep = [m for m in members if not m[0].startswith(SNAPSHOT_EXCLUDE)]
    own = [m for m in keep if not m[0].startswith(('lib/vitagl/', 'lib/so_util/', 'lib/falso_jni/', 'lib/falso_ndk/'))]
    for path, _, data in keep:
        if FORBIDDEN_NAME.search(path):
            die('forbidden file in snapshot: %s' % path)
    for path, _, data in own:
        if PRIVATE_TEXT.search(data):
            die('private text in snapshot file %s: %r' % (path, PRIVATE_TEXT.search(data).group(0)))
    return sorted(keep)


def write_tar(members, dest, root, mtime):
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode='w', format=tarfile.PAX_FORMAT) as tf:
        for path, mode, data in members:
            ti = tarfile.TarInfo('%s/%s' % (root, path))
            ti.size, ti.mtime, ti.uid, ti.gid, ti.uname, ti.gname = len(data), mtime, 0, 0, '', ''
            ti.mode = 0o755 if mode == '100755' else 0o644
            tf.addfile(ti, io.BytesIO(data))
    with open(dest, 'wb') as f, gzip.GzipFile(filename='', mode='wb', fileobj=f, mtime=0, compresslevel=9) as gz:
        gz.write(buf.getvalue())


def write_zip(entries, dest):
    """entries: (name, bytes, executable). Fixed 1980 timestamps, sorted, deflated."""
    with zipfile.ZipFile(dest, 'w', zipfile.ZIP_DEFLATED) as z:
        for name, data, exe in sorted(entries):
            zi = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            zi.compress_type = zipfile.ZIP_DEFLATED
            zi.external_attr = ((0o755 if exe else 0o644) | 0o100000) << 16
            z.writestr(zi, data)


KIT_README = """Army of Darkness Defense for PS Vita %s: data preparation kit
=============================================================

This kit contains NO game data. It turns YOUR OWN Army of Darkness Defense 1.1.1 Android APK into the
ux0:data/aodd/ folder the Vita port needs. It runs offline on Windows, macOS or Linux with Python 3.8+.

    python3 prepare_data.py path/to/your.apk out
    -> out/aodd/  (copy its contents to ux0:data/aodd/ on the Vita; keep your files/ save folder)

Contents:
  prepare_data.py            the tool (verifies the APK, extracts, applies the offline profile, checks)
  offline/                   the port's offline profile: script/config edits applied to your copy,
                             original replacement scripts (patches/lua), documentation, provenance hashes
  expected/                  sha256 lists: the 1.1.1 files it needs, and the expected result
  trophies/                  clean homebrew trophy pack generator, frozen mapping and metadata
  LICENSES/                  license for the adapted trophy pack writer
  tests/                     self-tests: AODD_APK=your.apk python3 -m unittest discover -s tests -v
  LICENSE.txt                MIT (this kit is part of the port's source)

See README.md of the release for installation, controls and credits.
""" % VERSION


def build_kit(members, dest):
    entries = []
    for path, mode, data in members:
        if path in KIT_FILES or path.startswith(KIT_DIRS):
            entries.append((path[len('release/'):], data, mode == '100755' or path.endswith('prepare_data.py')))
    if not any(n == 'prepare_data.py' for n, _, _ in entries):
        die('prepare_data.py missing from the kit')
    lic = [d for p, _, d in members if p == 'LICENSE'][0]
    entries += [('README.txt', KIT_README.encode(), False), ('LICENSE.txt', lic, False)]
    trophy_licenses = [(p, d, False) for p, _, d in members if p == 'LICENSES/GoldenBalloon-MIT.txt']
    if not trophy_licenses:
        die('Golden Balloon trophy pack writer license missing from the kit')
    entries += trophy_licenses
    write_zip(entries, dest)
    return len(entries)


def self_payload(eboot):
    """The eboot (SELF) stores its segments zlib-compressed; return them decompressed so a string scan
    cannot be defeated by compression. Bounded: at most 64 streams and 64 MiB."""
    out, i, total = [], 0, 0
    while i < len(eboot) - 2 and len(out) < 64:
        if eboot[i] == 0x78 and eboot[i + 1] in (0x01, 0x5e, 0x9c, 0xda):
            z = zlib.decompressobj()
            try:
                x = z.decompress(eboot[i:], 64 << 20)
            except zlib.error:
                x = b''
            if len(x) > 1000:
                out.append(x)
                total += len(x)
                i = len(eboot) - len(z.unused_data)
                continue
        i += 1
    return b''.join(out), len(out)


def check_vpk(vpk, expect_eboot):
    with zipfile.ZipFile(vpk) as z:
        names = z.namelist()
        if z.testzip() is not None:
            die('VPK CRC error')
        eboot = z.read('eboot.bin')
        if sha256(eboot) != expect_eboot:
            die('VPK eboot.bin is not the expected build %s' % expect_eboot)
        payload, streams = self_payload(eboot)
        hits = sorted(set(m.group(0)[:80] for m in PRIVATE_TEXT.finditer(payload + eboot)))
        print('eboot: %d compressed segment(s), %d bytes decompressed; private-pattern hits: %d'
              % (streams, len(payload), len(hits)))
        for h in hits:
            print('  WARNING: eboot contains %r' % h)
        bad = [n for n in names if FORBIDDEN_NAME.search(n) or n.startswith(('assets/', 'lib'))]
        if bad:
            die('VPK contains game or private files: %s' % bad)
    return names


def clean_repo(members, dest, date):
    """One-commit repository: the port's own files; submodules as gitlinks at the pinned commits."""
    os.makedirs(dest)
    run = lambda *a, env=None: subprocess.run(('git', '-C', dest) + a, check=True, env=env,
                                              stdout=subprocess.DEVNULL)
    run('init', '-q', '-b', 'main')
    subs = {}
    for path, mode, data in members:
        top = path.split('/')[1] if path.startswith('lib/') and path.count('/') >= 2 else None
        if top in ('vitagl', 'so_util', 'falso_jni', 'falso_ndk'):
            continue
        p = os.path.join(dest, *path.split('/'))
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, 'wb') as f:
            f.write(data)
        if mode == '100755':
            os.chmod(p, 0o755)
    run('add', '-A')
    for name, sha in SUBMODULES.items():
        run('update-index', '--add', '--cacheinfo', '160000,%s,lib/%s' % (sha, name))
    env = dict(os.environ, GIT_AUTHOR_NAME=AUTHOR, GIT_AUTHOR_EMAIL=AUTHOR_EMAIL, GIT_COMMITTER_NAME=AUTHOR,
               GIT_COMMITTER_EMAIL=AUTHOR_EMAIL, GIT_AUTHOR_DATE=date, GIT_COMMITTER_DATE=date)
    run('-c', 'commit.gpgsign=false', 'commit', '-q', '-m',
        'Army of Darkness Defense for PS Vita %s' % VERSION, env=env)
    run('-c', 'tag.gpgsign=false', 'tag', TAG, env=env)


SUBMODULES = {}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--repo', required=True)
    ap.add_argument('--vpk', required=True)
    ap.add_argument('--expect-eboot', required=True)
    ap.add_argument('--cover', required=True)
    ap.add_argument('--dep-sources', required=True, help='output of build_dep_sources.py (copied verbatim)')
    ap.add_argument('--out', required=True)
    ap.add_argument('--private-patterns', help='file of extra regexes (one per line) that must not occur in '
                    "the port's own files; keep it outside the repository")
    a = ap.parse_args()
    global PRIVATE_TEXT
    pats = list(GENERIC_PRIVATE)
    if a.private_patterns:
        with open(a.private_patterns, 'rb') as f:
            pats += [l.strip() for l in f.read().splitlines() if l.strip() and not l.startswith(b'#')]
    PRIVATE_TEXT = re.compile(b'|'.join(b'(?:%s)' % p for p in pats), re.I)
    if os.path.exists(a.out):
        die('%s exists' % a.out)
    if git(a.repo, 'status', '--porcelain', '--untracked-files=no').strip():
        die('source tree has uncommitted changes')
    with open(a.cover, 'rb') as f:
        cover = f.read()
    if sha256(cover) != COVER_SHA256:
        die('cover image is not the supplied original')
    for line in git(a.repo, 'submodule', 'status').decode().splitlines():
        sha, path = line.strip().lstrip('+-U').split()[:2]
        SUBMODULES[path.split('/', 1)[1]] = sha
    date = git(a.repo, 'log', '-1', '--format=%cI').decode().strip()
    mtime = int(git(a.repo, 'log', '-1', '--format=%ct').decode().strip())
    head = git(a.repo, 'rev-parse', 'HEAD').decode().strip()

    members = tree_members(a.repo)
    snap = select_snapshot(members)
    vpk_names = check_vpk(a.vpk, a.expect_eboot)
    pub = os.path.join(a.out, 'public')
    os.makedirs(os.path.join(pub, 'LICENSES'))
    shutil.copyfile(a.vpk, os.path.join(pub, 'aod_vita.vpk'))
    shutil.copyfile(a.dep_sources, os.path.join(pub, 'aod-vita-%s-dependency-sources.tar.gz' % VERSION))
    write_tar(snap, os.path.join(pub, 'aod-vita-%s-source.tar.gz' % VERSION), 'aod-vita-%s' % VERSION, mtime)
    nkit = build_kit(members, os.path.join(pub, 'aodd-prepare-kit-%s.zip' % VERSION))
    byname = {p: d for p, _, d in members}
    for src, dst in PUBLIC_DOCS:
        with open(os.path.join(pub, dst), 'wb') as f:
            f.write(byname[src])
    for p in sorted(byname):
        if p.startswith('LICENSES/'):
            with open(os.path.join(pub, 'LICENSES', p.split('/', 1)[1]), 'wb') as f:
                f.write(byname[p])
    with open(os.path.join(pub, 'cover.png'), 'wb') as f:
        f.write(cover)
    sums = []
    for dp, _, fs in os.walk(pub):
        for fn in fs:
            full = os.path.join(dp, fn)
            with open(full, 'rb') as f:
                sums.append('%s  %s' % (sha256(f.read()), os.path.relpath(full, pub).replace(os.sep, '/')))
    sums.sort(key=lambda l: l.split('  ', 1)[1])
    with open(os.path.join(pub, 'SHA256SUMS'), 'w', newline='\n') as f:
        f.write('\n'.join(sums) + '\n')
    clean_repo(snap, os.path.join(a.out, 'clean-repo', 'army-of-darkness-defense-vita'), date)
    print('source HEAD %s; snapshot %d files; kit %d files; VPK %d members (eboot verified); %d public files'
          % (head, len(snap), nkit, len(vpk_names), len(sums)))


if __name__ == '__main__':
    main()
