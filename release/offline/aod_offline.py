#!/usr/bin/env python3
"""AoD Defense 1.1.1 -> Vita: OFFLINE PROFILE asset transformer (ads / social / telemetry ablation).

Reads the immutable source APK and the user's own libgame.so, asserts their hashes, and writes a
prepared `assets/` tree for the Vita port with ad, social and analytics tie-ins removed.

Integrity: serviceconfigs/*.json and __asset_manifest.json carry a 32-byte header
SHA-256(K64 || body). K64 is read from libgame.so (vaddr 0x8cc134) at run time and is never
stored in this repository; only its SHA-256 fingerprint is. Every signed file in the output is
re-verified before the tool reports success.

Usage:
  aod_offline.py --apk APK --libgame libgame.so --out DIR [--profile full|lua-only]
                 [--keep-weblinks] [--no-offline-iap] [--luac PATH]
  aod_offline.py --apk APK --in-place TREE      (make_data.py hook: TREE = out/aodd with
                 libgame.so + assets/ extracted byte-identical; verified against the APK first)
"""
import argparse, hashlib, json, os, re, shutil, subprocess, sys, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, 'tools'))
from archondb import Manifest  # noqa: E402

APK_SHA256 = '4800eac5d52807a7c0d0b4f3e41489a1ce68de7baa0d50b8aa79c4207ebf6bf4'
LIBGAME_SHA256 = 'd4d6632d5c3d7453655daa4dd823a39c9f84129885360308f99b4553fd92c41d'
KEY_FILE_OFFSET = 0x8cb134          # vaddr 0x8cc134 in .data (vaddr - 0x1000)
KEY_SHA256 = '73138e71e98ca0e3f85501ae0875faed47f7c34b768fcee144d94616a73b84b5'
HDR = 32

SC = 'serviceconfigs/'
SCRIPTS = 'scripts.archondb/'
ASSETSDB = 'assets.archondb/'

