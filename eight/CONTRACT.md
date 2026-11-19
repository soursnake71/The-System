# Omega 808: shared contract (internal: for the people and agents building it)

Omega 808 is built the same way as Omega Twelve (`twelve/`). It is a DOM-free JS engine running in an AudioWorklet,
a web UI, an Android flavor that bundles the web app offline (`eight` flavor, like `twelve`), and a JUCE VST3 whose
engine is a line-for-line C++ port of the JS engine (`plugins/eight/`), checked against it by `eight/tools/compare.mjs`.

## Files and owners
| File | What |
|---|---|
| `eight/fx.js` | `Rev7` (Yamaha REV7-style digital reverb, its program list) and `TC2290` (TC Electronic 2290-style delay) |
| `eight/engine.js` | `Eight808`: the 808 voices modelled as circuits, the accent bus, the trigger-out click, sample slots, mixer, FX sends |
| `eight/seq.js` | `Seq808`: the 808 sequencer (basic rhythms 1-12, intro/fill 1-4, A/AB/B variation, 1st/2nd part, pre-scale, auto fill in, swing, accent track) |
| `eight/worklet.js` | the AudioWorkletProcessor `eight-808` that owns one `Eight808` and one `Seq808` |
| `eight/tools/test.mjs`, `render.mjs` | Node tests: render every voice to WAV, check levels, NaN, decay, accent |
| `eight/index.html`, `app.js`, `style` | the faceplate photo UI, fold-open circuit panel, sample loading, FX panel |
| `eight/presets.js` | kits, rhythms and FX presets (lots) |
| `eight/midi.js` | Web MIDI + Android MIDI shim, note map, CC map, learn, clock in/out, trigger out as MIDI |
| `android/.../eight/` | `EightActivity.java`, manifest, strings, icon |
| `plugins/eight/` | C++ port + JUCE plugin + editor (photo faceplate) |

## Module style (every DSP file: fx.js, engine.js, seq.js)
Plain JS with no DOM and no imports. Wrap it like this:
`(function (g) { 'use strict'; ...; g.Eight808 = Eight808; if (typeof module !== 'undefined' && module.exports) module.exports = {...}; })(typeof globalThis !== 'undefined' ? globalThis : this);`
The worklet loads them in this order: `audioWorklet.addModule('fx.js')`, `('seq.js')`, `('engine.js')`, `('worklet.js')`.
They share the worklet global scope. Node loads them with `require`. Do not use `Math.random()` in DSP.
Use a seeded xorshift32 instead, so the C++ port renders the same samples.

## Voices (trigger ids)
`BD SD LT MT HT LC MC HC RS CL CP MA CB CY OH CH TRIG`
Sequencer tracks (the 12 INSTRUMENT-SELECT positions): `AC BD SD LT MT HT RS CP CB CY OH CH`, plus an extra `TRIG` track.
The tom/conga, rimshot/claves and clap/maracas switches (`LT.sel` and the others) decide which voice a track plays.
MIDI and direct hits can still play any voice.
The OH and CH voices choke each other, as on the 808 (CH cuts OH).

## Parameters
Use one flat key space for everything: `engine.set(k, v)`, preset `params`, MIDI CC targets and the plugin parameter IDs.
The engine exports `Eight808.PARAMS = [{k, label, group, min, max, def, step?, opts?}]`. That list is the source of truth.
The UI builds the circuit panel from it, and the plugin generates its parameters from it.

Faceplate, all 0..1 unless noted:
`master AC.level BD.level BD.tone BD.decay SD.level SD.tone SD.snappy LT.level LT.tuning LT.sel MT.level MT.tuning MT.sel
HT.level HT.tuning HT.sel RS.level RS.sel CP.level CP.sel CB.level CY.level CY.tone CY.decay OH.level OH.decay CH.level`
The `*.sel` keys are 0 or 1 (0 = tom / rimshot / handclap, 1 = conga / claves / maracas). The defaults follow the photo's knob positions.

