# Third-party code

| Folder | What | License | From |
|---|---|---|---|
| `minimp3/` | MP3 decoder (import) | CC0 (public domain) | github.com/lieff/minimp3 @ ea99364 |
| `shine/` | MP3 encoder (export), `src/lib` only | LGPL 2 (see `shine/COPYING`) | github.com/toots/shine @ ab5e352 |

Local change: `shine/types.h` defines away `__attribute__` under MSVC (the only GCC-only syntax in the library).