# ---------------------------------------------------------------- input assertions (sha256 of the
# unmodified file inside the APK). A mismatch aborts before anything is written.
INPUT_SHA256 = {
    SCRIPTS + 'analytics-jf5g3fy62k4nl7iw2icj5pvma5hf6ik7.lua': '4a899ccb5260d23b5096fc910ef55059b83cb7f60c3f3c330f2c23c5ec4a4314',
    SCRIPTS + 'incent_ads-gesdllc6ybpp32xfx26a4eznhhupvvck.lua': '4294a5842928542932f2fba4bd31fbbd0190098a80c7a151070739e0a71e8dec',
    SCRIPTS + 'engine-fya2qupsarwve6chaepukyvmowbm4t6e.lua': 'a179a3425462f749592745f2ca37438cf3056f084d5a06874810c0ff9c523375',
    SCRIPTS + 'mainmenuwindow-hg5xjdrimb6aotnan7336utzxnrfnwsz.lua': '939a34001a8806582202821876ceb0166fd734de49cc660dceaf6aaa95bd0da6',
    SCRIPTS + 'optionsmenuwindow-wgxm2cwzsmucl4h55c3yntp4gtfm5qfr.lua': 'eb6a374100e0804bec76b904464e4bf61d977fb199f3be9ee9063c1de8230765',
    SCRIPTS + 'achievements-3k7cwojlwz3adpq2a5wlgkff4e4vkvis.lua': '536dbe46d50cc66843efc8b0dbb37cca0ceb2edd05e3c0028a51be8687248287',
    SCRIPTS + 'maingame-qxi26tivjkefeflif7nha2awb2eyvl7m.lua': 'cea89328d71b97a3ef8a5639546397aa1fc68bcbed5c41c47110271a368530fa',
    SCRIPTS + 'globals-qkjgbvmvahhiiwj5ogycgsvtg7ebksch.lua': 'd076f24ae360f1729d616182886f09d304acd753386f095fab6b49d91d7648a1',
    SCRIPTS + 'upgradeunitmenuwindow-hg2aqkp3umufnaw2arhdb476bzdfguhg.lua': '02beaefc70c2e873f516df4327e33476cda2ccdd7391e295965b79d20dcd3c3d',
    SCRIPTS + 'assets-tnyd2ylr7msjnj5pwxz5dgp5lahyhgod.lua': '29354d937408c12319bfb5f1ce5ab9d6520e357e453318ceda42b647b104a6ed',
    SC + 'ads.json': '89e9122d4332fde6ca913d68e8394966df142c1cae29d0dd862fb523019e094b',
    SC + 'analytics.json': 'd610ed2bf5a29d449b755e79cbb498bfd8d7f025832dc0dbbc5831a13f261173',
    SC + 'applovin.json': '4075c82cca27cafe7d93b88e062160d1982f109e1f607192f0acc7cf675243e9',
    SC + 'flurry.json': 'e7d35ff57c00452f75f5a9b2f53f36c624ac03c362b52ac23dba7860e18a19d7',
    SC + 'gamecenter.json': '0d5daf158c632adfa1f3b06a56968e78c24cffdf0425b6d9f870e3dfec630b1d',
    SC + 'google_conversion_tracking.json': '91898a6e2ea726a3174bb8cfca991fa3a517c3825ef83599b2bb5effa23f131a',
    SC + 'google_video_ads.json': 'bc3900b69506c21a5a2d0ff29e142230e84b0a7b83a8c1470a531c04b0e0fbe8',
    SC + 'hockeyapp.json': '0bfba3c841327d6ad03b5a32083176970615d98af560eeea367ce8a2d18670e8',
    SC + 'localytics.json': '353525d46d621b646e2a0c322ca5fd5fc0bf121b97ac5d61c540c61663850b1e',
    SC + 'overmind.json': 'fab6406f4a5af4201bc7ece4912cc7810f94f307ce50e5914238b86cb3e6217b',
    SCRIPTS + 'coinstore-cdzrszrqzhyuifgcbh3xptld2a5xkfzm.lua': '138abdc0ad1dae538af0ca3abbf98a3326cae2ad38cea879afd9a54309556ac5',
    SCRIPTS + 'iap-uniaph4b6iitlz6rpgzema35dxlsygxw.lua': 'e5a9fbbf06f3639380a0b5a34d239522200efde2c390523d0e4249659c43ec78',
    '__asset_manifest.json': 'c52416d5e6e7d383bebdbc349bca4883315d17ddf20a62d4dfc41f5e13ab89a1',
    ASSETSDB + 'manifest.db': '398862675c94df1d73f48ec24b21f2dc75b8ab6114aadad1b71b8e33816fe1dc',
}

TAG = '[aod-offline]'

