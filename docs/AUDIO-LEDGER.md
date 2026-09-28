# Audio ledger

## Facts (boot-10, from emulator validation runs)
- F1: FMOD uses OpenSL ES. `CreateAudioPlayer` is rejected by the SDK libOpenSLES:
  `SL_LOGE … class AudioPlayer interface 1 required but unavailable MPH=48` (`MPH_to_AudioPlayer[48] = -1`, i.e. SL_IID_ANDROIDCONFIGURATION).
- F2: PCM is all zero; 9 outputs; 1 completed FillBuffer, then the audio thread stops (a second issue, unresolved).
- F3: FMOD's exact OpenSL sequence (libfmodex disassembly, `0xafc14`–`0xafe00`):
  1. `CreateAudioPlayer(engine, &player, src, sink, 2, {ANDROIDSIMPLEBUFFERQUEUE, ANDROIDCONFIGURATION}, {TRUE, TRUE})`.
     - src: locator `0x800007BD` plus PCM (formatType 2, N channels, rate×1000 mHz, 16/16 bit, mask 3 stereo or 4 mono, little-endian).
     - sink: output mix.
  2. `GetInterface(player, ANDROIDCONFIGURATION)`.
  3. `SetConfiguration("androidPlaybackStreamType", &SL_ANDROID_STREAM_MEDIA (3), 4)`.
  4. `Realize(player, FALSE)`.
  5. `GetInterface(PLAY)`, `GetInterface(ANDROIDSIMPLEBUFFERQUEUE)`, `RegisterCallback`.

  Every non-zero result is fatal to FMOD's OpenSL output: it returns FMOD error 60 (create) or 33 (the rest). There is **no retry and no fallback**.
- F4: The SDK headers define only `androidPlaybackStreamType` (VOICE 0 … NOTIFICATION 5) and `androidRecordingPreset`. SLEngineItf has 15 methods and SLObjectItf 10.

## Root cause (first failure)
The SDK OpenSL ES has no AndroidConfiguration interface on AudioPlayer, and FMOD requires it and uses it for exactly one call, setting the stream type to MEDIA before Realize. So FMOD never gets a player, and no PCM is ever produced.

## Fix design (boot-11)
- `slCreateEngine` is resolved through our dlsym table, so the port returns a thin **proxy** engine object. Its SL_IID_ENGINE interface forwards every method except CreateAudioPlayer.
- For an AudioPlayer that requests SL_IID_ANDROIDCONFIGURATION, the proxy CreateAudioPlayer:
  - removes that ID from the list passed to the real library (every other ID and its required flag keep their order);
  - wraps the returned player object in a proxy whose GetInterface(ANDROIDCONFIGURATION) returns a **real per-player implementation** with Android semantics:
    - `androidPlaybackStreamType`: SLint32 in [0, 5]; `valueSize` must be ≥ 4 (else PARAMETER_INVALID); allowed only before Realize (after it, PRECONDITIONS_VIOLATED); default MEDIA.
    - GetConfiguration: a NULL value pointer queries the size; returns the stored value.
    - Unknown keys and the recorder key on a player: PARAMETER_INVALID, as in AOSP.
  - Everything else is forwarded to the real object; Destroy destroys the real object and frees the proxy.
- **Vita mapping:** the SDK library mixes every player into its single BGM port. On the Vita there is no per-stream routing, so every Android stream type plays through that port. The stream type is kept and reported, not faked as routed.

## Predictions for the boot-11 emulator run (written before any run)
- P1: the `SL_LOGE … MPH=48` line is gone; the log shows `opensl: AudioPlayer … ANDROIDCONFIGURATION provided by port; stream type 3 (MEDIA)`.
- P2: CreateAudioPlayer, Realize, GetInterface ×2 and RegisterCallback succeed.
- P3: non-zero PCM reaches sceAudioOut (`nonzero > 0`, max-peak > 0) **if** the F2 stall was a consequence of the failed player. If the output count still stops after about 1 fill, the stall is independent (F2b) and needs its own probe. This fix does not claim to cure it.

