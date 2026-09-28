#!/usr/bin/env python3
"""Bundle the exact sources of the VitaSDK libraries statically linked into eboot.bin.

    build_dep_sources.py --deps DIR --out aod-vita-1.0-dependency-sources.tar.gz

DIR (prepared once, with network access; see DEPENDENCY-SOURCES.md) must contain:
  dl/<file>             upstream release archives, as fetched from the URLs in the recipes
  <repo>/               git checkouts at the pinned commits (opensles, pthread-embedded, math-neon,
                        kubridge, libpng)
  vita-packages/        https://github.com/vitasdk/packages, whose recipes (VITABUILD + patches) built the
                        installed packages
Every input is checked against the sha256 / commit recorded below before anything is written. Output is
deterministic (sorted, fixed metadata, gzip mtime 0). No network access.
"""
import argparse
import gzip
import hashlib
import io
import os
import subprocess
import sys
import tarfile

ROOT = 'aod-vita-1.0-dependency-sources'
RECIPES_COMMIT = '771ad4363f357f65d6ecedae9de920e17cb959de'
# package -> recipe files; each VITABUILD's sha256 equals the pkgbuild_sha256sum in the installed package
RECIPES = {
    'vitaShaRK': ['VITABUILD'], 'SceShaccCgExt': ['VITABUILD'], 'opensles': ['VITABUILD'],
    'libsndfile': ['VITABUILD'], 'mpg123': ['VITABUILD', 'mpg123.patch'], 'lame': ['VITABUILD'],
    'libvorbis': ['VITABUILD'], 'libogg': ['VITABUILD'], 'flac': ['VITABUILD'], 'opus': ['VITABUILD'],
    'freetype': ['VITABUILD'], 'libpng': ['VITABUILD', 'libpng.patch'], 'zlib': ['VITABUILD', 'zlib-no-pic.diff'],
    'bzip2': ['VITABUILD', 'bzip2.pc'], 'kubridge': ['VITABUILD', 'vita-headers-886.diff'],
    'taihen': ['VITABUILD'], 'libmathneon': ['VITABUILD'],
}
ARCHIVES = {   # file in dl/ -> sha256 (equal to the recipe's sha256sums entry)
    'vitaShaRK-v.1.7.tar.gz': '08783ea390ddbe93ce15ff3648fce7df0026846662c1d81ce8c2cc06bcb754f1',
    'SceShaccCgExt-v1.0.1.tar.gz': 'a1cda9f3d637032cc597f2f4fdbc17813e5c7c90943a7d44c2e35f013c8dbc0b',
    'libsndfile-1.2.2.tar.xz': '3799ca9924d3125038880367bf1468e53a1b7e3686a934f098b7e1d286cdb80e',
    'mpg123-1.33.7.tar.bz2': '31d0e35a4ca567ec9b5ebda6c3062bb4435d6d3eacd6ef0d95cadd7854dc03ee',
    'lame-4.0.tar.gz': '3df5124d5ad3a98312ffd7ba6a9b36230e4f8a3e66d3ce0f425e336c32d216eb',
    'libvorbis-1.3.7.tar.gz': '0e982409a9c3fc82ee06e08205b1355e5c6aa4c36bca58146ef399621b0ce5ab',
    'libogg-1.3.6.tar.xz': '5c8253428e181840cd20d41f3ca16557a9cc04bad4a3d04cce84808677fa1061',
    'flac-1.3.4.tar.xz': '8ff0607e75a322dd7cd6ec48f4f225471404ae2730d0ea945127b1355155e737',
    'opus-1.6.1.tar.gz': '6ffcb593207be92584df15b32466ed64bbec99109f007c82205f0194572411a1',
    'freetype-2.14.3.tar.xz': '36bc4f1cc413335368ee656c42afca65c5a3987e8768cc28cf11ba775e785a5f',
    'zlib-1.3.2.tar.xz': 'd7a0654783a4da529d1bb793b7ad9c3318020af77667bcae35f95d0e42a792f3',
    'bzip2-1.0.8.tar.gz': 'ab5a03176ee106d3f0fa90e381da478ddae405918153cca248e682cd0c4a2269',
    'taihen.tar.gz': '6a7d4592b983292e8b8c53c624fa800e33ff69fe06c9d267cafc33d0ded4d2a9',
}
REPOS = {      # checkout dir -> pinned commit (from the recipe, or vitasdk-core version_info)
    'opensles': 'e35b0630ac4c9091db63374d5c111d719887dde9',
    'pthread-embedded': '11d2e5722d98c86f33c908fc47b2cf6e55205db5',
    'math-neon': '0faab814782c071ff4015527f1ca955ab1ccc470',
    'kubridge': '417ddde9a744eba98d769382c1c1372b8e119139',
    'libpng': '3061454d980de7d53608f594194cfac722721d2a',          # tag v1.6.58
}
LICENSE_FILES = {   # file in dl/ -> sha256; license texts not in the archives above (newlib 892f530, taiHEN v0.11,
                    # vita-headers 5e1e7d3; raw.githubusercontent.com at those revisions)
    'newlib-COPYING.NEWLIB': 'f3afe48e4bc6ed8466a42e9dacb6be1d8f9cbf5aac15cb8e474a5ccde8b40ef6', 'newlib-COPYING.LIBGLOSS': '4ef33c7bdd57f5fa8fbe3bdc0200245ab91c564c2ef530d95bbe7fd4070757cd', 'newlib-COPYING': '231f7edcc7352d7734a96eef0b8030f77982678c516876fcb81e25b32d68564c',
    'newlib-COPYING.LIB': 'a9bdde5616ecdd1e980b44f360600ee8783b1f99b8cc83a2beb163a0a390e861', 'newlib-COPYING3': '8ceb4b9ee5adedde47b31e975c1d90c73ad27b6b165a1dcd80c7c545eb65b903', 'newlib-COPYING3.LIB': 'a853c2ffec17057872340eee242ae4d96cbf2b520ae27d903e1b2fef1a5f9d1c',
    'taihen-LICENSE': 'b82d83d4c589bfba4c9ce82de4ba98d2b9186b5c59e6cb848bd7ef143b1af437', 'vita-headers-LICENSE.md': '57cfaa2b34577e7d1afe314dca63bfa4e7a3326080b59f21fd1c50048268c160',
}
MTIME = 1790000000   # fixed
MAX_BYTES = 256 << 20