# ---------------------------------------------------------------- Lua edits.
# ('file', path_to_replacement)                   whole-module replacement, same public API
# ('sub', old, new)                                exact substring, must occur exactly once
# ('region', start, end, new, include_end)         start..end (end inclusive/exclusive), unique
LUA_CORE = {
    SCRIPTS + 'analytics-jf5g3fy62k4nl7iw2icj5pvma5hf6ik7.lua': [
        ('file', 'patches/lua/analytics.lua')],
    SCRIPTS + 'incent_ads-gesdllc6ybpp32xfx26a4eznhhupvvck.lua': [
        ('file', 'patches/lua/incent_ads.lua')],
    SCRIPTS + 'engine-fya2qupsarwve6chaepukyvmowbm4t6e.lua': [
        ('sub', "        local gc = GameCenterService.get()\n        if gc then\n            local gc_task = function()",
                "        local gc = nil -- %s GameCenter/Play Games login task removed\n        if gc then\n            local gc_task = function()" % TAG),
        ('sub', "    if notifService and game_service then\n",
                "    if false then -- %s incent-ad and re-engagement local notifications removed\n" % TAG)],
    SCRIPTS + 'mainmenuwindow-hg5xjdrimb6aotnan7336utzxnrfnwsz.lua': [
        ('sub', "    local gc =  GameCenterService.get()\n",
                "    local gc = nil -- %s leaderboards/achievements buttons removed\n" % TAG),
        ('sub', "    if( not ui_handle ) then\n        print(\"Running app update check task\")",
                "    if false then -- %s publisher-backend app-update/store prompt removed\n        print(\"Running app update check task\")" % TAG)],
    SCRIPTS + 'achievements-3k7cwojlwz3adpq2a5wlgkff4e4vkvis.lua': [
        ('sub', "    local gc = GameCenterService.get()\n    if gc then\n        gc:reportCompletion",
                "    local gc = nil -- %s no GameCenter reporting; local progress counters unchanged\n    if gc then\n        gc:reportCompletion" % TAG)],
    SCRIPTS + 'maingame-qxi26tivjkefeflif7nha2awb2eyvl7m.lua': [
        ('sub', "\tlocal gc = GameCenterService.get()\n\tif gc then\n\t\tgc:postScore",
                "\tlocal gc = nil -- %s no leaderboard score posting\n\tif gc then\n\t\tgc:postScore" % TAG)],
    SCRIPTS + 'globals-qkjgbvmvahhiiwj5ogycgsvtg7ebksch.lua': [
        ('sub', "ratings_dialog_levels = { 14, 29, 44 }\n",
                "ratings_dialog_levels = {} -- %s store-rating prompt removed (was { 14, 29, 44 })\n" % TAG)],
    SCRIPTS + 'upgradeunitmenuwindow-hg2aqkp3umufnaw2arhdb476bzdfguhg.lua': [
        ('sub', "    if ( ( not self.m_is_watching_ad ) and game_service:shouldDisplayIncentAd() ) then \n",
                "    if false then -- %s 'free coins' video-ad button removed (no reward substitution)\n" % TAG)],
}

LUA_WEBLINKS = {
    SCRIPTS + 'mainmenuwindow-hg5xjdrimb6aotnan7336utzxnrfnwsz.lua': [
        ('region', "        privacy_button = util.createButtonWithTexture(window, assets.textures.privacy_button)\n",
                   "    sprite.location = pool.get_v3f(0, 0, 1)\n",
                   "        -- %s privacy-policy web link removed (external URL, dead offline).\n"
                   "        -- Kept only as an invisible, non-interactive layout anchor for the buttons above it.\n"
                   "        privacy_button = util.createWindowWithTexture(window, assets.textures.privacy_button)\n"
                   "        privacy_button.color = pool.get_colorf(0, 0, 0, 0)\n"
                   "        privacy_button.location = pool.get_v3f(0.0, 0.0, 200.0)\n"
                   "\n"
                   "        util.snapBottom(privacy_button, world_size, 5)\n"
                   "        util.snapRight(privacy_button, world_size, 7)\n"
                   "    end\n" % TAG, True)],
    SCRIPTS + 'optionsmenuwindow-wgxm2cwzsmucl4h55c3yntp4gtfm5qfr.lua': [
        ('region', "    -- Legal Button\n", "    local text_button_controls = {}",
                   "    -- %s 'Legal' web view (publisher backend URL) removed; dead offline.\n\n" % TAG, False)],
}

