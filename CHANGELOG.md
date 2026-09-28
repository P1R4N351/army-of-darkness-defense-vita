# Changelog

## 1.0 (v1.0.0), first release

Army of Darkness Defense (Android 1.1.1) for PS Vita, by PiSCES.

- So-loader port: the game's own ARMv7 `libgame.so` and `libfmodex.so` run natively, with Android/JNI,
  OpenGL ES (vitaGL) and OpenSL ES bridges.
- Graphics: correct viewport under the softfp ABI; the game's GLSL shaders are compiled at run time.
- Audio through FMOD Ex and OpenSL ES:
  - sound effects;
  - music streams;
  - the output runs at FMOD's own sample rate, so pitch and speed are correct.
- Touch input; Circle/Select act as Back and Start as Menu.
- `prepare_data.py` builds the data folder from the user's own APK, offline, with the offline profile:
  - no ads, analytics or social services;
  - local, free coin-store purchases;
  - every output file hash-checked.
- Diagnostics are off by default (`ux0:data/aodd/diag.enable` turns them on).
