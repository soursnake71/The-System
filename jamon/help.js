/* In-app help. One section per screen; the ? button opens the section for the screen you are on. */
const HELP = [
  ['start', 'Getting started', `<ol><li>Pick a song in the top bar (seven styles from the album, or your own) and press <b>▶</b>. Space bar also plays and stops.</li>
    <li>The tabs along the top are the machines. Each machine has three views: <b>Pattern</b> (the notes or steps), <b>Sound</b> (its front panel) and <b>Play</b> (keys or pads).</li>
    <li>Want a whole new sound at once? On the <b>Song</b> tab pick a <b>Rig preset</b>: it sets every machine, the vocoder and the mix.</li>
    <li>Add it to your home screen from Chrome's menu and it works offline.</li></ol>`],
  ['song', 'Songs and sections', `<p>A song is a list of <b>sections</b> (Intro, Groove, Break…) played in order. Each section has its length in bars and how many times it repeats (×).</p>
    <ul><li>Tap a section to edit it; every machine's Pattern view then shows that section.</li><li>↑ moves it earlier, ⧉ copies it, ✕ deletes it.</li>
    <li><b>Loop sec</b> (under ⋯) keeps playing the section you are editing.</li><li><b>Rig preset</b> sets the sound of everything; <b>Reset the 7 styles</b> brings back the album-style songs.</li></ul>`],
  ['drums', 'TR-808 and DMX', `<ul><li><b>Pattern</b>: one row per drum, 16 steps per bar. Tap a step for a soft hit, again for an accent, again for off. Pick the bar with the Bar buttons.</li>
    <li><b>Sound</b>: the machine's own knobs. On the 808 the tom / conga, rim shot / claves and clap / maracas pairs share their knobs, like the real one.</li>
    <li><b>Play</b>: tap the pads. With <b>● Notes</b> on (under ⋯) while the song plays, what you tap is written into the pattern.</li>
    <li>Hats and cymbals go to the <b>Cymbal bus</b> so they can get a phaser or flanger (Mix FX).</li></ul>`],
  ['tb303', 'TB-303', `<p>The bass line machine. In the Pattern view draw notes, then mark them with <b>Accent</b> (louder, brighter, the squelch) and <b>Slide</b> (glides into the next note). Cutoff, Resonance, Env Mod and Decay make the acid sound; Accent sets how hard accented notes hit.</p>`],
  ['synth', 'Pro-One, SH-101, Juno-60, RS-09', `<ul><li><b>Pattern</b> is a piano roll: <b>Draw</b> adds a note of the chosen Length (tap it again to remove it), <b>Accent</b> and <b>Slide</b> mark notes, ▲▼ move the view, <b>Text</b> shows the section as notation you can edit or paste (E2, E3+G3+B3 for a chord, . rest, - hold, ! accent, ~ slide).</li>
    <li><b>Sound</b>: the front panel, one section at a time (tap the names above the knobs). Drag a knob up or down, double-tap to centre it.</li>
    <li><b>Arp</b>: up, down, up-down or random over 1-3 octaves; hold a chord (or use <b>Hold</b> on the keys) and it plays in time.</li>
    <li><b>Play</b>: the keyboard; ◀ ▶ change octave.</li></ul>`],
  ['pbass', 'Chilly B Bass', `<p>A Fender Precision simulator: a modelled string, the split pickup and the passive tone control. <b>Playing</b> picks finger, pick, slap or palm mute; <b>Pluck Pos</b> moves your hand toward the neck or bridge; <b>Fret Buzz</b> and <b>Strings</b> (old or new) add character. Volume and Tone are the bass's own two knobs.</p>`],
  ['voc', 'Vocoder, speech and chipmunk voices', `<ul><li>The Electro-Harmonix vocoder makes a synth (the <b>carrier</b>, picked under Music input) talk with the speech or your mic.</li>
    <li><b>Speech</b>: type words and press <b>Say it</b>; it plays a chord on the carrier while it talks. Type /phonemes/ between slashes to spell a word exactly. Lines added under "Spoken in a section" are said at the start of their bar while the song plays.</li>
    <li><b>Live mic</b> uses your voice instead (use headphones).</li>
    <li><b>Chipmunk voice</b> puts the mic and the speech, sped up like tape, on the <b>Voice</b> channel. Pick the tape speed beside it (×2 is the classic sound).</li>
    <li>The carrier is only heard through the vocoder unless <b>Carrier also dry</b> is on.</li></ul>`],
  ['fx', 'Mix FX and dub', `<ul><li><b>Whole mix</b> sets every effect and send at once in a 1984 style.</li>
    <li><b>Channel</b>: pick a channel, give it an <b>insert</b> (Chipmunk, Phase 90, Small Stone, Electric Mistress, A/DA Flanger, 1176) and set its <b>sends</b> to the five rack effects.</li>
    <li><b>Rack</b>: the shared effects: PrimeTime echo, Effectron, H910 Harmonizer, Lexicon 224 / AMS reverbs and the gated room. Each has presets.</li>
    <li><b>Dub</b>: hold <b>Throw</b> to fire a channel into the echo, hold <b>Drop</b> to cut it. Map them to pads with MIDI Learn. They are live moves, so use REC to capture them.</li></ul>`],
  ['mix', 'Mixer', `<p>Swipe sideways through the channels like a desk: fader, pan, mute (M) and solo (S) for every machine, the Cymbal bus, the Voice channel and the Vocoder, then the effect returns and the master.</p>`],
  ['midi', 'MIDI', `<ul><li><b>Connect</b>: plug a USB MIDI keyboard or MPC into the phone (USB-C), then Song → MIDI input → <b>Connect MIDI</b>. Bluetooth MIDI works once it is paired in Android settings.</li>
    <li>Notes play <b>the machine on screen</b>, or choose <b>by channel</b> and set each machine's channel (drums use General MIDI note numbers).</li>
    <li>Understood: velocity, sustain pedal, pitch bend, mod wheel, volume (CC7), pan (CC10), program change (picks presets), all notes off.</li>
    <li><b>MIDI Learn</b> (⋯ → Learn): tap any knob, slider, switch, mixer control, dub pad or Play, then move a control on your device. A dot marks learned controls; remove them under Song → MIDI input.</li>
    <li><b>Sync</b>: "Follow MIDI clock in" takes tempo, start, stop and song position from your MPC or DAW. Pick an <b>Out</b> to send clock and transport, and every machine's notes, to other gear.</li></ul>`],
  ['files', 'Saving and exporting', `<ul><li>Everything is saved on the phone as you work.</li>
    <li>Song → Files: <b>Export WAV</b> renders the whole song with its effects; <b>Export MIDI</b> writes one track per machine; <b>Import MIDI</b> turns a MIDI file into a new song (you pick a machine for each track); <b>Save / Open project</b> keeps everything in one file.</li>
    <li><b>REC</b> (under ⋯) records what you hear, live moves included, to a WAV.</li></ul>`],
  ['about', 'Credits', `<p>Newcleus: Cozmo D on keys, Chilly B on bass, and Lady E. The songs here are new arrangements in the style of <i>Jam On Revenge</i>, not the records. The machines are circuit models built from published documentation; the presets are programmed for this rig.</p>`]
];
const HELP_FOR = {song:'song', d808:'drums', dmx:'drums', tb303:'tb303', proone:'synth', sh101:'synth', juno:'synth', rs09:'synth', pbass:'pbass', voc:'voc', fx:'fx', mix:'mix', about:'about'};