# Local offline in-app purchases (explicit owner request): coin-store buttons are approved locally
# and fulfilled with the normal globals.purchases amounts; Google billing is never called.
LUA_OFFLINE_IAP = {
    SCRIPTS + 'iap-uniaph4b6iitlz6rpgzema35dxlsygxw.lua': [
        ('append', 'patches/lua/iap_offline_append.lua')],
    SCRIPTS + 'coinstore-cdzrszrqzhyuifgcbh3xptld2a5xkfzm.lua': [
        ('sub', "\t\t\tgame_service:purchaseProduct(id, iap_success_callback, iap_failure_callback)\n",
                "\t\t\tchunks.iap.offlinePurchase(id, self.owner, iap_success_callback, iap_failure_callback) -- %s local approval; Google billing never called\n" % TAG),
        ('sub', "\t\tlocal price = chunks.iap.getPriceString(id)\n",
                "\t\tlocal price = \"FREE\" -- %s offline offer: no store price shown, no payment\n" % TAG)],
    SCRIPTS + 'mainmenuwindow-hg5xjdrimb6aotnan7336utzxnrfnwsz.lua': [
        ('sub', "    if not has_seen_iap_warning then\n",
                "    if false then -- %s real-money purchase warning removed: offline offers cost nothing\n" % TAG)],
    SCRIPTS + 'optionsmenuwindow-wgxm2cwzsmucl4h55c3yntp4gtfm5qfr.lua': [
        ('region', "    do -- Restore Purchases Button\n",
                   "            table.insert(text_button_controls, restore_text)\n        end        \n    end\n",
                   "    -- %s 'Restore Purchases' removed: no store account offline; all offline items are consumable gold already in the save.\n" % TAG,
                   True)],
}
TIER_RE = r'\{\s*id\s*=\s*"(gold_tier\d)",\s*gold\s*=\s*(\d+)\s*\}'

# assets.lua texture definitions whose only users are removed UI
TEX_CORE = ['more_games_button', 'more_games_button_locale', 'leaderboards_button', 'achievements_button',
            'watch_ad_button', 'incent_ad_background', 'incent_ad_yes', 'incent_ad_no', 'video_icon']
TEX_WEBLINKS = ['privacy_button_text', 'legal_button']
ASSETS_LUA = SCRIPTS + 'assets-tnyd2ylr7msjnj5pwxz5dgp5lahyhgod.lua'

# ---------------------------------------------------------------- service configs
DELETE_CONFIGS = ['ads.json', 'applovin.json', 'google_video_ads.json',          # ad networks
                  'analytics.json', 'flurry.json', 'localytics.json',            # analytics
                  'google_conversion_tracking.json',                              # install tracking
                  'hockeyapp.json']                                               # crash telemetry
OFFLINE_SERVER = 'https://offline.invalid'   # RFC 2606 reserved TLD: never resolves


def sha(b):
    return hashlib.sha256(b).hexdigest()


def die(msg):
    sys.stderr.write('aod_offline: FAIL: %s\n' % msg)
    sys.exit(2)


def read_key(libgame):
    data = open(libgame, 'rb').read()
    if sha(data) != LIBGAME_SHA256:
        die('libgame.so sha256 %s != expected %s' % (sha(data), LIBGAME_SHA256))
    key = data[KEY_FILE_OFFSET:KEY_FILE_OFFSET + 64]
    if sha(key) != KEY_SHA256:
        die('integrity key fingerprint mismatch')
    return key


def verify_signed(key, blob):
    return len(blob) > HDR and hashlib.sha256(key + blob[HDR:]).digest() == blob[:HDR]


def sign(key, body):
    return hashlib.sha256(key + body).digest() + body


class Tree:
    def __init__(self, root):
        self.root = root
        self.log = []

    def p(self, rel):
        return os.path.join(self.root, rel)

    def read(self, rel):
        return open(self.p(rel), 'rb').read()

    def write(self, rel, data, why):
        before = sha(self.read(rel))
        open(self.p(rel), 'wb').write(data)
        self.log.append({'action': 'modify', 'path': 'assets/' + rel, 'sha256_before': before,
                         'sha256_after': sha(data), 'why': why})

    def delete(self, rel, why):
        before = sha(self.read(rel))
        os.remove(self.p(rel))
        self.log.append({'action': 'delete', 'path': 'assets/' + rel, 'sha256_before': before, 'why': why})


