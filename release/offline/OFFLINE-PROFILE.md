# Offline profile for Army of Darkness Defense 1.1.1 (Vita port)

`aod_offline.py`, `tools/archondb.py` and `patches/lua/*.lua` implement the offline profile that
`release/prepare_data.py` applies to the user's own game data. They are unchanged copies of the tool that
produced the data tree tested on hardware (sha256 of each file in `PROVENANCE.sha256`). The profile
edits only the game's Lua scripts and configuration files; the native libraries are never modified. Every edit checks the input file's
sha256 and a unique text anchor before anything is written, and `prepare_data.py` then compares the
complete result with `release/expected/data-offline.sha256`.

The edit anchors in `aod_offline.py` quote the single lines of the game's scripts they replace, and the
three files in `patches/lua/` are original replacement code (same function names as the game's modules).
No other game content is included.

## 2. Integrity header (recovered, verified)

- Signed files are 32-byte header + JSON body, where header = **SHA-256(K64 ‖ body)**.
- The secure reader is at libgame `0x2ed6cc`. It gets the digest length, creates the hash, calls `update(K64, 64)` then `update(body)`, then `finish`, and does a byte compare.
- K64 is the 64 bytes at vaddr `0x8cc134` (file offset `0x8cb134`). It is **not** stored in the repository. The tool reads it from the user's libgame.so (sha256 `d4d6632d…c41d`) and checks the key's fingerprint `73138e71…84b5`.
- The source verifies 19/19: all 18 `serviceconfigs/**.json` plus `__asset_manifest.json`. Every output signed file is re-verified, and a test confirms that a single flipped bit fails verification.
- Lua scripts and archondb files are **not** signed. `manifest.db` only maps logical to physical names and has no content hashes, and libgame ships `AlwaysValidArchonDbSecurityProvider`.
- archondb `manifest.db` schema: a global string table, then 20 quality/language variant tables, each with a presence list and a logical→physical map. `tools/archondb.py` re-encodes all three dbs byte-identically.

## 3. What changes (exact)

Every edit asserts the input file's sha256 and a unique anchor; a mismatch aborts before anything is written. The per-file before/after sha256 is in `ablation-report.json`.

**Service configs.**
- Deleted, and removed from the signed listing:
  - `ads.json`: DFP interstitial units `/21135348/AODD_*`
  - `applovin.json`
  - `google_video_ads.json`
  - `analytics.json`
  - `flurry.json`: API keys
  - `localytics.json`: API keys
  - `google_conversion_tracking.json`: `install_tracking_id`
  - `hockeyapp.json`: app_id
- The tool checks that no remaining service lists any of these in `dependencies` or `facet_dependencies`.
- `gamecenter.json`: the three `*_enabled` flags are set to false, and the achievement and leaderboard id arrays are emptied. The file is kept because `game` depends on `gamecenter`, and it is re-signed.
- `overmind.json`: `server_address` changes from `https://iris-prod.beast-dev.com` to `https://offline.invalid` (a reserved TLD). The file is kept because `game`, `iap` and `platform_ui` depend on it, and it is re-signed.
- `__asset_manifest.json`: the listing is updated for removed files and re-signed.
- Unchanged: `game`, `http`, `iap`, `notification`, `platform_ui`, and `information/*`.

**Lua (`scripts.archondb/`, 14 files; physical names unchanged)**

| module | change |
|---|---|
| analytics | whole module replaced by same-API no-ops; no `AnalyticsEvent` is built |
| incent_ads | same-API stub: `videoAvailable`=false, preload/show resolve **synchronously** as failure; AdMob unit ids removed; never grants reward |
| upgradeunitmenuwindow | "Free coins" video-ad button branch disabled (`if false`); existing button teardown path kept |
| engine | GameCenter login startup task removed; incent-ad + 24h/72h/1wk re-engagement notification scheduling removed |
| mainmenuwindow | leaderboards/achievements buttons removed; publisher app-update/store prompt removed; privacy web link removed (invisible non-input layout anchor kept so Options/Play stay in place); first-launch real-money IAP warning removed (offline IAP group) |
| optionsmenuwindow | "Legal" web view (backend URL) removed; "Restore Purchases" removed (see §4) |
| achievements | GameCenter reporting removed; local progress counters untouched |
| maingame | leaderboard score posting removed |
| globals | `ratings_dialog_levels = {}` (store-rating prompt after waves 14/29/44 removed) |
| assets | 11 texture definitions for removed UI deleted |
| iap | offline purchase branch appended (§4) |
| coinstore | two lines: price label shows `FREE` instead of a store price; `purchaseProduct` → `chunks.iap.offlinePurchase` |

**Art.** 44 files are removed from `assets.archondb`: the 22 `.texture` descriptors and 22 PNGs behind more-games, leaderboards, achievements, watch-ad, incent-ad popup, video icon, privacy text and legal buttons. Their map and presence entries are removed from all 20 variants. A PNG is removed only if no kept descriptor references it.

**Left alone on purpose.**
- `incentadmenu.lua` and `events.on_more_games`: unreachable now and contain no identifiers.
- `platform_ui.json` `rate_url`: PlatformUI must survive because the intro video and quit dialog use it. The trigger for the rating prompt is removed instead.
- `iap.json` public key.
- The orphan texture `legal_button_text`: nothing references it.

## 4. Local offline in-app purchases (owner request)

This is a Lua-only branch. Google billing is never called: no account, no network, no receipt, no signature.

**Evidence.**
- The only purchasable products are consumable gold tiers `gold_tier1..5` = 5 000 / 15 000 / 50 000 / 150 000 / 500 000. This comes from `globals.purchases`, and the tool asserts it equals the coin store's `m_iaps`.
- `iap.json` has `products: []`.
- `globals.products` is nil. The `ad_buyout` reference sits in unreachable restore code. **This build has no non-consumable product**, so there is nothing that needs durable ownership.

**Behaviour.** A coin-store button press calls `offlinePurchase(id, store_window, ok, fail)`:
- It applies the grant synchronously: gold +amount, `GoldFromIAP` +amount, `offline_iap_seq`+1, and a ledger entry `offline_iap_txn_N = "<id>:<amount>:local"`. All of these go into the same UserData save as gold.
- The UI success callback is deferred one action tick (0.1 s) and guarded so it fires exactly once, which gives the original close-store and coin roll-up animation.
- `GoldFromIAP` follows the game's own rule that "Reset data" keeps purchased gold.

**Player-facing wording.**
- Each tier button shows the original gold amount and `FREE` where the store price used to be.
- The first-launch warning that in-app purchases cost real money is removed.
- No implementation wording appears on screen. `FREE` is a plain English string, chosen over the localized `free_coins` ("Free coins!") to avoid overflowing the button; the layout is unverified on hardware.
- With `--no-offline-iap`, the original price label and warning are kept.

**Guards.**
- A second request while one is pending is refused.
- A retried or duplicate tick cannot re-grant or re-fire. Success and failure callbacks both have a once-guard; the host tests caught and fixed a missing failure guard.
- Unknown ids fail once with no grant.
- A pending flag whose tick never ran is released after 5 s without granting.
- Nothing runs at load time, and there is no auto-click.

"Restore Purchases" is removed: there is no store account offline, and all offline items are consumable gold that is already in the save.

The native purchase fulfilment in libgame is not reused. It is stripped C++ reached only through Google billing callbacks, so the Lua branch reproduces the observable result instead: amounts, `GoldFromIAP`, and store UI flow.

## 5. Test status

- The original tool's host tests (20 tests, including a Lua 5.1 syntax and behaviour check of the
  edited scripts) passed when the profile was made; they need a local Lua 5.1 build and are not part
  of this release.
- `release/tests/test_prepare.py` rebuilds the tree from the real APK and checks it file for file
  against the hardware-tested tree, and exercises the refusals (wrong or modified APK, unsafe archives).
- On hardware, the port with this data boots, and its menus, gameplay, music and sound work (user
  report). The offline coin-store flow, the removed-button layout and long sessions have not been
  checked systematically.
