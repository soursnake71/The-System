/* XL-Desk help: how to use it, where every number came from, and what had to be estimated. */
'use strict';
function renderHelp(){
  const v = document.querySelector('#v-help');
  v.innerHTML = `<div class="card help">
<h2>XL-Desk</h2>
<p>A working model of the Solid State Logic XL-Desk: 16 dual-input mono channels (VHD mic preamps on 1-8), 4 stereo channels, 2 stereo returns, four stereo mix buses with Mix B, C and D summable into Mix A, stereo cue and two mono auxes, the 16 + 2 slot 500-series rack with the G Series Bus Compressor in slots 17-18, and the full monitor section: three speaker sets with bass management, two foldbacks with talkback, headphones, the listen mic and its compressor.</p>
<p><b>Made for Absolute Future Studios.</b> This is an independent emulation built from SSL's published manuals; it is not made or endorsed by Solid State Logic.</p>
<h3>Getting sound through it</h3>
<ol>
<li>Press <b>⏻</b> (power) in the header.</li>
<li>Tape tab: load your stems. Mono files land on DAW inputs 1-16, stereo files on the stereo channels. Press <b>DAW</b> on a mono channel to hear its DAW input (that is the desk's own behaviour: the main input of 1-8 is the VHD preamp, of 9-16 the line input).</li>
<li>Press ▶. Everything is routed to Mix A, faders at 0 dB, Mix A feeds the monitors.</li>
<li>For live inputs, open the Rear panel tab, press <b>Enable input</b> and patch interface channels to the mic, line, DAW and stereo inputs. With a multichannel interface, patch the console outputs (Mix A-D, the direct outs, foldbacks, the speaker sets and the sub) to its outputs too.</li>
<li>Print with ●: it records the outputs you tick on the Tape tab, as 24-bit WAV.</li>
</ol>
<h3>Working the surface</h3>
<ul>
<li>Knobs: drag up and down. Hold shift (or use a mouse wheel) for fine moves. Double-tap returns a control to its default.</li>
<li>Tap the 500 module above a channel to open its faceplate; the 500 Rack tab shows all 18 slots.</li>
<li><b>Solo safe</b>: SAFE, then the SOLO buttons to protect (they flash), then SAFE. <b>CLEAR</b> cancels solos. <b>AFL</b> switches from solo-in-place to after-fader listen.</li>
<li><b>SUB</b> remembers its state for each speaker set. Hold <b>MINI 1</b> or <b>MINI 2</b> for three seconds to trim that set in 0.5 dB steps with DIM (down) and CUT (up), then press MINI again; the Mix A meter shows the offset.</li>
<li><b>TALK A/B</b> are momentary unless DIP 3 / 5 are down (latching). DIP 2 / 4 down makes talkback replace the foldback instead of mixing with it.</li>
<li>The bus compressor's send is always live, as on the desk: its needle moves even when COMP is out. The module's IN switch enables the sidechain; its VCA and make-up are in whenever COMP is.</li>
<li>Empty 500 slots and empty INS returns are silent when switched in, as on the desk. The Rear panel tab lets you pick what is patched to each INS, and pull the four 500 link cables.</li>
</ul>
<h3>MIDI</h3>
<ul>
<li>Every control answers MIDI with no setup: MIDI channel n is mono channel n (fader CC 7, pan CC 10, trim 16, VHD gain 17, drive 18, cue 19/20, aux 21/22, switches 80-96, the slot's 500 module from CC 102). Stereo channels, returns, the centre section, the bus compressor, monitoring and the transport sit on CCs 40-63; the full table is on the MIDI tab.</li>
<li><b>Learn</b> in the header: tap a control, move a knob, fader or button on your controller. Learned controls override the default map.</li>
<li>Feedback sends every change back out so motorised faders and LED rings follow. Pitch bend moves faders with 14-bit resolution (Mackie-style).</li>
<li>Program change recalls a desk snapshot (save them on the MIDI tab).</li>
<li><b>MMC</b>: receives stop, play, deferred play, fast forward, rewind, record strobe, record exit, pause and locate (any frame rate), on device ID 127 (all call) or the ID you set. The transport sends MMC out, including locate at 30 fps, so the desk can drive your DAW. Chrome needs SysEx permission for MMC.</li>
</ul>
<h3>Where the numbers come from</h3>
<ul>
<li><b>XL-Desk Owner's Manual</b>, SSL 82BMLM01B (May 2017), <a href="https://solidstatelogic.com/assets/uploads/downloads/xl-desk/SSL-XL-Desk-Owners-Manual.pdf">solidstatelogic.com</a>: the signal flow block diagram (Appendix A, "XL DESK Analogue Overview Rev 1.11"), VHD gain to +75 dB, 20 dB pad, HI-Z 1.2 kΩ → 10 kΩ, trim ±20 dB, the tri-colour LED points (green -24 dBu, yellow +4 / 0 dBu, red +21 / +16 dBu on the +24 / +18 scales), DAW / Ø / 500 / INS / CHOP POST, cue pre/post and ALT, aux pre switching, the slot-stealing pairs (A 9-10, B 11-12, C 13-14, D 15-16), the centre-detent +10 dB masters, Mix A's 0 dB-at-the-top fader, SUM and TO A, solo-in-place / AFL / safe, three speaker sets, bass management (3rd-order 80 Hz low-pass to the sub, 2nd-order high-pass on the speakers), the MINI trim procedure, all twelve DIP switches, 20-segment bus meters, and the specs: frequency response 20 Hz-40 kHz, THD+N &lt; 0.05%, noise &lt; -86 dBu channel / &lt; -83 dBu mix, headroom &gt; +24 dBu.</li>
<li><b>G Series Bus Compressor module user guide</b> (V2.0, 2020), <a href="https://www.solidstatelogic.com/assets/uploads/downloads/SSL_500_Series_G_Comp_Module_User_Guide.pdf">solidstatelogic.com</a>: every switch position (attack .1-30 ms, release .1-1.6 s and AUTO, ratio 1.5-10, sidechain HPF off/30/60/105/125/185 Hz, threshold ±20 dB), the dominant true-peak sidechain, and the threshold that moves with ratio.</li>
<li><b>E Series EQ module user guide</b>, <a href="https://www.solidstatelogic.com/assets/uploads/downloads/SSL_500_Series_E_EQ_Module_User_Guide.pdf">solidstatelogic.com</a>: Brown (02) and Black (242) EQs, ±15 / ±18 dB, 6 dB/octave shelves with BELL, constant-interval mid bands, and the band ranges on the faceplate (HF 1.5-16 kHz, HMF 0.6-7 kHz, LMF 0.2-2.5 kHz, LF 30-450 Hz).</li>
<li><b>E Series Dynamics module user guide</b>, <a href="https://www.solidstatelogic.com/assets/uploads/downloads/500-mods/E-Series_Dynamics_for_500_Format_User_Guide.pdf">solidstatelogic.com</a>: RMS sidechain, over-easy / hard knee, auto make-up, LIN REL (+6 dB threshold), attack 30 ms / 3 ms per 20 dB, release 0.1-4 s, 20:1 gate or 2:1 expander, 40 dB range, gate attack 1.5 ms / 100 µs per 40 dB, threshold hysteresis.</li>
<li><a href="https://www.soundonsound.com/reviews/ssl-xl-desk">Sound On Sound's XL-Desk review</a>: VHD gain from +15 dB, the MDAC faders, the module layout.</li>
</ul>
<h3>What had to be estimated</h3>
<p>SSL has not published component-level schematics for the XL-Desk, so these are modelled from how the circuits are described and how this kind of circuit behaves, not measured from a desk:</p>
<ul>
<li>The VHD transfer curve (2nd harmonic from an asymmetric stage, 3rd from a symmetric one, blended by the black pot, growing with drive level).</li>
<li>Pot tapers: fader scale marks, cue / aux sends (unity at 3 o'clock, +6 dB max), Q range 0.5-2.5, the Black EQ's Q-with-gain and LF control law, BELL Q.</li>
<li>Pan law (-3 dB constant power) and the stereo return mono sum.</li>
<li>Bus compressor knee width, how far the threshold moves with ratio, the AUTO release's two time constants, make-up range (-5 to +15 dB), sidechain HPF slope (12 dB/octave), and the feed-forward topology (sources disagree on feedback vs feed-forward).</li>
<li>The listen-mic compressor's fixed settings (fast attack, about 120 ms release, 20:1).</li>
<li>Bargraph segment spacing below 0 dB, and the rail clip shape around +27 dBu.</li>
</ul>
<p>Noise is modelled at the published figures (switch it off on the Rear panel tab). Levels: 0 dBFS = +24 dBu, so +4 dBu line-up is -20 dBFS.</p>
</div>`;
}