def adopt_in_place(apk, tree):
    """Use an already-extracted install tree; every assets/ file must equal the APK member."""
    data = open(apk, 'rb').read()
    if sha(data) != APK_SHA256:
        die('APK sha256 %s != expected %s' % (sha(data), APK_SHA256))
    root = os.path.join(tree, 'assets')
    if not os.path.isdir(root):
        die('%s has no assets/ directory' % tree)
    if os.path.exists(os.path.join(tree, 'ablation-report.json')):
        die('%s was already transformed (ablation-report.json present)' % tree)
    with zipfile.ZipFile(apk) as z:
        members = {n[len('assets/'):] for n in z.namelist() if n.startswith('assets/') and not n.endswith('/')}
        on_disk = set()
        for dp, _, fs in os.walk(root):
            for f in fs:
                on_disk.add(os.path.relpath(os.path.join(dp, f), root).replace(os.sep, '/'))
        if members != on_disk:
            die('tree assets/ differs from APK file set: extra %s missing %s' %
                (sorted(on_disk - members)[:5], sorted(members - on_disk)[:5]))
        for n in sorted(members):
            with open(os.path.join(root, n), 'rb') as fh:
                if fh.read() != z.read('assets/' + n):
                    die('tree file differs from APK: assets/' + n)
    return Tree(root)


def extract(apk, out):
    data = open(apk, 'rb').read()
    if sha(data) != APK_SHA256:
        die('APK sha256 %s != expected %s' % (sha(data), APK_SHA256))
    if os.path.exists(out):
        die('output %s already exists; refusing to overwrite' % out)
    root = os.path.join(out, 'assets')
    with zipfile.ZipFile(apk) as z:
        for n in z.namelist():
            if n.startswith('assets/') and not n.endswith('/'):
                dst = os.path.join(out, n)
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                with z.open(n) as src, open(dst, 'wb') as fo:
                    shutil.copyfileobj(src, fo)
    return Tree(root)


def listing_gaps(t):
    """Files on disk that the signed directory listing does not mention, per directory."""
    gaps = set()
    for e in json_body(t, '__asset_manifest.json')['contents']:
        d = t.p(e['path']) if e['path'] else t.root
        listed = {f['name'] for f in e['files']}
        for f in os.listdir(d):
            if os.path.isfile(os.path.join(d, f)) and f not in listed and not (e['path'] == '' and f == '__asset_manifest.json'):
                gaps.add(os.path.join(e['path'], f))
    return gaps


def assert_inputs(t, key):
    for rel, want in INPUT_SHA256.items():
        got = sha(t.read(rel))
        if got != want:
            die('input %s sha256 %s != expected %s' % (rel, got, want))
    for rel in signed_files(t):
        if not verify_signed(key, t.read(rel)):
            die('source signature does not verify: %s (wrong key/APK?)' % rel)
    t.baseline_gaps = listing_gaps(t)   # source: configuration.json (read by Java only) is unlisted
    if t.baseline_gaps != {'configuration.json'}:
        die('unexpected unlisted files in source: %s' % sorted(t.baseline_gaps))


def signed_files(t):
    out = ['__asset_manifest.json']
    for dp, _, fs in os.walk(t.p('serviceconfigs')):
        for f in fs:
            out.append(os.path.relpath(os.path.join(dp, f), t.root))
    return sorted(out)


def apply_lua(t, table, why):
    for rel, ops in table.items():
        s = t.read(rel).decode('latin-1')
        for op in ops:
            if op[0] == 'file':
                s = open(os.path.join(HERE, op[1]), 'rb').read().decode('latin-1')
            elif op[0] == 'append':
                s = s + open(os.path.join(HERE, op[1]), 'rb').read().decode('latin-1')
            elif op[0] == 'sub':
                n = s.count(op[1])
                if n != 1:
                    die('%s: anchor occurs %d times: %r' % (rel, n, op[1][:60]))
                s = s.replace(op[1], op[2])
            elif op[0] == 'region':
                start, end, new, incl = op[1], op[2], op[3], op[4]
                if s.count(start) != 1 or s.count(end) != 1:
                    die('%s: region anchors not unique' % rel)
                a = s.index(start); b = s.index(end)
                if b < a:
                    die('%s: region end before start' % rel)
                b = b + len(end) if incl else b
                s = s[:a] + new + s[b:]
        t.write(rel, s.encode('latin-1'), why)


