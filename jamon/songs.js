/* Seven arrangements in the style of the Jam On Revenge tracks (Where's the Beat? left out, as asked).
   These are original patterns and original spoken lines, written for this rig: each one sets up the gear the way the
   record's style suggests (tempo feel, drum machine, which machine plays bass, arps, leads, vocoder), not a transcription
   of the songs. Tempos and keys are starting points; change anything.
   Line notation, one token per 16th: "E2" note, "E3+G3+B3" chord, "." rest, "-" hold the previous note,
   "!" in front = accent, "~" after = slide into the next note. Lines and drum strings repeat to fill the section. */
const hold = (chord, steps) => [chord].concat(Array(steps - 1).fill('-')).join(' ');
const bars = (...b) => b.join(' ');
const DMX_ELECTRO = {bd:'x.....x...x.....', sd:'....x.......x...', chh:'x.o.x.o.x.o.x.o.', clap:'....x.......x...'};
const SONGS = [
  {name:'Computer Age (style)', bpm:112, swing:0, key:'E minor', note:'DMX electro, Pro-One sync bass, Juno into the vocoder, SH-101 zaps, RS-09 strings',
    presets:{proone:'Electro Bass (sync)', juno:'Vocoder Carrier', sh101:'Electro Pluck', rs09:'Strings', dmx:'Punchy', voc:'Electro Robot'}, carrier:'juno',
    sections:[
      {name:'Intro', bars:4, dmx:{chh:'x.o.x.o.x.o.x.o.', clap:'..............x.'}, rs09:bars(hold('E3+G3+B3', 32), hold('D3+F#3+A3', 32)), say:[[0, 'system online.'], [2, 'all circuits ready.']]},
      {name:'Groove', bars:4, rep:2, dmx:DMX_ELECTRO, proone:bars('E1 . E2 . . E1 . E1 . . G1 . A1 . B1 .', 'E1 . E2 . . E1 . E1 . . D2 . B1 . G1 .'), sh101:'. . . . !E5 . . . . . . . !B4 . . .'},
      {name:'Vocoder', bars:4, rep:2, dmx:Object.assign({}, DMX_ELECTRO, {ohh:'..............o.'}), proone:bars('E1 . E2 . . E1 . E1 . . G1 . A1 . B1 .', 'E1 . E2 . . E1 . E1 . . D2 . B1 . G1 .'),
        juno:bars(hold('E3+G3+B3', 16), hold('D3+F#3+A3', 16), hold('C3+E3+G3', 16), hold('D3+F#3+A3', 16)), say:[[0, 'the age of machines.'], [1, 'press the key.'], [2, 'feel the voltage.'], [3, 'and move your feet.']]},
      {name:'Break', bars:2, dmx:{bd:'x.....x...x.....', t1:'........x.x.....', t2:'............x.x.', clap:'....x.......x...'}, sh101:'!E5 . . !E5 . . !B4 . . . !E5 . . . !G5 .', rs09:hold('C3+E3+G3', 32)},
      {name:'Groove 2', bars:4, rep:2, dmx:Object.assign({}, DMX_ELECTRO, {tamb:'..x...x...x...x.'}), proone:bars('E1 . E2 . . E1 . E1 . . G1 . A1 . B1 .', 'E1 . E2 . . E1 . E1 . . D2 . B1 . G1 .'),
        juno:bars(hold('E3+G3+B3', 32), hold('C3+E3+G3', 32)), sh101:'. . . . !E5 . . . . . . . !B4 . . .', say:[[0, 'computer age.'], [2, 'computer age.']]},
      {name:'Outro', bars:4, dmx:{chh:'x.o.x.o.x.o.x.o.', bd:'x...............'}, rs09:hold('E3+G3+B3', 64), say:[[0, 'system shutting down.']]}]},

  {name:'Automan (style)', bpm:116, swing:0, key:'A minor', note:'DMX, TB-303 acid bass, Juno stabs, Pro-One sync solo, RS-09 strings carrying the vocoder',
    presets:{tb303:'Acid Bass', juno:'Electro Brass Stab', proone:'Sync Scream Solo', rs09:'Strings', dmx:'Low and Heavy', voc:'Deep Machine'}, carrier:'rs09',
    sections:[
      {name:'Intro', bars:4, dmx:{bd:'x.....x.x.....x.', chh:'x.o.x.o.x.o.x.o.'}, tb303:bars('!A1 . A2 . A1~ C2 . A1 . . G1 . !A1 . E2 .', 'A1 . A2 A1 . . C2~ D2 . A1 . G1 . !E2 . .')},
      {name:'Groove', bars:4, rep:2, dmx:{bd:'x.....x.x.....x.', sd:'....x.......x...', chh:'x.o.x.o.x.o.x.o.', clap:'....x.......x...'}, tb303:bars('!A1 . A2 . A1~ C2 . A1 . . G1 . !A1 . E2 .', 'A1 . A2 A1 . . C2~ D2 . A1 . G1 . !E2 . .'),
        juno:'. . . A3+C4+E4 . . . . . . G3+B3+D4 . . . . .'},
      {name:'Vocoder', bars:4, rep:2, dmx:{bd:'x.....x.x.....x.', sd:'....x.......x...', chh:'x.o.x.o.x.o.x.o.'}, tb303:bars('!A1 . A2 . A1~ C2 . A1 . . G1 . !A1 . E2 .', 'A1 . A2 A1 . . C2~ D2 . A1 . G1 . !E2 . .'),
        rs09:bars(hold('A2+C3+E3', 32), hold('G2+B2+D3', 16), hold('F2+A2+C3', 16)), say:[[0, 'the man of steel and wire.'], [1, 'he never sleeps.'], [2, 'he walks the night.'], [3, 'he keeps the time.']]},
      {name:'Solo', bars:4, dmx:{bd:'x.....x.x.....x.', sd:'....x.......x...', chh:'x.x.x.x.x.x.x.x.', ohh:'..............x.', clap:'....x.......x...'}, tb303:'!A1 . A2 . A1~ C2 . A1 . . G1 . !A1 . E2 .',
        proone:bars('A4 - - C5 - D5 - E5 - - - - D5 - C5 -', 'A4 - - - - - - - . . G4 - A4 - C5 -', 'E5 - - - D5 - C5 - D5 - - - - - . .', 'C5 - A4 - G4 - E4 - G4 - A4 - - - - -'), juno:'. . . A3+C4+E4 . . . . . . G3+B3+D4 . . . . .'},
      {name:'Break', bars:2, dmx:{t4:'x.....x.........', t3:'........x...x...', t2:'..........x...x.', t1:'............x.x.'}, tb303:'A1 . . . . . . . . . . . . . . .'},
      {name:'Groove 2', bars:4, rep:2, dmx:{bd:'x.....x.x.....x.', sd:'....x.......x...', chh:'x.o.x.o.x.o.x.o.', clap:'....x.......x...', cym:'x...............................................................'},
        tb303:bars('!A1 . A2 . A1~ C2 . A1 . . G1 . !A1 . E2 .', 'A1 . A2 A1 . . C2~ D2 . A1 . G1 . !E2 . .'), juno:'. . . A3+C4+E4 . . . . . . G3+B3+D4 . . . . .', rs09:hold('A2+C3+E3', 64), say:[[0, 'automatic.'], [2, 'automatic.']]}]},

  {name:'I\'m Not a Robot (style)', bpm:108, swing:8, key:'D minor', note:'DMX, Fender finger bass, RS-09 organ and strings, SH-101 arpeggio, Juno into the vocoder',
    presets:{pbass:'Finger Funk', rs09:'Organ + Strings', sh101:'Squelch Arp', juno:'Vocoder Carrier', dmx:'Punchy', voc:'Clear Speech'}, carrier:'juno', arp:{sh101:{mode:'up', oct:2, rate:16}},
    sections:[
      {name:'Intro', bars:4, dmx:{chh:'x.x.x.x.x.x.x.x.'}, rs09:bars(hold('D3+F3+A3', 32), hold('A#2+D3+F3', 16), hold('C3+E3+G3', 16)), sh101:hold('D3+F3+A3', 64)},
      {name:'Groove', bars:4, rep:2, dmx:{bd:'x.......x.x.....', sd:'....x.......x...', chh:'x.x.x.x.x.x.x.x.', ohh:'......o.......o.', clap:'............x...'},
        pbass:bars('D1 . . D2 . D1 . . F1 . G1 . A1 . C2 .', 'D1 . . D2 . D1 . . C2 . A1 . G1 . F1 .'), rs09:bars(hold('D3+F3+A3', 32), hold('A#2+D3+F3', 16), hold('C3+E3+G3', 16))},
      {name:'Vocoder', bars:4, rep:2, dmx:{bd:'x.......x.x.....', sd:'....x.......x...', chh:'x.x.x.x.x.x.x.x.', clap:'............x...'}, pbass:bars('D1 . . D2 . D1 . . F1 . G1 . A1 . C2 .', 'D1 . . D2 . D1 . . C2 . A1 . G1 . F1 .'),
        juno:bars(hold('D3+F3+A3', 32), hold('A#2+D3+F3', 16), hold('C3+E3+G3', 16)), say:[[0, 'circuits feel the rhythm.'], [1, 'my heart is made of wire.'], [2, 'listen to the signal.'], [3, 'can you hear me now?']]},
      {name:'Arp break', bars:4, dmx:{chh:'x.x.x.x.x.x.x.x.', clap:'....x.......x...'}, sh101:bars(hold('D3+F3+A3', 32), hold('A#2+D3+F3', 16), hold('C3+E3+G3', 16)), pbass:'D1 . . . . . . . . . . . . . . .'},
      {name:'Groove 2', bars:4, rep:2, dmx:{bd:'x.......x.x.....', sd:'....x.......x...', chh:'x.x.x.x.x.x.x.x.', ohh:'......o.......o.', tamb:'....x.......x...'},
        pbass:bars('D1 . . D2 . D1 . . F1 . G1 . A1 . C2 .', 'D1 . . D2 . D1 . . C2 . A1 . G1 . F1 .'), sh101:bars(hold('D3+F3+A3', 32), hold('A#2+D3+F3', 16), hold('C3+E3+G3', 16)),
        juno:bars(hold('D3+F3+A3', 32), hold('C3+E3+G3', 32)), say:[[0, 'not a machine.'], [2, 'i have a soul.']]}]},

  {name:'Destination Earth (style)', bpm:118, swing:0, key:'F# minor', note:'TR-808, Pro-One arpeggio, TB-303 square sub, RS-09 slow strings, SH-101 solo, vocoder on the strings',
    presets:{proone:'Space Arp', tb303:'Square Sub Bass', rs09:'Slow Strings', sh101:'Solo Lead', d808:'Electro Boom', voc:'Electro Robot'}, carrier:'rs09', arp:{proone:{mode:'updown', oct:2, rate:16}},
    sections:[
      {name:'Launch', bars:4, d808:{cy:'x...............................................................', ma:'..x...x...x...x.'}, rs09:bars(hold('F#2+A2+C#3', 32), hold('D2+F#2+A2', 32)), say:[[0, 'leaving orbit.'], [2, 'coordinates set.']]},
      {name:'Groove', bars:4, rep:2, d808:{bd:'x......x..x.....', sd:'....x.......x...', ch:'x.x.x.x.x.x.x.x.', cp:'....x.......x...', cb:'...........x....'},
        tb303:'F#1 . . . . . F#1 . . . F#1 . . . E1 .', proone:bars(hold('F#3+A3+C#4', 32), hold('D3+F#3+A3', 16), hold('E3+G#3+B3', 16))},
      {name:'Transmission', bars:4, rep:2, d808:{bd:'x......x..x.....', sd:'....x.......x...', ch:'x.x.x.x.x.x.x.x.', oh:'..............x.', cp:'....x.......x...'}, tb303:'F#1 . . . . . F#1 . . . F#1 . . . E1 .',
        rs09:bars(hold('F#2+A2+C#3', 32), hold('D2+F#2+A2', 16), hold('E2+G#2+B2', 16)), say:[[0, 'calling planet earth.'], [1, 'do you read me?'], [2, 'we are coming home.'], [3, 'prepare to land.']]},
      {name:'Solo', bars:4, d808:{bd:'x......x..x.....', sd:'....x.......x...', ch:'x.x.x.x.x.x.x.x.', cp:'....x.......x...', cb:'...........x....'}, tb303:'F#1 . . . . . F#1 . . . F#1 . . . E1 .',
        sh101:bars('F#4 - - - A4 - B4 - C#5 - - - - - - -', 'E5 - - - C#5 - B4 - A4 - - - B4 - - -', 'C#5 - - - - - - - . . F#4 - A4 - C#5 -', 'B4 - - - - - - - - - - - . . . .'), proone:hold('F#3+A3+C#4', 64)},
      {name:'Re-entry', bars:2, d808:{lt:'x.....x.........', mt:'........x...x...', ht:'..........x...x.', bd:'x...............'}, rs09:hold('D2+F#2+A2', 32)},
      {name:'Touchdown', bars:4, rep:2, d808:{bd:'x......x..x.....', sd:'....x.......x...', ch:'x.x.x.x.x.x.x.x.', cp:'....x.......x...', cb:'...........x....', cy:'x...............................................................'},
        tb303:'F#1 . . . . . F#1 . . . F#1 . . . E1 .', proone:bars(hold('F#3+A3+C#4', 32), hold('D3+F#3+A3', 16), hold('E3+G#3+B3', 16)), rs09:hold('F#2+A2+C#3', 64), say:[[0, 'destination reached.']]}]},

  {name:'Jam On Revenge (style)', bpm:112, swing:10, key:'G minor', note:'DMX, Fender slap bass, sped-up chipmunk voices, Pro-One lead riff, Juno brass stabs',
    presets:{pbass:'Slap', proone:'Wikki Lead', juno:'Electro Brass Stab', dmx:'Punchy', voc:'Chipmunk'}, carrier:'juno',
    sections:[
      {name:'Intro', bars:2, dmx:{chh:'x.x.x.x.x.x.x.x.', clap:'....x.......x...'}, pbass:'!G1 . G2 . . G1 G2 . !G1 . . F2 . G2 . D2', say:[[0, 'scratch it up.']]},
      {name:'Groove', bars:4, rep:2, dmx:{bd:'x.....x...x..x..', sd:'....x.......x...', chh:'x.x.x.x.x.x.x.x.', clap:'....x.......x...', ohh:'..............o.'},
        pbass:bars('!G1 . G2 . . G1 G2 . !G1 . . F2 . G2 . D2', '!G1 . G2 . . G1 G2 . !A#1 . . C2 . D2 . F1'), juno:bars('G3+A#3+D4 . . . . . . . . . . . . . . .', 'F3+A3+C4 . . . . . . . . . . . . . . .')},
      {name:'Riff', bars:4, rep:2, dmx:{bd:'x.....x...x..x..', sd:'....x.......x...', chh:'x.x.x.x.x.x.x.x.', clap:'....x.......x...'}, pbass:bars('!G1 . G2 . . G1 G2 . !G1 . . F2 . G2 . D2', '!G1 . G2 . . G1 G2 . !A#1 . . C2 . D2 . F1'),
        proone:'. . G4 A#4 C5 . A#4 G4 . . F4 G4 - - . .'},
      {name:'Little voices', bars:4, rep:2, dmx:{bd:'x.....x...x..x..', sd:'....x.......x...', chh:'x.x.x.x.x.x.x.x.', clap:'....x.......x...'}, pbass:bars('!G1 . G2 . . G1 G2 . !G1 . . F2 . G2 . D2', '!G1 . G2 . . G1 G2 . !A#1 . . C2 . D2 . F1'),
        juno:bars(hold('G3+A#3+D4', 16), hold('F3+A3+C4', 16)), say:[[0, 'turn the tables.'], [1, 'tiny voices.'], [2, 'speed it up.'], [3, 'say it again.']]},
      {name:'Break', bars:2, dmx:{bd:'x...............', t1:'....x.x.........', t2:'........x.x.....', t3:'............x.x.'}, pbass:'!G1 . . . . . . . . . . . . . . .'},
      {name:'Riff 2', bars:4, rep:2, dmx:{bd:'x.....x...x..x..', sd:'....x.......x...', chh:'x.x.x.x.x.x.x.x.', clap:'....x.......x...', tamb:'..x...x...x...x.'},
        pbass:bars('!G1 . G2 . . G1 G2 . !G1 . . F2 . G2 . D2', '!G1 . G2 . . G1 G2 . !A#1 . . C2 . D2 . F1'), proone:'. . G4 A#4 C5 . A#4 G4 . . F4 G4 - - . .', juno:bars('G3+A#3+D4 . . . . . . . . . . . . . . .', 'F3+A3+C4 . . . . . . . . . . . . . . .')}]},

  {name:'Jam On It (style)', bpm:110, swing:6, key:'C minor', note:'DMX, Fender finger bass, Juno arpeggio, Pro-One laser zaps, RS-09 strings into the vocoder',
    presets:{pbass:'Finger Funk', juno:'Juno Arp Pluck', proone:'Laser Zap', rs09:'Strings', dmx:'Punchy', voc:'Electro Robot'}, carrier:'rs09', arp:{juno:{mode:'up', oct:2, rate:16}},
    sections:[
      {name:'Intro', bars:4, dmx:{chh:'x.o.x.o.x.o.x.o.'}, juno:bars(hold('C3+D#3+G3+A#3', 32), hold('G#2+C3+D#3+G3', 32)), proone:'. . . . . . . . . . . . . . C6 .'},
      {name:'Groove', bars:4, rep:2, dmx:{bd:'x.....x.x.....x.', sd:'....x.......x...', chh:'x.o.x.o.x.o.x.o.', clap:'....x.......x...'},
        pbass:bars('C2 . . C2 . . G1 . A#1 . . C2 . D#2 . F2', 'C2 . . C2 . . G1 . A#1 . . G1 . F1 . D#1'), juno:bars(hold('C3+D#3+G3+A#3', 32), hold('G#2+C3+D#3+G3', 32))},
      {name:'Vocoder', bars:4, rep:2, dmx:{bd:'x.....x.x.....x.', sd:'....x.......x...', chh:'x.o.x.o.x.o.x.o.', clap:'....x.......x...'}, pbass:bars('C2 . . C2 . . G1 . A#1 . . C2 . D#2 . F2', 'C2 . . C2 . . G1 . A#1 . . G1 . F1 . D#1'),
        rs09:bars(hold('C3+D#3+G3', 32), hold('G#2+C3+D#3', 32)), say:[[0, 'on the beat.'], [1, 'keep it moving.'], [2, 'electric feeling.'], [3, 'all night long.']]},
      {name:'Zap break', bars:2, dmx:{bd:'x.....x.x.....x.', clap:'....x.......x...'}, proone:'. . C6 . . . G5 . . . C6 . . . D#6 .', pbass:'C2 . . . . . . . . . . . . . . .'},
      {name:'Groove 2', bars:4, rep:2, dmx:{bd:'x.....x.x.....x.', sd:'....x.......x...', chh:'x.o.x.o.x.o.x.o.', clap:'....x.......x...', ohh:'..............o.'},
        pbass:bars('C2 . . C2 . . G1 . A#1 . . C2 . D#2 . F2', 'C2 . . C2 . . G1 . A#1 . . G1 . F1 . D#1'), juno:bars(hold('C3+D#3+G3+A#3', 32), hold('G#2+C3+D#3+G3', 32)), rs09:hold('C3+D#3+G3', 64), say:[[0, 'jam on.'], [2, 'jam on.']]}]},

  {name:'No More Runnin\' (style)', bpm:100, swing:0, key:'B-flat minor', note:'TR-808 with cowbell, Pro-One fat bass, RS-09 slow strings, SH-101 PWM solo, deep vocoder',
    presets:{proone:'Fat Unison Bass', rs09:'Slow Strings', sh101:'PWM Lead', d808:'Long Boom', voc:'Deep Machine'}, carrier:'rs09',
    sections:[
      {name:'Intro', bars:4, d808:{ch:'x.x.x.x.x.x.x.x.', cb:'..x.....x.......'}, rs09:bars(hold('A#2+C#3+F3', 32), hold('F#2+A#2+C#3', 32))},
      {name:'Groove', bars:4, rep:2, d808:{bd:'x.......x.x.....', sd:'....x.......x...', ch:'x.x.x.x.x.x.x.x.', cb:'..x.....x.......', cl:'......x.........'},
        proone:bars('A#1 - - . A#1 . . F1 G#1 - - . G#1 . F#1 .', 'A#1 - - . A#1 . . F1 G#1 - - . F1 . C#1 .'), rs09:bars(hold('A#2+C#3+F3', 32), hold('F#2+A#2+C#3', 32))},
      {name:'Vocoder', bars:4, rep:2, d808:{bd:'x.......x.x.....', sd:'....x.......x...', ch:'x.x.x.x.x.x.x.x.', oh:'..............x.'}, proone:bars('A#1 - - . A#1 . . F1 G#1 - - . G#1 . F#1 .', 'A#1 - - . A#1 . . F1 G#1 - - . F1 . C#1 .'),
        rs09:bars(hold('A#2+C#3+F3', 32), hold('F#2+A#2+C#3', 32)), say:[[0, 'slow it down.'], [1, 'stand your ground.'], [2, 'the city sleeps.'], [3, 'we keep moving.']]},
      {name:'Solo', bars:4, d808:{bd:'x.......x.x.....', sd:'....x.......x...', ch:'x.x.x.x.x.x.x.x.', cb:'..x.....x.......'}, proone:bars('A#1 - - . A#1 . . F1 G#1 - - . G#1 . F#1 .', 'A#1 - - . A#1 . . F1 G#1 - - . F1 . C#1 .'),
        sh101:bars('F4 - - - G#4 - A#4 - - - - - G#4 - F4 -', 'C#5 - - - C5 - A#4 - G#4 - - - - - - -', 'F4 - G#4 - A#4 - C#5 - D#5 - - - C#5 - A#4 -', 'A#4 - - - - - - - - - - - . . . .'), rs09:hold('A#2+C#3+F3', 64)},
      {name:'Outro', bars:4, d808:{bd:'x.......x.x.....', ch:'x.x.x.x.x.x.x.x.', cb:'..x.....x.......', cy:'x...............................................................'}, rs09:hold('A#2+C#3+F3', 64), say:[[0, 'no more.']]}]}
];
