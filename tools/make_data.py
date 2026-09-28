#!/usr/bin/env python3
"""
Build the ux0:data/aodd/ install tree for aod-vita from YOUR OWN copy of
Army of Darkness Defense 1.1.1 (com.backflipstudios.android.aodd, versionCode 7018).

The VPK contains no game data. This script extracts the two native libraries and the
assets/ tree from the APK, byte-identical (service configs and __asset_manifest.json carry
integrity headers that libgame verifies), and writes a MANIFEST.sha256 of the result.

    python3 make_data.py army-of-darkness-defense-1-1-1.apk out/
    -> out/aodd/{libgame.so, libfmodex.so, assets/..., MANIFEST.sha256}
    copy out/aodd to ux0:data/aodd on the Vita.

For the released port use release/prepare_data.py instead: it applies the offline profile and
checks the complete result against the known-good tree. This script is the older developer path; it
can also produce the unmodified data (no profile) for comparison. Its --offline-profile hook runs
release/offline/aod_offline.py, e.g.
    --offline-profile "python3 release/offline/aod_offline.py --apk YOUR.apk --in-place"
"""
import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import zipfile

APK_SHA256 = "4800eac5d52807a7c0d0b4f3e41489a1ce68de7baa0d50b8aa79c4207ebf6bf4"
LIBS = {
    "lib/armeabi-v7a/libgame.so": ("libgame.so", "d4d6632d5c3d7453655daa4dd823a39c9f84129885360308f99b4553fd92c41d"),
    "lib/armeabi-v7a/libfmodex.so": ("libfmodex.so", "94b25d7969402da2802bfa32806d65fbb2b289969c464e8507c067140ef54bb4"),
}


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("apk")
    ap.add_argument("outdir")
    ap.add_argument("--force", action="store_true", help="accept an APK with a different hash (untested)")
    ap.add_argument("--offline-profile", metavar="CMD",
                    help="command that transforms OUT/aodd in place (ablation tool); receives the tree path as its last argument")
    args = ap.parse_args()

    digest = sha256_file(args.apk)
    if digest != APK_SHA256:
        msg = f"APK sha256 {digest} != expected {APK_SHA256} (Army of Darkness Defense 1.1.1)"
        if not args.force:
            sys.exit("error: " + msg + "; use --force to try anyway")
        print("warning: " + msg)

    root = os.path.join(args.outdir, "aodd")
    if os.path.exists(root):
        sys.exit(f"error: {root} already exists; remove it first")
    os.makedirs(root)

    with zipfile.ZipFile(args.apk) as z:
        for member, (name, want) in LIBS.items():
            data = z.read(member)
            got = hashlib.sha256(data).hexdigest()
            if got != want and not args.force:
                sys.exit(f"error: {member} sha256 {got} != {want}")
            with open(os.path.join(root, name), "wb") as f:
                f.write(data)
        n = 0
        for info in z.infolist():
            if not info.filename.startswith("assets/") or info.is_dir():
                continue
            dest = os.path.join(root, info.filename)
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            with z.open(info) as src, open(dest, "wb") as dst:
                shutil.copyfileobj(src, dst)
            n += 1
    print(f"extracted 2 libraries and {n} asset files")

    if args.offline_profile:
        cmd = args.offline_profile.split() + [root]
        print("applying offline profile:", " ".join(cmd))
        subprocess.run(cmd, check=True)

    os.makedirs(os.path.join(root, "files"), exist_ok=True)
    lines = []
    for dirpath, _, files in os.walk(root):
        for fn in sorted(files):
            p = os.path.join(dirpath, fn)
            rel = os.path.relpath(p, root)
            if rel == "MANIFEST.sha256":
                continue
            lines.append(f"{sha256_file(p)}  {rel}")
    lines.sort(key=lambda l: l.split("  ", 1)[1])
    with open(os.path.join(root, "MANIFEST.sha256"), "w") as f:
        f.write("\n".join(lines) + "\n")
    print(f"wrote {root} ({len(lines)} files); copy it to ux0:data/aodd/")


if __name__ == "__main__":
    main()