def assert_tier_tables(t):
    """The local grant reads globals.purchases; the store UI shows LuaCoinStore.m_iaps. They must agree."""
    g = re.findall(TIER_RE, t.read(SCRIPTS + 'globals-qkjgbvmvahhiiwj5ogycgsvtg7ebksch.lua').decode('latin-1'))
    c = re.findall(TIER_RE, t.read(SCRIPTS + 'coinstore-cdzrszrqzhyuifgcbh3xptld2a5xkfzm.lua').decode('latin-1'))
    want = [('gold_tier1', '5000'), ('gold_tier2', '15000'), ('gold_tier3', '50000'),
            ('gold_tier4', '150000'), ('gold_tier5', '500000')]
    if g != want or c != want:
        die('IAP tier tables differ from expected: globals=%s coinstore=%s' % (g, c))


def remove_texture_defs(t, names, why):
    s = t.read(ASSETS_LUA).decode('latin-1')
    lines = s.split('\n'); keep = []; paths = []
    for ln in lines:
        m = re.match(r'textures\.([A-Za-z0-9_]+) = \{ path = "([^"]+)"', ln)
        if m and m.group(1) in names:
            paths.append(m.group(2) + '.texture'); names_left = None
            continue
        keep.append(ln)
    if len(paths) != len(names):
        die('assets.lua: expected %d texture defs, found %d' % (len(names), len(paths)))
    t.write(ASSETS_LUA, '\n'.join(keep).encode('latin-1'), why)
    return paths


def remove_textures(t, logical_paths, why):
    """Drop logical textures from assets.archondb/manifest.db and delete physical files that no
    remaining variant still lists. PNGs are removed only if no kept .texture references them."""
    mrel = ASSETSDB + 'manifest.db'
    m = Manifest(t.read(mrel))
    drop_logical = {m.logical_id(p) for p in logical_paths}
    candidate_files = set()
    for k, hdr, body in m.variants():
        strings, present, mapping = m.variant_tables(body)
        for lid, pid in mapping:
            if lid in drop_logical:
                name = m.name_of(strings, pid)
                candidate_files.add(name)
                if name.endswith('.texture'):
                    for ref in re.findall(r'\?\*://([^"]+)', t.read(ASSETSDB + name).decode()):
                        candidate_files.add(ref)
    # references from kept .texture descriptors anywhere on disk
    kept_refs = set()
    for f in os.listdir(t.p(ASSETSDB)):
        if f.endswith('.texture') and f not in candidate_files:
            kept_refs.update(re.findall(r'\?\*://([^"]+)', t.read(ASSETSDB + f).decode()))
    drop_files = {f for f in candidate_files if f not in kept_refs}
    for k, hdr, body in m.variants():
        strings, present, mapping = m.variant_tables(body)
        drop_pids = {pid for pid in present if m.name_of(strings, pid) in drop_files}
        new = []
        for f, wt, v in body:
            from archondb import kv
            e = kv(v)
            if f == 4 and e[1] in drop_logical:
                continue
            if f == 2 and e[1] in drop_pids:
                continue
            if f == 1 and e[1] in drop_pids:
                continue
            new.append((f, wt, v))
        m.set_variant_body(k, new)
    t.write(mrel, m.encode(), why)
    # delete physical files not present in any variant any more
    still = set()
    for k, hdr, body in m.variants():
        strings, present, mapping = m.variant_tables(body)
        still.update(m.name_of(strings, pid) for pid in present)
    removed = []
    for f in sorted(drop_files):
        if f in still:
            continue
        if os.path.exists(t.p(ASSETSDB + f)):
            t.delete(ASSETSDB + f, why); removed.append(f)
    return removed


