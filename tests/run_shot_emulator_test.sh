#!/usr/bin/env bash
# Isolate each real update_sfx() weapon playback on Genesis Plus GX and
# correlate its emitted waveform with the original sample (not just RMS).
# Requires NumPy and SciPy; the default GPGX audio rate is 44100 Hz.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
: "${GDK:?Set GDK}"
: "${GPGX_CORE:?Set GPGX_CORE}"
: "${GPGX_INCLUDE:?Set GPGX_INCLUDE to the directory containing libretro.h}"
PREFIX="${PREFIX:-m68k-elf-}"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
cc -O2 -o "$TMP/harness" "$ROOT/tests/libretro_harness.c" -I"$GPGX_INCLUDE" -ldl -lm
for shot in 0 1 2 3 4; do
  PATH="$GDK/bin:$PATH" make -C "$ROOT" GDK="$GDK" PREFIX="$PREFIX" clean >"$TMP/build.log" 2>&1
  PATH="$GDK/bin:$PATH" make -C "$ROOT" GDK="$GDK" PREFIX="$PREFIX" release -j4 EXTRA_FLAGS="-DJAZZ_SFX_TEST=$shot" >>"$TMP/build.log" 2>&1 || { cat "$TMP/build.log"; exit 1; }
  JAZZ_CAPTURE="$TMP/$shot.raw" "$TMP/harness" "$GPGX_CORE" "$ROOT/out/rom.bin" 180 '' "$TMP/screen_" >"$TMP/run.log" 2>&1 || { cat "$TMP/run.log"; exit 1; }
done
python3 - "$TMP" "$ROOT" <<'PY'
import sys, wave
from pathlib import Path
import numpy as np
from scipy.signal import correlate
p,root=map(Path,sys.argv[1:])
shots=[('fire_blaster.wav',13000),('fire_toaster.wav',10000),('fire_upward.wav',10000),('fire_bouncer.wav',13000)]
for i,(filename, rate) in enumerate(shots):
    data=np.fromfile(p / f'{i}.raw',dtype='<i2').astype(float).reshape(-1,2).mean(axis=1)
    with wave.open(str(root/'res/jj1'/filename)) as w:
        source=np.frombuffer(w.readframes(w.getnframes()),dtype=np.uint8).astype(float)-128
    # Compare the first ~113 ms, searching time offset. The Genesis emulation
    # and DAC apply a little filtering, hence correlation rather than equality.
    length=5000
    ref=np.interp(np.arange(length)*rate/44100,np.arange(len(source)),source)
    corr=correlate(data,ref,mode='valid',method='fft'); k=int(np.argmax(np.abs(corr)))
    denom=np.linalg.norm(data[k:k+length])*np.linalg.norm(ref)
    coefficient=float(corr[k]/denom) if denom else 0
    assert coefficient > 0.45, (filename, coefficient)
    # Also ensure the chosen clip matches more closely than any other weapon
    # or the formerly misassigned rapid-fire announcement.
    for other, other_rate in shots + [('05_FIRE1.wav',11025)]:
        if other == filename: continue
        with wave.open(str(root/'res/jj1'/other)) as w:
            raw=np.frombuffer(w.readframes(w.getnframes()),dtype=np.uint8).astype(float)-128
        candidate=np.interp(np.arange(length)*other_rate/44100,np.arange(len(raw)),raw)
        c=correlate(data,candidate,mode='valid',method='fft'); j=int(np.argmax(np.abs(c)))
        norm=np.linalg.norm(data[j:j+length])*np.linalg.norm(candidate)
        competing=abs(float(c[j]/norm)) if norm else 0
        assert coefficient > competing + 0.1, (filename, other, coefficient, competing)
    print(f'PASS: Genesis Plus GX {filename} firing waveform correlation {coefficient:.3f}')
# Boot reset can make a brief transient; look after the first 0.2 seconds.
silent=np.fromfile(p/'4.raw',dtype='<i2').astype(float)[2*8820:]
assert np.max(np.abs(silent)) < 20, 'special shot should have no assigned sound'
print('PASS: special shot has no firing sound')
PY
