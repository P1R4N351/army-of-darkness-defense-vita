# Trophy tooling provenance

- Homebrew service contract: Rinnegatamante/NoTrpDrm,
  commit `aea3950297182ae56ddf261b76fe6763b70e890d`, official README and source.
- TRP writer, PNG encoder and native setup ABI reference:
  zm2283145/goldenballoon-vita-port,
  commit `cd06074b6f9f99f8deb90ebaf82891af1243f373`,
  `tools/build_vita_trophy_pack.py` and `platform/vita_trophy.c`, MIT.
  The preserved license is `LICENSES/GoldenBalloon-MIT.txt` in the source and
  preparation kit. Adaptations use AOD's stable identity and original geometric
  icon design.
- Additional API behavior reference: WolffsRoom/DeltaruneVita,
  commit `2ec956d76ab4436dc581cbc1e8afc755017b785d`, legacy trophy backend.
- Storage design accounts for VitaSDK newlib commit `892f530`,
  `newlib/libc/sys/vita/syscalls.c`: `_rename_r` removes the destination first.
  The two-slot implementation does not use rename.

The 52 keys are interface identifiers verified against the user's supported
original APK configuration and achievement module. `mapping.json` freezes their
numeric correspondence; `metadata.json` contains port-authored labels and factual
predicate summaries, not a copied localization file. Original scripts, assets,
service configuration and APK are not distributed. The only Lua rewrite is a
strict, hash-verified transformation applied to the user's own data.

All trophy grades are bronze by homebrew policy, with no synthetic platinum or
extra achievements. Existing IDs, completion predicates and original save
counters are preserved. The pack is unsigned and intended for the documented
NoTrpDrm homebrew workflow; no retail trophy or PSN service is modified.
