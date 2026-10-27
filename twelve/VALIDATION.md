# How the SP engine was checked

The SP-1200 / SP-12 engine (`twelve/sp1200.js`, ported line for line to `plugins/twelve/SP1200.h`) simulates the
machines' analog circuits from the component values in E-mu's service manuals (transcribed with sources in
[libmd12](https://github.com/Mudb0y/libmd12), MIT) and their digital side from how the hardware works. Nothing below is
tuned by ear; every number comes from a tool in `twelve/tools/` run against simulation or recordings of a real machine.

Reference recordings: John Nolting's SP-12 measurements, CCRMA 2007, <https://ccrma.stanford.edu/~nolting/220c/>
(noise through each of the 8 outputs, sine sweeps at every tuning, kick / snare / piano before and after the machine).

| Check | Tool | Result |
|---|---|---|
| Circuit solver against ngspice (anti-alias filter, outputs 3-6, SSM2044 output stage) | `sp-circuits.mjs` | within 0.3 dB to 16 kHz (the anti-alias notch, more than 50 dB down, differs more) |
| Outputs 3-6 against the real SP-12 (noise, each output over output 8) | `sp-measure.py`, `sp-fit.mjs` | parts as drawn: 0.7-1.6 dB rms off; capacitors 6-9 % under nominal (inside 10 % parts): 0.17-0.26 dB rms |
| SSM2044 at rest (outputs 1-2) | `sp-fit.mjs` | 925 Hz stage frequency, feedback 0.3: 1.4 dB rms |
| The filter sweep after a hit (envelope capacitor, diode and resistor network, simulated) | `sp-sweep.mjs` | follows the real machine within 0.24 octave rms over 0-0.6 s, with C111 15 % under its 10 uF (tantalum, +-20 %) |
| Pitch: how a tuning step becomes a playback step | note lengths of `sine_sweep_all_ch_tune.wav` | all 15 lengths match the SP-12 rule ((byte OR 1) + 1) / 128 within 0.3 %; the SP-1200 rule (byte + 1) / 128 would miss by up to 1.4 % |
| Whole chain: kick, snare, piano sampled and pitched down 7 steps, octave bands against the SP-12's recordings | `sp-listen.mjs` | within about 2-4 dB per band from 355 Hz to 15 kHz, aliasing included; the snare through output 4 within about 1 dB from 10 to 13.75 kHz |
| C++ against JavaScript | `compare.mjs` | identical samples in all 9 cases |

What it doesn't know: the hold capacitors' droop, DAC nonlinearity and op-amp slew are left out (no published data);
the hold output's full scale is taken as 5 V, which sets how hard loud sounds drive the SSM2044's nonlinearity; the
SSM2044's internal resonance amount is fitted from the recording, not from a schematic. Below 200 Hz the pianos in the
reference recordings have 5-10 dB more bass than the engine gives; both of Nolting's versions (the machine and his model)
share it, so it may be his recording chain rather than the SP-12.

Calibration "Measured SP-12" uses the fitted tolerances; "Service manual" uses the parts as drawn.