Circuit mods (the fold-open panel). Each default is the stock circuit, so a fresh kit sounds like a stock 808:
- BD: `mod.BD.tune` (0.5 = stock), `mod.BD.sweep` (pitch-envelope depth), `mod.BD.longDecay` (the famous long-boom decay mod, 0 = stock), `mod.BD.click`, `mod.BD.drive`
- SD: `mod.SD.tune`, `mod.SD.decay`, `mod.SD.noiseColor`
- Toms and congas: `mod.TOM.decay`, `mod.TOM.sweep`, `mod.CONGA.decay`
- RS, CL, CP, MA: `mod.RS.tune`, `mod.CL.tune`, `mod.CP.tail`, `mod.CP.spread`, `mod.MA.decay`
- CB and the metal voices: `mod.CB.tune`, `mod.CB.decay`, `mod.HH.tune` (the shared 6-square-oscillator bank), `mod.CH.decay`, `mod.CY.tune`
- Accent: `mod.AC.depth` (0.5 = stock), `mod.AC.punch` (accent also brightens and tightens; 0 = stock)
- Trigger out (Egyptian Lover): `mod.TRIG.src` (0 CB, 1 CP, 2 AC, 3 its own TRIG track), `mod.TRIG.width` (ms, 1..40, stock 20), `mod.TRIG.level` (into the mix, 0 = off), `mod.TRIG.tone` (coupling-cap high-pass)
- Drive: `mod.BD.drive`, `mod.SD.drive`, `mod.TOM.drive`, `mod.CP.drive`, and `mod.drive` (machine overdrive: the mixer bus and sends)
- Whole machine: `mod.age` (component tolerance and drift), `mod.hiss`, `swing` (0..1)

FX sends: `rev.send.<V>` and `dly.send.<V>` for each of the 12 tracks' voices and TRIG. Use `<V>` in `BD SD LT MT HT RS CP CB CY OH CH TRIG`. LC/MC/HC use the LT/MT/HT send, CL the RS send, MA the CP send.

Rev7: `rev.program` (index into `Rev7.PROGRAMS`, REV7-style list: Hall 1, Hall 2, Room 1, Room 2, Vocal 1, Vocal 2, Plate 1, Plate 2, Early Ref 1, Early Ref 2, Gate Reverb, Reverse Gate, ...).
Its other keys are `rev.time` (s), `rev.predelay` (ms), `rev.hf` (HF ratio), `rev.diffusion`, `rev.density`, `rev.lpf`, `rev.hpf`, `rev.er` (early/late balance) and `rev.return`.

TC 2290: `dly.time` (ms), `dly.sync` (0/1), `dly.division` (index), `dly.feedback`, `dly.hicut`, `dly.lowcut`, `dly.modDepth`, `dly.modRate`, `dly.pingpong` (0/1), `dly.duck` (the 2290 dynamic delay), `dly.return`.

## Patterns
Patterns are JSON, shared by the sequencer, presets, the UI and the plugin state:
```
pattern = { scale: 0..3,            // pre-scale 1-4: 0 = 1/16 (16 steps), 1 = 1/32 triplet-ish 8th... see seq.js; 2 = 16th triplets (12 steps), 3 = 8th triplets
            len: 1..16,             // last step of each part
            parts: 1|2,             // 2 = 1st and 2nd part (up to 32 steps)
            steps: { AC:'x...x...', BD:'x.........x.....', ... } }  // one char per step, '.' off, 'x' on; length len*parts
rhythm  = { A: pattern, B: pattern }           // basic variation A and B
bank    = { basic: [12 rhythm], fill: [4 rhythm], tempo, swing, fillEvery: 0|2|4|8|12|16 }
```

## Worklet messages
UI to worklet (`node.port.postMessage`):
`{t:'set', k, v}` · `{t:'setMany', p:{k:v}}` · `{t:'hit', v:'BD', acc:0|1, vel:0..1}` · `{t:'play', on:true|false}` · `{t:'tempo', bpm}`
`{t:'bank', bank}` · `{t:'select', basic:0..11, fill:0..3, variation:'A'|'AB'|'B', ifVar:'A'|'B'}` · `{t:'tap'}` (fill-in trigger; while stopped it sets the intro)
`{t:'edit', which:'basic'|'fill', n, var:'A'|'B', track, step, on}` · `{t:'sample', v, data:Float32Array, sr}` · `{t:'sample', v, clear:true}`
`{t:'clock', bpm}` (external MIDI clock: follow its tempo) · `{t:'pos', step}`

Worklet to UI:
`{t:'step', step, part, rhythm, which, var, hits:['BD','CH'], acc:0|1, trig:0|1, time}`, sent on every step for the LEDs, MIDI out and trigger out.
`{t:'ready', params:Eight808.PARAMS, programs:Rev7.PROGRAMS}`