def json_body(t, rel):
    return json.loads(t.read(rel)[HDR:])


def write_config(t, key, rel, obj, why):
    body = (json.dumps(obj, indent=4) + '\n').encode()
    t.write(rel, sign(key, body), why)


def apply_configs(t, key):
    # dependency safety: nothing kept may depend on a deleted service
    deleted_ids = set()
    for f in DELETE_CONFIGS:
        for s in json_body(t, SC + f)['services']:
            deleted_ids.add(s['id'])
    for rel in signed_files(t):
        if rel == '__asset_manifest.json' or not rel.startswith(SC) or '/information/' in rel:
            continue
        if os.path.basename(rel) in DELETE_CONFIGS:
            continue
        for s in json_body(t, rel).get('services', []):
            deps = set(s.get('dependencies') or []) | set(s.get('facet_dependencies') or [])
            if deps & deleted_ids:
                die('%s (%s) depends on deleted service(s) %s' % (rel, s['id'], deps & deleted_ids))
    for f in DELETE_CONFIGS:
        t.delete(SC + f, 'ad/analytics/tracking service config (identifiers/keys) removed')

    gc = json_body(t, SC + 'gamecenter.json')
    p = gc['services'][0]['params']
    for flag in ('achievments_enabled', 'leaderbaords_enabled', 'achievement_banners_enabled'):
        if p.get(flag) is not True:
            die('gamecenter.json: unexpected %s=%r' % (flag, p.get(flag)))
        p[flag] = False
    for lst in ('achievements', 'ascending_leaderboards', 'descending_leaderboards'):
        if not isinstance(p.get(lst), list):
            die('gamecenter.json: %s is not a list' % lst)
        p[lst] = []
    write_config(t, key, SC + 'gamecenter.json', gc,
                 'social: achievements/leaderboards disabled, store IDs removed (service kept: game depends on it)')

    om = json_body(t, SC + 'overmind.json')
    sp = om['services'][0]['params']
    if sp.get('server_address') != 'https://iris-prod.beast-dev.com':
        die('overmind.json: unexpected server_address')
    sp['server_address'] = OFFLINE_SERVER
    write_config(t, key, SC + 'overmind.json', om,
                 'publisher backend endpoint replaced by reserved .invalid host (service kept: game/iap/platform_ui depend on it)')


def rebuild_asset_manifest(t, key):
    rel = '__asset_manifest.json'
    j = json_body(t, rel)
    for e in j['contents']:
        d = t.p(e['path']) if e['path'] else t.root
        e['files'] = [f for f in e['files'] if os.path.isfile(os.path.join(d, f['name']))]
    body = json.dumps(j, separators=(',', ':')).encode()
    t.write(rel, sign(key, body), 'directory listing updated for removed files; re-signed')


