# Local homebrew trophies

This optional integration maps the game's 52 original achievement keys to a
homebrew trophy set. Host checks and an isolated Vita softfp build pass; emulator
behavior and real NoTrpDrm registration/unlocks remain unverified.
It does not contact Game Center, PlayStation Network or any advertising service.

The target is [NoTrpDrm](https://github.com/Rinnegatamante/NoTrpDrm), using its
documented homebrew `sceNpTrophy` route. Install the plugin according to its own
official instructions if you want trophies. The game does not install plugins or
alter device configuration. Without an available trophy service, gameplay
continues and new completed events are journalled for a later launch. Creating
`ux0:data/aodd/no-trophies` explicitly disables both trophy registration and this
local event journal.

## Inputs and stable identity

Prepare data again from your own supported Android 1.1.1 APK using this source's
`release/prepare_data.py`. Its strict, hashed transformer changes only the central
achievement reporting function after the existing offline profile. Original
predicates, thresholds and save counters remain unchanged. Do not overwrite your
existing `files/` save folder when copying prepared data.

`release/trophies/mapping.json` fixes IDs 0–51 and the communication ID
`AODD00001`; the TRP directory is `sce_sys/trophy/AODD00001_00/`. These must not
be casually renumbered or replaced after installation. The application title ID
also remains `AODD00001`.

All 52 awards are bronze. This is an explicit homebrew presentation policy, not
a claimed conversion of Game Center scores: the source configuration supplies
identifiers, not PlayStation trophy grades. There is no invented platinum or
extra completion condition. Human-readable names and descriptions in
`metadata.json` are port-authored from the original keys and predicates;
`predicate` fields record the condition used to check each description. The
geometric icons are original generated art, not extracted game artwork.

The original rules contain quirks that are intentionally preserved. For example,
the courtyard condition counts enemy deaths at the courtyard boundary, and the
all-upgrades condition counts final-upgrade events. The port does not rewrite
those game rules while adding trophies.

## Earned-event bridge

The original achievement dispatcher emits a completion only at exactly 100%.
The patched Lua dispatcher calls `os.getenv` with a reserved prefix and the
original key. The native game's Lua OS registration and getenv PLT import were
checked against the supported library hash; the loader already resolves getenv
through `getenv_soloader`. This avoids relying on the game's disabled local
`print` function or enabling external Game Center calls.

The callback validates the key and sets one bit under a short mutex. Disk I/O and
`sceNpTrophy` calls run on a worker thread, outside the gameplay callback. The
52-bit set cannot overflow from duplicate reports. Partial, non-finite, malformed
or unknown messages do not create awards. There is no bulk retrospective award
from guessed wave counters: an original predicate must report completion while
the patched script is running.

At startup, registration is attempted before the native game lifecycle. The
setup-dialog loop is limited to 30 seconds. Unlock errors keep the event pending;
the backend checks native unlocked state instead of guessing error constants.
Round-robin selection prevents one failing trophy from starving later awards.
Disk and platform retries have independent exponential delays, capped at one
minute. New events remain queued during a disk retry delay.

## Journal and limits

Earned events are persisted before attempting the platform award. The journal
uses `files/homebrew-trophies-v1.dat.0` and `.1`, with independent checksums and
64-bit generations. Before reclaiming an inactive slot, the selected valid record must pass a fresh
fsync barrier. This covers an earlier failed flush that left a structurally valid
newer record in cache. Each write then targets the inactive slot; it never renames or
removes the current valid slot. This matters because the pinned Vita newlib
rename implementation removes its destination before renaming. A legacy single
file, if present, is read without deleting it during migration.

Recovery selects the latest valid slot using serial generation arithmetic,
including wrap from the maximum generation to zero. Conflicting equal or
half-range generations fail closed. A corrupt newest slot falls back to the
previous valid slot. If there is no valid record, trophies are disabled for that
run while gameplay continues and the damaged files are preserved. A worker that
observed a completely absent journal at startup can retry its own interrupted
first write using its still-live earned bits; that permission ends after its first
successful save and is never inferred from corrupt files on a later launch.

This protects the last valid record against interrupted writes; it cannot make
an event durable before its first successful flush, guarantee Vita filesystem
power-loss behavior, or authenticate locally edited save files. Device testing
remains necessary. The game saves are separate and are not rewritten by this
subsystem.

## Reproduce host checks

On Linux with a C compiler and Python 3:

```
sh tests/run_trophies_test.sh
```

This includes native queue/scheduler tests and link-time write/fsync/close fault
injection against the real storage implementation. Optional sanitizers can be
enabled with `AODD_SANITIZE=1`. Tests cover every torn-write byte boundary,
corruption, migration, failed opens, generation wrap, duplicate reports and
permanently failing lower IDs.

The release tests validate the reproducible pack with an independent reader and
exercise the extracted preparation kit. With `AODD_ASSETS` pointing to your
original extracted assets, `AODD_TROPHY_CORE` to a host shared build of
`source/aod/trophies.c`, and host `lupa.lua51` installed, the tests also execute
original achievement predicates and compare their completion reports with the
patched Lua/native bridge. `AODD_APK` enables the existing end-to-end preparation
regressions. No private APK or complete original script is included in source.

Host success does not prove Vita ABI correctness, trophy registration, popups,
power-loss durability or gameplay regression freedom. They remain unverified in the experimental 1.1 release; no native trophy-operation claim is made.
