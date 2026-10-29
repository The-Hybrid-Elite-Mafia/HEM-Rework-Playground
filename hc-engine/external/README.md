# Third-party code in the engine

All of it is free and allowed inside a GPLv3 program. Nothing here needs a paid licence.

| Folder | What | Version | Licence |
|---|---|---|---|
| `pugixml/` | XML reader / writer (sessions, presets) | v1.14 | MIT |
| `dr_libs/` | WAV / AIFF (`dr_wav.h`), FLAC (`dr_flac.h`), MP3 (`dr_mp3.h`) decoders | dr_wav 0.14.6, dr_flac 0.13.4, dr_mp3 0.7.4 — github.com/mackron/dr_libs commit `39ba1ae7945d562ed18db5bba89298343c121222` | Public domain (Unlicense) or MIT-0, your choice |
| `stb/` | Ogg Vorbis decoder (`stb_vorbis.c`) | v1.22 — github.com/nothings/stb commit `003501f6d7a8668ce8b6d875e34dd97ad0d5d5b5` | Public domain (Unlicense) or MIT, your choice |

SHA-256 of the files as downloaded:

```
8f1585bd0141849ed018d07cc7026ac74c12e77d64e662de986cfaeb21b82eb3  dr_libs/dr_wav.h
898ad822e21da1127ef8065eca655bcff8dc5f6060a411724c5c48017488b1b6  dr_libs/dr_flac.h
390e4e0ef0cff80fb86ebf1b461d6947cfa4ed699a4fe107e5a5e69a47db2950  dr_libs/dr_mp3.h
4c7cb2ff1f7011e9d67950446b7eb9ca044f2e464d76bfbb0b84dd2e23e65636  stb/stb_vorbis.c
```