def validate(t, key, luac):
    errs = []
    for rel in signed_files(t):
        if not verify_signed(key, t.read(rel)):
            errs.append('signature: ' + rel)
    j = json_body(t, '__asset_manifest.json')
    for e in j['contents']:
        d = t.p(e['path']) if e['path'] else t.root
        for f in e['files']:
            if not os.path.isfile(os.path.join(d, f['name'])):
                errs.append('listed but missing: %s' % os.path.join(e['path'], f['name']))
    if listing_gaps(t) != t.baseline_gaps:
        errs.append('unlisted files changed: %s' % sorted(listing_gaps(t) ^ t.baseline_gaps))
    for db in ('assets.archondb', 'scripts.archondb', 'engine.archondb'):
        m = Manifest(t.read(db + '/manifest.db'))
        for k, hdr, body in m.variants():
            strings, present, mapping = m.variant_tables(body)
            pres = set(present)
            for pid in present:
                n = m.name_of(strings, pid)
                if n is None or not os.path.isfile(t.p(db + '/' + n)):
                    errs.append('%s variant %d: present file missing: %r' % (db, k, n))
            for lid, pid in mapping:
                if pid not in pres:
                    errs.append('%s variant %d: mapping to non-present id %d' % (db, k, pid))
            for pid in present:
                n = m.name_of(strings, pid)
                if n and n.endswith('.texture'):
                    for ref in re.findall(r'\?\*://([^"]+)', t.read(db + '/' + n).decode()):
                        if not os.path.isfile(t.p(db + '/' + ref)):
                            errs.append('%s: %s references missing %s' % (db, n, ref))
    if luac:
        for f in sorted(os.listdir(t.p(SCRIPTS))):
            if f.endswith('.lua'):
                r = subprocess.run([luac, '-p', t.p(SCRIPTS + f)], capture_output=True, text=True)
                if r.returncode:
                    errs.append('luac: ' + r.stderr.strip())
    return errs


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--apk', required=True)
    ap.add_argument('--libgame', help='default with --in-place: TREE/libgame.so')
    ap.add_argument('--out')
    ap.add_argument('--in-place', action='store_true', help='transform an existing make_data.py tree (last argument)')
    ap.add_argument('tree', nargs='?', help='with --in-place: the out/aodd directory')
    ap.add_argument('--profile', choices=['full', 'lua-only'], default='full')
    ap.add_argument('--keep-weblinks', action='store_true',
                    help='keep privacy-policy and legal web-view buttons')
    ap.add_argument('--no-offline-iap', action='store_true',
                    help='do not install the local offline purchase branch (coin store then uses native billing, which fails closed)')
    ap.add_argument('--luac', help='Lua 5.1 luac for syntax validation')
    a = ap.parse_args()

    if a.in_place:
        if not a.tree or a.out:
            die('--in-place needs the tree as last argument and no --out')
        a.libgame = a.libgame or os.path.join(a.tree, 'libgame.so')
        a.out = a.tree
    elif not a.out or a.tree:
        die('need --out DIR (or --in-place TREE)')
    if not a.libgame:
        die('--libgame is required')
    key = read_key(a.libgame)
    t = adopt_in_place(a.apk, a.tree) if a.in_place else extract(a.apk, a.out)
    assert_inputs(t, key)

    apply_lua(t, LUA_CORE, 'ads/social/telemetry call sites removed')
    if not a.keep_weblinks:
        apply_lua(t, LUA_WEBLINKS, 'external web-link buttons removed (dead offline)')
    if not a.no_offline_iap:
        assert_tier_tables(t)
        apply_lua(t, LUA_OFFLINE_IAP, 'local offline in-app purchase approval (owner request)')
    removed_tex = []
    if a.profile == 'full':
        tex = TEX_CORE + ([] if a.keep_weblinks else TEX_WEBLINKS)
        paths = remove_texture_defs(t, tex, 'texture definitions for removed UI')
        removed_tex = remove_textures(t, paths, 'art used only by removed ad/social/web-link UI')
        apply_configs(t, key)
        rebuild_asset_manifest(t, key)

    errs = validate(t, key, a.luac)
    report = {'tool': 'aod_offline.py', 'profile': a.profile, 'keep_weblinks': a.keep_weblinks,
              'offline_iap': not a.no_offline_iap,
              'apk_sha256': APK_SHA256, 'libgame_sha256': LIBGAME_SHA256,
              'luac_checked': bool(a.luac), 'changes': t.log, 'removed_texture_files': removed_tex,
              'validation_errors': errs}
    with open(os.path.join(a.out, 'ablation-report.json'), 'w') as fo:
        json.dump(report, fo, indent=2)
    if errs:
        for e in errs:
            sys.stderr.write('  ' + e + '\n')
        die('%d validation error(s); see ablation-report.json' % len(errs))
    print('aod_offline: OK profile=%s changes=%d removed_textures=%d out=%s' %
          (a.profile, len(t.log), len(removed_tex), a.out))


if __name__ == '__main__':
    main()
