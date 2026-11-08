# shine (vendored)

The fixed-point MPEG audio layer III (MP3) encoder from https://github.com/toots/shine, commit `ab5e3526b64af1a2eaa43aa6f441a7312e013519`,
`src/lib` copied unchanged. Licence: GNU Library General Public License version 2 (`COPYING`).

Omega SID 64 uses it for its MP3 export (`plugins/sid/SidExport.h`). It is compiled from this source into
the plugin and the Android app; the complete source of both is in this repository, so it can be changed and rebuilt.

Built as its own static library (`omega_shine` in `plugins/CMakeLists.txt`, and in `android-sid`), with
`dmask` and `hs` renamed to `shine_dmask` / `shine_hs` by compiler definitions, since those two short global names
could meet others in a plugin.