def die(msg):
    sys.stderr.write('build_dep_sources: FAIL: %s\n' % msg)
    sys.exit(2)


def read(path):
    if os.path.getsize(path) > MAX_BYTES:
        die('%s too large' % path)
    with open(path, 'rb') as f:
        return f.read()


def git(repo, *args):
    r = subprocess.run(('git', '-C', repo) + args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if r.returncode:
        die('git -C %s %s: %s' % (repo, ' '.join(args), r.stderr.decode(errors='replace').strip()))
    return r.stdout


def collect(deps, manifest_md):
    files = [('DEPENDENCY-SOURCES.md', manifest_md)]
    for name, want in sorted(ARCHIVES.items()):
        data = read(os.path.join(deps, 'dl', name))
        if hashlib.sha256(data).hexdigest() != want:
            die('%s sha256 differs from the recipe' % name)
        files.append(('upstream/' + name, data))
    for repo, commit in sorted(REPOS.items()):
        path = os.path.join(deps, repo)
        if git(path, 'rev-parse', 'HEAD').decode().strip() != commit:
            die('%s is not at %s' % (repo, commit))
        files.append(('git/%s-%s.tar' % (repo, commit[:12]),
                      git(path, 'archive', '--format=tar', '--prefix=%s-%s/' % (repo, commit[:12]), commit)))
    vp = os.path.join(deps, 'vita-packages')
    if git(vp, 'rev-parse', 'HEAD').decode().strip() != RECIPES_COMMIT:
        die('vita-packages is not at %s' % RECIPES_COMMIT)
    for pkg, names in sorted(RECIPES.items()):
        for n in names:
            files.append(('recipes/%s/%s' % (pkg, n), git(vp, 'show', '%s:%s/%s' % (RECIPES_COMMIT, pkg, n))))
    for name in sorted(LICENSE_FILES):
        data = read(os.path.join(deps, 'dl', name))
        if hashlib.sha256(data).hexdigest() != LICENSE_FILES[name]:
            die('%s sha256 differs' % name)
        files.append(('licenses/' + name, data))
    return files


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--deps', required=True)
    ap.add_argument('--manifest', required=True, help='DEPENDENCY-SOURCES.md to include at the top')
    ap.add_argument('--out', required=True)
    a = ap.parse_args()
    if os.path.exists(a.out):
        die('%s exists' % a.out)
    files = collect(a.deps, read(a.manifest))
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode='w', format=tarfile.PAX_FORMAT) as tf:
        for name, data in sorted(files):
            ti = tarfile.TarInfo('%s/%s' % (ROOT, name))
            ti.size, ti.mtime, ti.mode, ti.uid, ti.gid, ti.uname, ti.gname = len(data), MTIME, 0o644, 0, 0, '', ''
            tf.addfile(ti, io.BytesIO(data))
    with open(a.out, 'wb') as f, gzip.GzipFile(filename='', mode='wb', fileobj=f, mtime=0, compresslevel=9) as gz:
        gz.write(buf.getvalue())
    print('%s: %d files' % (a.out, len(files)))


if __name__ == '__main__':
    main()