## Implementation and review resolution (boot-11, after code review)
- **R1 (wiring), resolved and verified in the linked `build-b13/so_loader`:**
  - the dlsym table entry `"slCreateEngine"` → `0x8100dad1` = `aod_slCreateEngine` (Thumb);
  - `main` calls `aod_opensl_set_backend(slCreateEngine, *SL_IID_ENGINE, *SL_IID_ANDROIDCONFIGURATION)` with the IID **values** (`ldr` through the SDK variables), before `soloader_init_all` and so long before `nativeCreate`, where FMOD dlopens OpenSL ES.
- **R3 (NULL clobber), fixed:** a repeat GetInterface(ENGINE) fetches into a local and assigns only on success. A new test (a failing repeat, then the old engine proxy is used) passes in the target ABI.
  - The negative run against the old code **hung under qemu and had to be stopped manually**. It is recorded as "did not pass", **not** as a demonstrated assertion failure. The test runner now uses a named container and removes it on every exit.
- **R4 (callback identity), no change needed.**
  - FMOD's only RegisterCallback is at `libfmodex 0xafdf8`: `ldr pc, [r3, #12]` with `r3 = *(player+680)`.
  - That slot was filled at `0xafdd0` by `GetInterface(player, SL_IID_ANDROIDSIMPLEBUFFERQUEUE, player+680)`, the IID taken from the GOT entry held in r8.
  - Offset +12 of `SLAndroidSimpleBufferQueueItf_` is RegisterCallback, while the object's RegisterCallback is at +16 of `SLObjectItf_`.
  - So FMOD registers on the SDK's real buffer-queue interface, and the `caller` it receives is the interface it holds.
- **R5:** no change.
- **Test ABI note:** the SDK types `SLint32`/`SLuint32` are `long`. The host (LP64) build therefore mis-sized them, so the functional test runs as 32-bit ARM softfp (NDK r27d) under qemu-arm with UB trapping. A `_Static_assert` in the module pins them to 4 bytes on ARM. The SDK's `OpenSLES_AndroidConfiguration.h` is usable only from C++ (an unclosed `#ifdef __cplusplus`), so the constants are restated and checked against it by a C++ test.
- **Not proven by these tests (R2):** that the real SDK player realizes, that FMOD's buffer-queue callback is driven by `IOutputMixExt_FillBuffer`, and that PCM becomes non-zero. That needs the emulator run of this build (see emulator/BOOT11-NEXT.md). The separate boot-10 stall (1 completed FillBuffer) may persist independently of this fix.

## BGM: FMOD stream creation (boot-13)
- **Observed** (emulator, boot-12): `createStream("loop.ogg", 0x2) -> 33` in 74.8 ms, while every SFX `createSound` succeeds.
- **Error code:** 33 is `FMOD_ERR_INTERNAL` in FMOD Ex 4.44.31 (`0x00044431`). The enum was calibrated on this library: `getVersion(NULL)` → 37 = `FMOD_ERR_INVALID_PARAM`.
- **Path, stream only** (libfmodex disassembly):
  1. `SystemI::createSoundInternal` (`0x46b44`) enables file double buffering (`0xa632c`).
  2. That finds or creates a file thread (`0xa61f0` → `0xa60dc`) through the thread helper (`0xa6150`), which requests a **hard-coded 8192-byte stack** (`0xa6138`).
  3. `FMOD_OS_Thread_Create` (`0xadfdc`) calls `attr_init`, `setdetachstate`, `setstacksize(max(req, 8192))`, `create`, `destroy`, and returns 33 if any of them fails.
- **Failure:** the bridge passed 8192 straight to pthread-embedded, whose `pthread_attr_setstacksize` returns EINVAL below 32768 (the linked code checks `cmp r1, #0x8000`; the SDK's `PTHREAD_STACK_MIN` is `32*1024`). Bionic (32-bit) accepts anything ≥ 8192.
- **Why SFX work:** FMOD's other thread stacks come from its defaults (stream and nonblocking 65536, mixer 49152, set at `0x43a28`–`0x43ac4`), and the game never calls `setAdvancedSettings`.
- **Fix:** `pthread_attr_setstacksize_soloader` keeps bionic's rule (EINVAL below 8192) and raises smaller valid requests to 32768, with a capped log line. Real threads still get 512 KiB from `pthread_create_soloader`.
- **Clock epoch ruled out for this error:** libfmodex imports `gettimeofday` (newlib's, not our skewed `clock_gettime`) and no timed waits, and the 33 is returned synchronously from thread creation.
