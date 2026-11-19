/* Omega 808 presets: kits (sound settings), rhythms (patterns), banks (12 basic + 4 fills) and FX (Rev7 + TC 2290).
   All the patterns are original. Keys and pattern format follow CONTRACT.md. This is a plain script: it defines
   EIGHT_KITS, EIGHT_RHYTHMS, EIGHT_BANKS and EIGHT_FX as globals, and module.exports for Node.
   Units: knobs, mods and sends 0..1. *.sel 0/1. mod.TRIG.src 0..3. mod.TRIG.width in ms. rev.time in s.
   rev.predelay and dly.time in ms. rev.lpf / rev.hpf / dly.hicut / dly.lowcut in Hz. dly.modRate in Hz. */
(function (g) {
'use strict';

/* ---------------- kits ---------------- */
// stock: the knob positions in the faceplate photo
const STOCK = {
  master: 0.75, 'AC.level': 0.6,
  'BD.level': 0.65, 'BD.tone': 0.55, 'BD.decay': 0.6,
  'SD.level': 0.65, 'SD.tone': 0.55, 'SD.snappy': 0.6,
  'LT.level': 0.6, 'LT.tuning': 0.5, 'LT.sel': 0, 'MT.level': 0.6, 'MT.tuning': 0.5, 'MT.sel': 0, 'HT.level': 0.6, 'HT.tuning': 0.5, 'HT.sel': 0,
  'RS.level': 0.6, 'RS.sel': 0, 'CP.level': 0.6, 'CP.sel': 0, 'CB.level': 0.6,
  'CY.level': 0.6, 'CY.tone': 0.55, 'CY.decay': 0.6, 'OH.level': 0.6, 'OH.decay': 0.4, 'CH.level': 0.65
};
const KITS = [];
const kit = (cat, name, p) => KITS.push({name, cat, params: Object.assign({}, STOCK, p)});

// Stock
kit('Stock', 'Factory 808', {});
kit('Stock', 'Factory, Levels Flat', {'BD.level': 0.6, 'SD.level': 0.6, 'CH.level': 0.6});
kit('Stock', 'Studio Room 1982', {'rev.send.SD': 0.18, 'rev.send.CP': 0.22, 'rev.program': 2, 'rev.time': 0.9, 'rev.return': 0.5});
kit('Stock', 'Tight Factory', {'BD.decay': 0.35, 'OH.decay': 0.25, 'CY.decay': 0.35, 'SD.snappy': 0.5});
kit('Stock', 'Open Factory', {'BD.decay': 0.8, 'OH.decay': 0.6, 'CY.decay': 0.85});
kit('Stock', 'Congas Switched In', {'LT.sel': 1, 'MT.sel': 1, 'HT.sel': 1});
kit('Stock', 'Claves and Maracas', {'RS.sel': 1, 'CP.sel': 1});
kit('Stock', 'All Alternates', {'LT.sel': 1, 'MT.sel': 1, 'HT.sel': 1, 'RS.sel': 1, 'CP.sel': 1});
kit('Stock', 'Bright Factory', {'BD.tone': 0.8, 'SD.tone': 0.8, 'CY.tone': 0.8});
kit('Stock', 'Dark Factory', {'BD.tone': 0.2, 'SD.tone': 0.25, 'CY.tone': 0.25});
kit('Stock', 'Accent Heavy', {'AC.level': 0.95, 'mod.AC.depth': 0.7});
kit('Stock', 'Accent Light', {'AC.level': 0.3});

// Electro
kit('Electro', 'Planet Rock Box', {'BD.decay': 0.75, 'BD.tone': 0.4, 'CP.level': 0.75, 'rev.send.CP': 0.35, 'rev.send.SD': 0.2, 'rev.program': 6, 'rev.time': 1.8, 'rev.return': 0.55, 'CB.level': 0.7, 'swing': 0});
kit('Electro', 'Breakdance Floor', {'BD.decay': 0.7, 'SD.snappy': 0.75, 'CP.level': 0.7, 'rev.send.CP': 0.3, 'rev.program': 0, 'rev.time': 2.2, 'rev.return': 0.5});
kit('Electro', 'Robot Funk', {'CB.level': 0.8, 'mod.CB.tune': 0.55, 'dly.send.CB': 0.3, 'dly.sync': 1, 'dly.division': 3, 'dly.feedback': 0.35, 'dly.return': 0.45});
kit('Electro', 'Zapper Toms', {'mod.TOM.sweep': 0.8, 'mod.TOM.decay': 0.65, 'HT.tuning': 0.7, 'LT.tuning': 0.35});
kit('Electro', 'Freestyle Clap', {'CP.level': 0.85, 'mod.CP.tail': 0.65, 'mod.CP.spread': 0.6, 'rev.send.CP': 0.4, 'rev.program': 6, 'rev.time': 2.5, 'rev.return': 0.55});
kit('Electro', 'Electric Kingdom', {'BD.decay': 0.9, 'mod.BD.sweep': 0.6, 'SD.tone': 0.7, 'rev.send.SD': 0.3, 'rev.program': 1, 'rev.time': 2.8, 'rev.return': 0.5});
kit('Electro', 'Pop-Lock Hats', {'CH.level': 0.8, 'mod.CH.decay': 0.3, 'mod.HH.tune': 0.6, 'OH.decay': 0.3});
kit('Electro', 'Vocoder Night', {'CP.level': 0.7, 'rev.send.CP': 0.45, 'rev.send.SD': 0.35, 'rev.program': 7, 'rev.time': 3, 'rev.return': 0.6, 'dly.send.CP': 0.2, 'dly.sync': 1, 'dly.division': 4, 'dly.feedback': 0.4, 'dly.return': 0.4});
kit('Electro', 'Space Cadet', {'mod.TOM.sweep': 0.6, 'dly.send.HT': 0.4, 'dly.send.MT': 0.4, 'dly.pingpong': 1, 'dly.sync': 1, 'dly.division': 3, 'dly.feedback': 0.5, 'dly.return': 0.5});
kit('Electro', 'Bronx Basement', {'BD.decay': 0.7, 'mod.age': 0.35, 'mod.hiss': 0.2, 'rev.send.SD': 0.15, 'rev.program': 3, 'rev.time': 0.6, 'rev.return': 0.4});
kit('Electro', 'Cowbell Cosmic', {'CB.level': 0.85, 'mod.CB.decay': 0.7, 'rev.send.CB': 0.35, 'rev.program': 0, 'rev.time': 3.5, 'rev.return': 0.5});

// Miami Bass
kit('Miami Bass', 'Bass Car Test', {'BD.level': 0.9, 'BD.decay': 1, 'mod.BD.longDecay': 0.8, 'BD.tone': 0.3, 'mod.BD.tune': 0.42});
kit('Miami Bass', 'Booty Boom', {'BD.level': 0.9, 'BD.decay': 0.95, 'mod.BD.longDecay': 0.65, 'CP.level': 0.75, 'swing': 0.1});
kit('Miami Bass', 'Low Rider', {'BD.level': 0.95, 'mod.BD.longDecay': 0.9, 'mod.BD.tune': 0.38, 'mod.BD.drive': 0.25});
kit('Miami Bass', 'Trunk Rattle', {'BD.level': 0.95, 'mod.BD.longDecay': 1, 'mod.BD.tune': 0.35, 'mod.BD.drive': 0.45, 'BD.tone': 0.25});
kit('Miami Bass', 'South Beach', {'mod.BD.longDecay': 0.7, 'CP.level': 0.7, 'rev.send.CP': 0.25, 'rev.program': 6, 'rev.time': 1.6, 'rev.return': 0.45, 'CB.level': 0.65});
kit('Miami Bass', 'Bass Sweep Tuned Up', {'mod.BD.longDecay': 0.75, 'mod.BD.tune': 0.6, 'mod.BD.sweep': 0.55});
kit('Miami Bass', 'Bass Sweep Tuned Down', {'mod.BD.longDecay': 0.85, 'mod.BD.tune': 0.3, 'mod.BD.sweep': 0.45});
kit('Miami Bass', 'Clap Party', {'CP.level': 0.9, 'mod.CP.spread': 0.7, 'mod.CP.tail': 0.55, 'mod.BD.longDecay': 0.6});
kit('Miami Bass', 'Rimshot Rider', {'RS.level': 0.8, 'mod.RS.tune': 0.55, 'mod.BD.longDecay': 0.7, 'dly.send.RS': 0.25, 'dly.sync': 1, 'dly.division': 5, 'dly.feedback': 0.3, 'dly.return': 0.4});
kit('Miami Bass', 'Subwoofer Breaker', {'BD.level': 1, 'mod.BD.longDecay': 1, 'mod.BD.drive': 0.6, 'mod.BD.tune': 0.32, 'master': 0.7});
kit('Miami Bass', 'Bass Station', {'mod.BD.longDecay': 0.8, 'LT.sel': 1, 'MT.sel': 1, 'HT.sel': 1, 'mod.CONGA.decay': 0.6});

// Trap / 808 Boom
kit('Trap / 808 Boom', 'Infinite Boom', {'BD.level': 0.95, 'BD.decay': 1, 'mod.BD.longDecay': 1, 'mod.BD.tune': 0.45, 'BD.tone': 0.35, 'mod.BD.drive': 0.3});
kit('Trap / 808 Boom', 'Distorted Boom', {'BD.level': 0.9, 'mod.BD.longDecay': 0.9, 'mod.BD.drive': 0.85, 'mod.BD.click': 0.6});
kit('Trap / 808 Boom', 'Glide Boom', {'mod.BD.longDecay': 0.95, 'mod.BD.sweep': 0.85, 'mod.BD.tune': 0.4});
kit('Trap / 808 Boom', 'Snappy Trap', {'mod.BD.longDecay': 0.85, 'SD.snappy': 0.9, 'SD.tone': 0.75, 'mod.SD.decay': 0.3, 'CH.level': 0.75, 'mod.CH.decay': 0.25});
kit('Trap / 808 Boom', 'Roll Hats', {'mod.BD.longDecay': 0.8, 'CH.level': 0.8, 'mod.HH.tune': 0.65, 'mod.CH.decay': 0.2});
kit('Trap / 808 Boom', 'Dark Boom', {'mod.BD.longDecay': 0.9, 'BD.tone': 0.1, 'mod.BD.tune': 0.36, 'CP.level': 0.7, 'rev.send.CP': 0.3, 'rev.program': 1, 'rev.time': 2.6, 'rev.return': 0.5});
kit('Trap / 808 Boom', 'Clap Stack', {'mod.BD.longDecay': 0.85, 'CP.level': 0.85, 'mod.CP.spread': 0.8, 'SD.level': 0.6, 'SD.snappy': 0.8});
kit('Trap / 808 Boom', 'Drill Slide', {'mod.BD.longDecay': 0.95, 'mod.BD.sweep': 0.7, 'mod.BD.tune': 0.5, 'RS.level': 0.75, 'swing': 0});
kit('Trap / 808 Boom', 'Clicky Sub', {'mod.BD.longDecay': 0.9, 'mod.BD.click': 0.9, 'BD.tone': 0.7});
kit('Trap / 808 Boom', 'Sub Only', {'mod.BD.longDecay': 1, 'BD.tone': 0, 'mod.BD.click': 0, 'mod.BD.tune': 0.4});
kit('Trap / 808 Boom', 'Cloud Rap', {'mod.BD.longDecay': 0.85, 'rev.send.SD': 0.45, 'rev.send.CP': 0.45, 'rev.send.CH': 0.2, 'rev.program': 0, 'rev.time': 4.5, 'rev.return': 0.55});

// Hip-Hop
kit('Hip-Hop', 'Boom Bap 808', {'BD.decay': 0.7, 'SD.snappy': 0.5, 'mod.age': 0.3, 'swing': 0.35});
kit('Hip-Hop', 'Golden Era', {'BD.decay': 0.75, 'mod.BD.drive': 0.3, 'SD.tone': 0.45, 'mod.age': 0.4, 'mod.hiss': 0.15, 'swing': 0.4});
kit('Hip-Hop', 'Def Jam Room', {'BD.decay': 0.85, 'mod.BD.longDecay': 0.4, 'SD.snappy': 0.7, 'rev.send.SD': 0.25, 'rev.program': 2, 'rev.time': 1.1, 'rev.return': 0.45});
kit('Hip-Hop', 'Cowbell Hustle', {'CB.level': 0.75, 'BD.decay': 0.8, 'swing': 0.25});
kit('Hip-Hop', 'Rimshot Jeep', {'RS.level': 0.85, 'mod.BD.longDecay': 0.5, 'swing': 0.3});
kit('Hip-Hop', 'Dusty Crate', {'mod.age': 0.6, 'mod.hiss': 0.35, 'BD.tone': 0.35, 'SD.tone': 0.35, 'CY.tone': 0.3, 'swing': 0.45});
kit('Hip-Hop', 'West Coast Bounce', {'mod.BD.longDecay': 0.6, 'CP.level': 0.75, 'mod.CP.tail': 0.45, 'swing': 0.2});
kit('Hip-Hop', 'Lazy Swing', {'swing': 0.55, 'BD.decay': 0.65, 'OH.decay': 0.35});
kit('Hip-Hop', 'Plate Snare', {'rev.send.SD': 0.4, 'rev.program': 6, 'rev.time': 1.5, 'rev.return': 0.55, 'swing': 0.3});
kit('Hip-Hop', 'Big Room Kick', {'BD.level': 0.9, 'mod.BD.longDecay': 0.55, 'mod.BD.drive': 0.4, 'rev.send.BD': 0.1, 'rev.program': 2, 'rev.time': 0.8, 'rev.return': 0.4});
kit('Hip-Hop', 'Head Nod Hats', {'CH.level': 0.55, 'OH.level': 0.5, 'OH.decay': 0.3, 'swing': 0.4});

// House / Acid
kit('House / Acid', 'Warehouse 808', {'BD.decay': 0.5, 'BD.tone': 0.6, 'CP.level': 0.75, 'OH.decay': 0.35, 'rev.send.CP': 0.25, 'rev.program': 3, 'rev.time': 1.2, 'rev.return': 0.45});
kit('House / Acid', 'Deep House Box', {'BD.decay': 0.55, 'BD.tone': 0.35, 'SD.level': 0.5, 'RS.level': 0.7, 'LT.sel': 1, 'MT.sel': 1, 'HT.sel': 1, 'swing': 0.2});
kit('House / Acid', 'Acid Basement', {'BD.decay': 0.45, 'mod.BD.drive': 0.4, 'CP.level': 0.7, 'CH.level': 0.75, 'dly.send.CP': 0.2, 'dly.sync': 1, 'dly.division': 4, 'dly.feedback': 0.35, 'dly.return': 0.4});
kit('House / Acid', 'Jackin Clap', {'CP.level': 0.85, 'mod.CP.spread': 0.65, 'swing': 0.25});
kit('House / Acid', 'Garage Shuffle', {'swing': 0.5, 'RS.level': 0.7, 'CH.level': 0.7, 'mod.CH.decay': 0.35});
kit('House / Acid', 'Cowbell Disco', {'CB.level': 0.75, 'OH.decay': 0.45, 'rev.send.CB': 0.2, 'rev.program': 2, 'rev.time': 1, 'rev.return': 0.4});
kit('House / Acid', 'Piano House Kit', {'CP.level': 0.8, 'BD.decay': 0.55, 'rev.send.CP': 0.3, 'rev.program': 6, 'rev.time': 1.4, 'rev.return': 0.5});
kit('House / Acid', 'Tribal Congas', {'LT.sel': 1, 'MT.sel': 1, 'HT.sel': 1, 'mod.CONGA.decay': 0.55, 'LT.level': 0.75, 'MT.level': 0.75, 'HT.level': 0.75, 'CP.sel': 1});
kit('House / Acid', 'Acid Rain', {'mod.BD.drive': 0.5, 'dly.send.CH': 0.25, 'dly.send.CP': 0.3, 'dly.sync': 1, 'dly.division': 3, 'dly.feedback': 0.55, 'dly.pingpong': 1, 'dly.return': 0.45});
kit('House / Acid', 'Ballroom Vogue', {'CP.level': 0.85, 'mod.CP.tail': 0.7, 'rev.send.CP': 0.35, 'rev.program': 0, 'rev.time': 2, 'rev.return': 0.5, 'CY.level': 0.7});
kit('House / Acid', 'Hip House', {'BD.decay': 0.65, 'SD.snappy': 0.7, 'CP.level': 0.75, 'swing': 0.15});

// Techno
kit('Techno', 'Detroit Machine', {'BD.decay': 0.55, 'BD.tone': 0.5, 'CP.level': 0.7, 'CB.level': 0.65, 'dly.send.CB': 0.25, 'dly.sync': 1, 'dly.division': 4, 'dly.feedback': 0.4, 'dly.return': 0.4});
kit('Techno', 'Driving Kick', {'BD.level': 0.9, 'BD.decay': 0.5, 'mod.BD.drive': 0.65, 'mod.BD.click': 0.7});
kit('Techno', 'Industrial Plant', {'mod.BD.drive': 0.85, 'mod.SD.noiseColor': 0.8, 'mod.age': 0.5, 'rev.send.SD': 0.3, 'rev.program': 10, 'rev.time': 0.6, 'rev.return': 0.55});
kit('Techno', 'Minimal Click', {'BD.decay': 0.3, 'RS.level': 0.75, 'mod.RS.tune': 0.65, 'CH.level': 0.6, 'mod.CH.decay': 0.15});
kit('Techno', 'Dub Techno Chord Room', {'RS.level': 0.7, 'rev.send.RS': 0.45, 'dly.send.RS': 0.45, 'dly.sync': 1, 'dly.division': 5, 'dly.feedback': 0.6, 'dly.hicut': 3500, 'dly.return': 0.5, 'rev.program': 0, 'rev.time': 3.5, 'rev.return': 0.45});
kit('Techno', 'Metal Hats', {'mod.HH.tune': 0.8, 'CH.level': 0.75, 'OH.level': 0.7, 'mod.CY.tune': 0.7});
kit('Techno', 'Rumble Kick', {'BD.decay': 0.8, 'mod.BD.longDecay': 0.5, 'rev.send.BD': 0.3, 'rev.program': 0, 'rev.time': 2.5, 'rev.lpf': 2500, 'rev.return': 0.5});
kit('Techno', 'Peak Time', {'BD.level': 0.9, 'mod.BD.drive': 0.5, 'CP.level': 0.8, 'OH.level': 0.7, 'OH.decay': 0.3});
kit('Techno', 'Electro Techno', {'mod.TOM.sweep': 0.7, 'CB.level': 0.7, 'mod.CB.tune': 0.6, 'CP.level': 0.7});
kit('Techno', 'Hard Gate', {'rev.send.SD': 0.4, 'rev.send.CP': 0.4, 'rev.program': 10, 'rev.time': 0.4, 'rev.return': 0.6, 'mod.BD.drive': 0.4});
kit('Techno', 'Reverse Wash', {'rev.send.CP': 0.45, 'rev.send.CY': 0.35, 'rev.program': 11, 'rev.time': 0.8, 'rev.return': 0.6});

// Lo-Fi / Aged
kit('Lo-Fi / Aged', 'Pawn Shop 808', {'mod.age': 0.8, 'mod.hiss': 0.4, 'BD.tone': 0.3, 'CY.tone': 0.3});
kit('Lo-Fi / Aged', 'Attic Find', {'mod.age': 0.95, 'mod.hiss': 0.55, 'SD.snappy': 0.4, 'OH.decay': 0.3});
kit('Lo-Fi / Aged', 'Tape Copy', {'mod.age': 0.5, 'mod.hiss': 0.45, 'rev.send.SD': 0.15, 'rev.program': 3, 'rev.time': 0.7, 'rev.lpf': 6000, 'rev.return': 0.4, 'swing': 0.3});
kit('Lo-Fi / Aged', 'Bedroom Study', {'mod.age': 0.45, 'mod.hiss': 0.3, 'BD.tone': 0.25, 'SD.tone': 0.3, 'swing': 0.45});
kit('Lo-Fi / Aged', 'Cold Joints', {'mod.age': 1, 'mod.hiss': 0.6, 'mod.BD.tune': 0.46, 'mod.HH.tune': 0.42});
kit('Lo-Fi / Aged', 'Sunday Tape Deck', {'mod.age': 0.6, 'mod.hiss': 0.5, 'dly.send.SD': 0.2, 'dly.time': 380, 'dly.hicut': 2500, 'dly.feedback': 0.3, 'dly.return': 0.4});
kit('Lo-Fi / Aged', 'Dusty Congas', {'mod.age': 0.6, 'LT.sel': 1, 'MT.sel': 1, 'HT.sel': 1, 'mod.hiss': 0.3, 'swing': 0.35});
kit('Lo-Fi / Aged', 'Burnt Cap', {'mod.age': 0.85, 'BD.decay': 0.9, 'mod.BD.longDecay': 0.3, 'SD.tone': 0.2});
kit('Lo-Fi / Aged', 'Muffled Party', {'BD.tone': 0.1, 'SD.tone': 0.1, 'CY.tone': 0.1, 'mod.age': 0.4, 'rev.send.SD': 0.2, 'rev.program': 3, 'rev.lpf': 3000, 'rev.time': 0.8, 'rev.return': 0.4});
kit('Lo-Fi / Aged', 'Rainy Window', {'mod.age': 0.5, 'mod.hiss': 0.65, 'rev.send.CP': 0.3, 'rev.send.SD': 0.3, 'rev.program': 1, 'rev.time': 2.4, 'rev.lpf': 4500, 'rev.return': 0.45, 'swing': 0.4});
kit('Lo-Fi / Aged', 'Flea Market Find', {'mod.age': 0.7, 'mod.hiss': 0.25, 'AC.level': 0.4});

// Circuit Bent
kit('Circuit Bent', 'Bent Kick Zap', {'mod.BD.sweep': 1, 'mod.BD.tune': 0.8, 'mod.BD.click': 1});
kit('Circuit Bent', 'Laser Toms', {'mod.TOM.sweep': 1, 'mod.TOM.decay': 0.8, 'HT.tuning': 1, 'MT.tuning': 0.8, 'LT.tuning': 0.6});
kit('Circuit Bent', 'Metal Overdrive', {'mod.HH.tune': 1, 'mod.CY.tune': 0.9, 'mod.CB.tune': 0.9, 'mod.CH.decay': 0.6});
kit('Circuit Bent', 'Low Metal', {'mod.HH.tune': 0.1, 'mod.CY.tune': 0.15, 'mod.CB.tune': 0.2, 'mod.CB.decay': 0.8});
kit('Circuit Bent', 'Infinite Clap', {'mod.CP.tail': 1, 'mod.CP.spread': 1, 'CP.level': 0.8});
kit('Circuit Bent', 'Snare Noise Burst', {'mod.SD.noiseColor': 1, 'mod.SD.decay': 1, 'SD.snappy': 1, 'mod.SD.tune': 0.8});
kit('Circuit Bent', 'Glitch Rim', {'mod.RS.tune': 1, 'mod.CL.tune': 0.1, 'RS.level': 0.8});
kit('Circuit Bent', 'Accent Overload', {'AC.level': 1, 'mod.AC.depth': 1, 'mod.AC.punch': 1});
kit('Circuit Bent', 'Long Maracas Rattle', {'mod.MA.decay': 1, 'CP.sel': 1, 'CP.level': 0.75});
kit('Circuit Bent', 'Drift Machine', {'mod.age': 1, 'mod.BD.tune': 0.3, 'mod.SD.tune': 0.7, 'mod.HH.tune': 0.35});
kit('Circuit Bent', 'Bent Everything', {'mod.BD.sweep': 0.9, 'mod.TOM.sweep': 0.9, 'mod.HH.tune': 0.85, 'mod.CP.tail': 0.9, 'mod.CB.decay': 0.9, 'mod.SD.noiseColor': 0.9, 'mod.AC.punch': 0.8});
kit('Circuit Bent', 'Click Machine', {'mod.BD.click': 1, 'BD.decay': 0.1, 'mod.CH.decay': 0.05, 'mod.RS.tune': 0.8});

// Trigger Out (Egyptian Lover trick): the rear trigger-out pulse recorded into the mix as a percussion hit
kit('Trigger Out (Egyptian Lover trick)', 'Trigger Out: Cowbell Click', {'mod.TRIG.src': 0, 'mod.TRIG.width': 20, 'mod.TRIG.level': 0.7, 'mod.TRIG.tone': 0.5, 'CB.level': 0.3});
kit('Trigger Out (Egyptian Lover trick)', 'Trigger Out: Clap Click', {'mod.TRIG.src': 1, 'mod.TRIG.width': 20, 'mod.TRIG.level': 0.7, 'mod.TRIG.tone': 0.5});
kit('Trigger Out (Egyptian Lover trick)', 'Trigger Out: Accent Click', {'mod.TRIG.src': 2, 'mod.TRIG.width': 20, 'mod.TRIG.level': 0.65, 'mod.TRIG.tone': 0.55});
kit('Trigger Out (Egyptian Lover trick)', 'Trigger Out: Own Track', {'mod.TRIG.src': 3, 'mod.TRIG.width': 20, 'mod.TRIG.level': 0.7, 'mod.TRIG.tone': 0.5});
kit('Trigger Out (Egyptian Lover trick)', 'Trigger Out: Hi-Q Tick', {'mod.TRIG.src': 3, 'mod.TRIG.width': 4, 'mod.TRIG.level': 0.8, 'mod.TRIG.tone': 0.85, 'rev.send.TRIG': 0.2, 'rev.program': 2, 'rev.time': 0.8, 'rev.return': 0.45});
kit('Trigger Out (Egyptian Lover trick)', 'Trigger Out: Fat Pulse', {'mod.TRIG.src': 3, 'mod.TRIG.width': 38, 'mod.TRIG.level': 0.75, 'mod.TRIG.tone': 0.2});
kit('Trigger Out (Egyptian Lover trick)', 'Trigger Out: Echo Tick', {'mod.TRIG.src': 3, 'mod.TRIG.width': 10, 'mod.TRIG.level': 0.7, 'mod.TRIG.tone': 0.7, 'dly.send.TRIG': 0.45, 'dly.sync': 1, 'dly.division': 3, 'dly.feedback': 0.45, 'dly.pingpong': 1, 'dly.return': 0.5});
kit('Trigger Out (Egyptian Lover trick)', 'Trigger Out: Electro Party', {'mod.TRIG.src': 0, 'mod.TRIG.width': 20, 'mod.TRIG.level': 0.75, 'BD.decay': 0.8, 'CP.level': 0.75, 'rev.send.CP': 0.3, 'rev.program': 6, 'rev.time': 1.8, 'rev.return': 0.5});
kit('Trigger Out (Egyptian Lover trick)', 'Trigger Out: Boom and Tick', {'mod.TRIG.src': 3, 'mod.TRIG.width': 15, 'mod.TRIG.level': 0.7, 'mod.BD.longDecay': 0.7});
kit('Trigger Out (Egyptian Lover trick)', 'Trigger Out: Plate Tick', {'mod.TRIG.src': 3, 'mod.TRIG.width': 8, 'mod.TRIG.level': 0.65, 'rev.send.TRIG': 0.4, 'rev.program': 7, 'rev.time': 2, 'rev.return': 0.5});
kit('Trigger Out (Egyptian Lover trick)', 'Trigger Out: Accent Stab', {'mod.TRIG.src': 2, 'mod.TRIG.width': 30, 'mod.TRIG.level': 0.8, 'mod.TRIG.tone': 0.35, 'AC.level': 0.85});
kit('Trigger Out (Egyptian Lover trick)', 'Trigger Out: Dry Studio', {'mod.TRIG.src': 3, 'mod.TRIG.width': 20, 'mod.TRIG.level': 0.6, 'mod.TRIG.tone': 0.5, 'mod.age': 0.2});

// FX-heavy
kit('FX-Heavy', 'Cathedral 808', {'rev.send.SD': 0.5, 'rev.send.CP': 0.5, 'rev.send.CY': 0.4, 'rev.send.HT': 0.4, 'rev.program': 0, 'rev.time': 6, 'rev.predelay': 60, 'rev.return': 0.6});
kit('FX-Heavy', 'Dub Siren Kit', {'dly.send.SD': 0.5, 'dly.send.RS': 0.5, 'dly.send.CB': 0.4, 'dly.sync': 1, 'dly.division': 5, 'dly.feedback': 0.7, 'dly.hicut': 3000, 'dly.return': 0.55, 'rev.send.SD': 0.3, 'rev.program': 7, 'rev.time': 2.5, 'rev.return': 0.45});
kit('FX-Heavy', 'Ping Pong Hats', {'dly.send.CH': 0.45, 'dly.send.OH': 0.35, 'dly.pingpong': 1, 'dly.sync': 1, 'dly.division': 3, 'dly.feedback': 0.5, 'dly.return': 0.5});
kit('FX-Heavy', 'Gated 80s', {'rev.send.SD': 0.6, 'rev.send.HT': 0.5, 'rev.send.MT': 0.5, 'rev.send.LT': 0.5, 'rev.program': 10, 'rev.time': 0.5, 'rev.return': 0.65});
kit('FX-Heavy', 'Reverse Snare', {'rev.send.SD': 0.6, 'rev.send.CP': 0.5, 'rev.program': 11, 'rev.time': 0.9, 'rev.return': 0.65});
kit('FX-Heavy', 'Chorused Space', {'rev.send.CP': 0.4, 'rev.send.CH': 0.25, 'rev.program': 1, 'rev.time': 3, 'rev.return': 0.5, 'dly.send.CP': 0.3, 'dly.time': 25, 'dly.feedback': 0.2, 'dly.modDepth': 0.7, 'dly.modRate': 0.6, 'dly.return': 0.45});
kit('FX-Heavy', 'Ducked Echo', {'dly.send.SD': 0.4, 'dly.send.CP': 0.4, 'dly.duck': 0.8, 'dly.sync': 1, 'dly.division': 4, 'dly.feedback': 0.6, 'dly.return': 0.6});
kit('FX-Heavy', 'Slapback Rock', {'dly.send.SD': 0.35, 'dly.send.BD': 0.15, 'dly.time': 110, 'dly.feedback': 0.1, 'dly.return': 0.5, 'rev.send.SD': 0.2, 'rev.program': 3, 'rev.time': 0.9, 'rev.return': 0.4});
kit('FX-Heavy', 'Vocal Hall Clap', {'rev.send.CP': 0.55, 'rev.program': 4, 'rev.time': 2.2, 'rev.predelay': 40, 'rev.return': 0.55});
kit('FX-Heavy', 'Early Reflections', {'rev.send.SD': 0.4, 'rev.send.RS': 0.4, 'rev.program': 8, 'rev.time': 0.5, 'rev.er': 0.85, 'rev.return': 0.55});
kit('FX-Heavy', 'Endless Tom Echo', {'dly.send.LT': 0.5, 'dly.send.MT': 0.5, 'dly.send.HT': 0.5, 'dly.sync': 1, 'dly.division': 6, 'dly.feedback': 0.8, 'dly.lowcut': 300, 'dly.return': 0.5, 'mod.TOM.sweep': 0.5});
kit('FX-Heavy', 'Flanged Cymbal', {'dly.send.CY': 0.5, 'dly.send.OH': 0.4, 'dly.time': 4, 'dly.feedback': 0.6, 'dly.modDepth': 0.9, 'dly.modRate': 0.25, 'dly.return': 0.55, 'CY.decay': 0.9});

/* ---------------- rhythms ---------------- */
const TRACKS = ['AC', 'BD', 'SD', 'LT', 'MT', 'HT', 'RS', 'CP', 'CB', 'CY', 'OH', 'CH', 'TRIG'];
// a pattern from a short spec: {BD:'x...x...', ...}. Each string is repeated to fill len*parts.
function pat(spec, len = 16, scale = 0, parts = 1){
  const n = len * parts, steps = {};
  for (const t of TRACKS){
    let s = spec[t] || '';
    if (!s) continue;
    s = s.replace(/\s+/g, '');
    while (s.length < n) s += s;
    s = s.slice(0, n).replace(/[^x]/g, '.');
    if (s.includes('x')) steps[t] = s;
  }
  return {scale, len, parts, steps};
}
function clone(p){ return {scale:p.scale, len:p.len, parts:p.parts, steps:Object.assign({}, p.steps)}; }
function setStep(p, t, i, on){
  const n = p.len * p.parts; let s = p.steps[t] || '.'.repeat(n);
  s = s.slice(0, i) + (on ? 'x' : '.') + s.slice(i + 1); p.steps[t] = s;
}
// variation B rules: each is musical and keeps the groove
const VAR = {
  // a snare/tom fill over the last 4 steps of the pattern
  fill(p){
    const q = clone(p), n = q.len * q.parts, e = n - 4;
    for (let i = e; i < n; i++){ setStep(q, 'BD', i, false); setStep(q, 'CH', i, false); }
    if (q.len % 4 === 0){ setStep(q, 'SD', e, true); setStep(q, 'HT', e + 1, true); setStep(q, 'MT', e + 2, true); setStep(q, 'LT', e + 3, true); setStep(q, 'AC', e + 3, true); }
    else { setStep(q, 'SD', e + 1, true); setStep(q, 'SD', e + 2, true); setStep(q, 'SD', e + 3, true); setStep(q, 'AC', e + 3, true); }
    return q;
  },
  // busier hats and an open hat on the last off-beat
  busy(p){
    const q = clone(p), n = q.len * q.parts;
    for (let i = 0; i < n; i++) if (!(q.steps.OH && q.steps.OH[i] === 'x')) setStep(q, 'CH', i, true);
    setStep(q, 'OH', n - 2, true); setStep(q, 'CH', n - 2, false);
    return q;
  },
  // an extra kick push and a clap doubled on the backbeat
  push(p){
    const q = clone(p), n = q.len * q.parts;
    setStep(q, 'BD', n - 3, true); setStep(q, 'BD', n - 1, false);
    if (q.steps.SD) for (let i = 0; i < n; i++) if (q.steps.SD[i] === 'x') setStep(q, 'CP', i, true);
    setStep(q, 'CY', 0, true); setStep(q, 'AC', 0, true);
    return q;
  },
  // a crash on the one and a cowbell answer
  answer(p){
    const q = clone(p), n = q.len * q.parts;
    setStep(q, 'CY', 0, true); setStep(q, 'AC', 0, true);
    for (let i = 3; i < n; i += 8) setStep(q, 'CB', i, true);
    return q;
  }
};
const RHYTHMS = [];
function rhy(cat, name, tempo, swing, A, B){ RHYTHMS.push({name, cat, tempo, swing, rhythm:{A, B}}); }
// a groove with its B made by one rule, and an optional second entry that uses another rule
function groove(cat, name, tempo, swing, spec, rule = 'fill', len = 16, scale = 0, parts = 1){
  const A = pat(spec, len, scale, parts);
  rhy(cat, name, tempo, swing, A, VAR[rule](A));
}

// ---- Electro / Freestyle ----
const EL = [
  ['Electro Funk 1', 112, {AC:'x.......x.......', BD:'x.....x...x.....', SD:'....x.......x...', CH:'x.x.x.x.x.x.x.x.', CB:'..x.......x..x..'}],
  ['Electro Funk 2', 118, {AC:'x...x.......x...', BD:'x.....x.x.....x.', CP:'....x.......x...', CH:'xxxxxxxxxxxxxxxx', OH:'......x.........'}],
  ['Robot Walk', 110, {AC:'x.......x.......', BD:'x..x..x...x.....', SD:'....x.......x...', CH:'x.x.x.x.x.x.x.x.', HT:'..............x.', MT:'.............x..'}],
  ['Breaker Floor', 122, {AC:'x...x...x...x...', BD:'x.....x...x..x..', SD:'....x.......x...', CP:'....x.......x...', CH:'x.xxx.xxx.xxx.xx'}],
  ['Cowbell Robot', 116, {AC:'x.......x.......', BD:'x.....x...x.....', CP:'....x.......x...', CB:'x..x..x...x..x..', CH:'x.x.x.x.x.x.x.x.'}],
  ['Laser Battle', 126, {AC:'x.....x.....x...', BD:'x..x....x.x.....', SD:'....x.......x...', HT:'......x.........', MT:'.......x........', LT:'..............x.', CH:'x.x.x.x.x.x.x.x.'}],
  ['Space Patrol', 108, {AC:'x.........x.....', BD:'x.........x.x...', SD:'....x.......x...', CH:'x.x.x.x.x.x.x.x.', RS:'.x.....x...x....', CB:'.......x.......x'}],
  ['Pop Lock', 104, {AC:'x.......x.......', BD:'x...x.x.....x...', CP:'....x.......x...', CH:'x.x.x.x.x.x.x.x.', OH:'.......x.......x'}],
  ['Freestyle Groove 1', 118, {AC:'x..x..x.x.......', BD:'x..x..x...x.....', SD:'....x.......x...', CP:'....x.......x...', CH:'x.x.x.x.x.x.x.x.', CB:'...x......x.....'}],
  ['Freestyle Groove 2', 122, {AC:'x.....x.....x...', BD:'x.....x...x.....', SD:'....x.......x..x', CH:'xxxxxxxxxxxxxxxx', OH:'..x.......x.....'}],
  ['Freestyle Ballad', 98, {AC:'x.......x.......', BD:'x......x..x.....', SD:'....x.......x...', CH:'x.x.x.x.x.x.x.x.', CY:'x...............'}],
  ['Electro Shuffle', 112, {AC:'x.......x.......', BD:'x.....x...x...x.', CP:'....x.......x...', CH:'x.xxx.xxx.xxx.xx', RS:'..x.......x.....'}],
  ['Mic Check Electro', 120, {AC:'x...............', BD:'x.x...x...x.....', SD:'....x.......x...', CH:'x.x.x.x.x.x.x.x.', CB:'......x.......x.'}],
  ['Planetary', 128, {AC:'x.......x.......', BD:'x.......x.x.....', CP:'....x.......x...', CH:'..x...x...x...x.', OH:'..x...x...x...x.', HT:'.............x..', MT:'..............x.'}],
  ['Kraftwerk Garage', 114, {AC:'x...x...x...x...', BD:'x...x...x...x...', SD:'....x.......x...', CH:'x.x.x.x.x.x.x.x.', CB:'x.......x.......'}],
  ['Uprock', 100, {AC:'x.......x.......', BD:'x..x...x..x.....', SD:'....x.......x...', CH:'x.x.x.x.x.x.x.x.', LT:'...............x'}]
];
for (const [n, t, s] of EL){ groove('Electro', n, t, 0, s, 'fill'); groove('Electro', n + ' (Busy)', t, 0, s, 'busy'); }

// ---- Trigger Out grooves: the TRIG track as a hi-Q tick (the Egyptian Lover trick) ----
const TR = [
  ['Trigger Tick Electro', 124, {AC:'x.......x.......', BD:'x.....x...x.....', CP:'....x.......x...', CH:'x.x.x.x.x.x.x.x.', TRIG:'..x...x...x..x.x'}],
  ['Trigger Party Freak', 128, {AC:'x...x...x...x...', BD:'x..x..x...x.....', SD:'....x.......x...', CH:'xxxxxxxxxxxxxxxx', TRIG:'.x...x.x.x...x..'}],
  ['Trigger Nile Walk', 116, {AC:'x.......x.......', BD:'x.....x.x.......', CP:'....x.......x...', CB:'..x.......x.....', TRIG:'...x...x...x...x'}],
  ['Trigger Roller', 132, {AC:'x.......x.......', BD:'x.....x...x...x.', SD:'....x.......x...', CH:'x.x.x.x.x.x.x.x.', TRIG:'x.xx.x.xx.x.x.xx'}],
  ['Trigger Click Track', 120, {AC:'x...............', BD:'x.......x.......', CP:'....x.......x...', TRIG:'xxxxxxxxxxxxxxxx'}],
  ['Trigger Pyramid', 126, {AC:'x.....x.......x.', BD:'x.....x...x.....', SD:'....x.......x...', CH:'x.x.x.x.x.x.x.x.', HT:'..............x.', TRIG:'..x.x...x.x...x.'}],
  ['Trigger and Cowbell', 122, {AC:'x.......x.......', BD:'x.....x...x.....', CP:'....x.......x...', CB:'..x..x....x..x..', TRIG:'..x..x....x..x..'}],
  ['Trigger on Accent', 118, {AC:'x..x..x...x..x..', BD:'x.....x...x.....', SD:'....x.......x...', CH:'x.x.x.x.x.x.x.x.', TRIG:'x..x..x...x..x..'}]
];
for (const [n, t, s] of TR){ groove('Trigger Out', n, t, 0, s, 'answer'); groove('Trigger Out', n + ' (Fill)', t, 0, s, 'fill'); }

// ---- Miami Bass ----
const MI = [
  ['Bass Booty 1', 132, {AC:'x.......x.......', BD:'x..x..x...x.....', CP:'....x.......x...', CH:'x.x.x.x.x.x.x.x.'}],
  ['Bass Booty 2', 136, {AC:'x.....x.........', BD:'x.....x..x......', CP:'....x.......x...', CH:'xxxxxxxxxxxxxxxx', CB:'..x.......x.....'}],
  ['Bass Drop', 130, {AC:'x...............', BD:'x.........x.....', CP:'....x.......x...', CH:'x.x.x.x.x.x.x.x.', OH:'..............x.'}],
  ['Car Show', 128, {AC:'x.......x.......', BD:'x..x....x.x.....', SD:'....x.......x...', CP:'....x.......x...', CH:'x.xxx.xxx.xxx.xx'}],
  ['Bass Rim Ride', 134, {AC:'x...x...x...x...', BD:'x.....x...x.....', RS:'..x..x..x...x.x.', CP:'....x.......x...', CH:'x.x.x.x.x.x.x.x.'}],
  ['Beach Jam', 126, {AC:'x.......x.......', BD:'x..x..x.....x...', CP:'....x.......x...', CB:'x.......x.......', CH:'x.x.x.x.x.x.x.x.'}],
  ['Bass Ballad', 100, {AC:'x.......x.......', BD:'x.....x.x.......', SD:'....x.......x...', CH:'x.x.x.x.x.x.x.x.', CY:'x...............'}],
  ['Bass Machine Gun', 140, {AC:'x.......x.......', BD:'x.x.x.x...x.....', CP:'....x.......x...', SD:'..............xx', CH:'xxxxxxxxxxxxxxxx'}]
];
for (const [n, t, s] of MI){ groove('Miami Bass', n, t, 0.05, s, 'push'); groove('Miami Bass', n + ' (Fill)', t, 0.05, s, 'fill'); }

// ---- Boom Bap / Hip-Hop ----
const HH = [
  ['Boom Bap 1', 90, {AC:'x.......x.......', BD:'x.........x.....', SD:'....x.......x...', CH:'x.x.x.x.x.x.x.x.'}],
  ['Boom Bap 2', 88, {AC:'x.......x.......', BD:'x......x..x.....', SD:'....x.......x...', CH:'x.x.x.x.x.x.x.x.', OH:'..............x.'}],
  ['Boom Bap 3', 94, {AC:'x...............', BD:'x.x.......x..x..', SD:'....x.......x...', CH:'x.x.x.x.x.x.x.x.'}],
  ['Head Nod', 86, {AC:'x.......x.......', BD:'x.......xx......', SD:'....x.......x...', CH:'x.x.x.x.x.x.x.x.', RS:'...........x....'}],
  ['Cowbell Hustle', 98, {AC:'x.......x.......', BD:'x.....x...x.....', SD:'....x.......x...', CB:'x..x..x.x..x..x.', CH:'x.x.x.x.x.x.x.x.'}],
  ['Rim Jeep Beat', 92, {AC:'x...x...x...x...', BD:'x.........x..x..', RS:'....x.......x...', CH:'x.x.x.x.x.x.x.x.'}],
  ['Old School Rap', 102, {AC:'x.......x.......', BD:'x.....x.x.....x.', SD:'....x.......x...', CP:'....x.......x...', CH:'x.x.x.x.x.x.x.x.'}],
  ['Lazy Sunday', 84, {AC:'x.......x.......', BD:'x......x...x....', SD:'....x.......x...', CH:'x.x.x.x.x.x.x.x.', OH:'......x.........'}],
  ['G-Funk Roll', 94, {AC:'x.......x.......', BD:'x.....x...x.....', CP:'....x.......x...', CH:'x.xxx.xxx.xxx.xx', OH:'..............x.'}],
  ['Underground', 90, {AC:'x...............', BD:'x..x......x.....', SD:'....x.......x...', CH:'..x...x...x...x.'}],
  ['Battle Beat', 96, {AC:'x.......x.......', BD:'x.x...x...x.....', SD:'....x.......x..x', CH:'x.x.x.x.x.x.x.x.', CY:'x...............'}],
  ['Kick Pusher', 92, {AC:'x.......x.......', BD:'x.....xx..x.....', SD:'....x.......x...', CH:'x.x.x.x.x.x.x.x.'}]
];
for (const [n, t, s] of HH){ groove('Hip-Hop', n, t, 0.35, s, 'fill'); groove('Hip-Hop', n + ' (Answer)', t, 0.35, s, 'answer'); }

// ---- Trap ----
const TP = [
  ['Trap Bounce', 140, {AC:'x.......x.......', BD:'x......x..x.....', SD:'........x.......', CH:'x.x.x.x.x.x.xxxx'}],
  ['Trap Triplet Hats', 142, {AC:'x.......x.......', BD:'x.....x.......x.', CP:'........x.......', CH:'x.x.x.x.xxx.x.x.'}],
  ['Trap Slow Boom', 136, {AC:'x...............', BD:'x.........x.....', SD:'........x.......', CH:'xxxxxxxxxxxxxxxx', OH:'..............x.'}],
  ['Trap Rolling', 144, {AC:'x.......x.......', BD:'x..x......x.....', CP:'........x.......', CH:'x.x.xxxxx.x.xxxx'}],
  ['Drill Step', 142, {AC:'x.....x.........', BD:'x.....x.......x.', SD:'......x.......x.', CH:'x..x..x.x..x..x.'}],
  ['Half-Time Knock', 70, {AC:'x.......x.......', BD:'x.x.......x.....', SD:'........x.......', CH:'x.x.x.x.x.x.x.x.', RS:'.............x..'}],
  ['Trap Crash', 140, {AC:'x.......x.......', BD:'x......x.x......', CP:'........x.......', CH:'x.x.x.x.x.x.x.x.', CY:'x...............'}],
  ['Cloud Groove', 130, {AC:'x...............', BD:'x.......x.x.....', SD:'........x.......', CH:'x...x...x...x...', OH:'......x.........'}]
];
for (const [n, t, s] of TP){ groove('Trap', n, t, 0, s, 'busy'); groove('Trap', n + ' (Fill)', t, 0, s, 'fill'); }

// ---- House / Acid ----
const HO = [
  ['House 1', 122, {AC:'x...x...x...x...', BD:'x...x...x...x...', CP:'....x.......x...', OH:'..x...x...x...x.', CH:'x...x...x...x...'}],
  ['House 2', 124, {AC:'x...x...x...x...', BD:'x...x...x...x...', CP:'....x.......x...', CH:'xxxxxxxxxxxxxxxx', OH:'..x...x...x...x.'}],
  ['Deep House', 118, {AC:'x...x...x...x...', BD:'x...x...x...x...', RS:'..x....x..x.....', CH:'..x...x...x...x.', HT:'.......x........'}],
  ['Jack Track', 126, {AC:'x...x...x...x...', BD:'x...x...x...x...', CP:'....x.......x...', CB:'..x...x...x...xx', CH:'..x...x...x...x.'}],
  ['Acid Pulse', 128, {AC:'x...x...x...x...', BD:'x...x...x...x...', CP:'....x.......x...', CH:'x.x.x.x.x.x.x.x.', OH:'..x...x...x...x.', RS:'.x.....x.....x..'}],
  ['Garage Shuffle', 130, {AC:'x.......x.......', BD:'x.....x...x.....', SD:'....x.......x...', CH:'x.xx.xx.x.xx.xx.', RS:'..x.......x.....'}],
  ['Disco House', 120, {AC:'x...x...x...x...', BD:'x...x...x...x...', CP:'....x.......x...', OH:'..x...x...x...x.', CB:'x.x...x.x.x...x.'}],
  ['Conga House', 122, {AC:'x...x...x...x...', BD:'x...x...x...x...', CP:'....x.......x...', LT:'x..x......x.....', MT:'...x...x.....x..', HT:'.....x.....x..x.', CH:'..x...x...x...x.'}],
  ['Hip House', 118, {AC:'x...x...x...x...', BD:'x.....x.x...x...', SD:'....x.......x...', CH:'x.x.x.x.x.x.x.x.', OH:'..............x.'}],
  ['Vogue Beat', 124, {AC:'x...x...x...x...', BD:'x...x...x...x...', CP:'....x.......x...', CY:'x...............', CH:'x.x.x.x.x.x.x.x.', SD:'...........x.xxx'}]
];
for (const [n, t, s] of HO){ groove('House / Acid', n, t, 0.15, s, 'answer'); groove('House / Acid', n + ' (Fill)', t, 0.15, s, 'fill'); }

// ---- Techno ----
const TE = [
  ['Detroit Pulse', 128, {AC:'x...x...x...x...', BD:'x...x...x...x...', CP:'....x.......x...', CB:'..x..x...x..x...', CH:'..x...x...x...x.'}],
  ['Motor City', 132, {AC:'x...x...x...x...', BD:'x...x...x...x...', SD:'....x.......x...', CH:'xxxxxxxxxxxxxxxx', OH:'..x...x...x...x.'}],
  ['Minimal Rim', 126, {AC:'x...x...x...x...', BD:'x...x...x...x...', RS:'...x..x....x..x.', CH:'..x...x...x...x.'}],
  ['Industrial', 136, {AC:'x.x.x.x.x.x.x.x.', BD:'x...x...x...x.x.', SD:'....x.......x...', CP:'....x.......x...', CH:'x.x.x.x.x.x.x.x.'}],
  ['Electro Techno', 130, {AC:'x.......x.......', BD:'x..x..x...x..x..', CP:'....x.......x...', CH:'x.x.x.x.x.x.x.x.', HT:'.............x..', MT:'..............x.'}],
  ['Tom Tunnel', 128, {AC:'x...x...x...x...', BD:'x...x...x...x...', LT:'..x.......x.....', MT:'.....x.......x..', HT:'.......x.......x', CH:'..x...x...x...x.'}],
  ['Dub Techno', 122, {AC:'x...x...x...x...', BD:'x...x...x...x...', RS:'......x.......x.', CH:'..x...x...x...x.', OH:'..x.......x.....'}],
  ['Peak Hour', 134, {AC:'x...x...x...x...', BD:'x...x...x...x...', CP:'....x.......x...', OH:'..x...x...x...x.', CH:'xxxxxxxxxxxxxxxx', CY:'x...............'}]
];
for (const [n, t, s] of TE){ groove('Techno', n, t, 0, s, 'busy'); groove('Techno', n + ' (Fill)', t, 0, s, 'fill'); }

// ---- Latin / conga grooves (use the tom tracks with congas switched in) ----
const LA = [
  ['Conga Groove', 112, {AC:'x.......x.......', BD:'x.....x.x.....x.', LT:'x..x...x..x.....', MT:'...x.......x..x.', HT:'.....x.x.....x..', CP:'....x.......x...', CH:'x.x.x.x.x.x.x.x.'}],
  ['Tumbao', 104, {AC:'x.......x.......', BD:'x..x....x..x....', LT:'......xx......xx', MT:'...x.......x....', HT:'.x...x...x...x..', RS:'x..x..x...x.x...'}],
  ['Mambo', 186, {AC:'x...x...x...x...', BD:'x..x....x..x....', CB:'x.x.xx.xx.x.xx.x', LT:'......xx......xx', HT:'...x.......x....', RS:'..x..x....x.x...'}],
  ['Salsa Clave 3-2', 180, {AC:'x..x...x........', RS:'x..x...x..x.x...', BD:'x.......x.......', LT:'......xx......xx', HT:'...x.......x....', CB:'x.x.x.x.x.x.x.x.'}],
  ['Salsa Clave 2-3', 180, {AC:'..x.x...x..x...x', RS:'..x.x...x..x...x', BD:'x.......x.......', LT:'......xx......xx', HT:'...x.......x....', CB:'x.x.x.x.x.x.x.x.'}],
  ['Merengue', 132, {AC:'x...x...x...x...', BD:'x...x...x...x...', LT:'..x...x...xxx.x.', HT:'x.....x.......x.', CP:'xxxxxxxxxxxxxxxx'}],
  ['Cumbia', 96, {AC:'x...x...x...x...', BD:'x.......x.......', LT:'..x...x...x...x.', HT:'...x.......x....', CP:'x.xxx.xxx.xxx.xx', RS:'....x.......x...'}],
  ['Calypso', 120, {AC:'x.......x.......', BD:'x..x...xx..x...x', SD:'....x.......x...', CB:'x...x.x.x...x.x.', CH:'x.x.x.x.x.x.x.x.'}],
  ['Afro Cuban', 118, {AC:'x.....x.....x...', BD:'x.....x.....x...', LT:'..x.x...x.x...x.', HT:'.x.....x.......x', CB:'x.x.xx.x.xx.x.x.', RS:'x..x..x...x.x...'}],
  ['Reggaeton', 94, {AC:'x..x..x.x..x..x.', BD:'x...x...x...x...', SD:'...x..x....x..x.', CH:'x.x.x.x.x.x.x.x.', RS:'...x..x....x..x.'}]
];
for (const [n, t, s] of LA){ groove('Latin', n, t, 0.1, s, 'answer'); groove('Latin', n + ' (Fill)', t, 0.1, s, 'fill'); }

// ---- Pop / Ballad / Soul ----
const PO = [
  ['Pop 1', 112, {AC:'x.......x.......', BD:'x.......x.x.....', SD:'....x.......x...', CH:'x.x.x.x.x.x.x.x.'}],
  ['Pop 2', 118, {AC:'x...x...x...x...', BD:'x.....x.x.......', CP:'....x.......x...', CH:'x.x.x.x.x.x.x.x.', OH:'..............x.'}],
  ['Synth Pop', 124, {AC:'x...x...x...x...', BD:'x...x...x...x...', SD:'....x.......x...', CH:'xxxxxxxxxxxxxxxx', CB:'..x.......x.....'}],
  ['Soul Ballad', 72, {AC:'x.......x.......', BD:'x......x..x.....', RS:'....x.......x...', CH:'x.x.x.x.x.x.x.x.', CY:'x...............'}],
  ['Quiet Storm', 68, {AC:'x...............', BD:'x.........x.....', RS:'....x.......x...', CH:'x.x.x.x.x.x.x.x.', LT:'..............x.'}],
  ['Slow Jam', 64, {AC:'x.......x.......', BD:'x.....x...x.....', CP:'....x.......x...', CH:'x.xxx.xxx.xxx.xx'}],
  ['Love Ballad', 76, {AC:'x.......x.......', BD:'x.......xx......', SD:'....x.......x...', RS:'.........x......', CY:'x...............', CH:'x.x.x.x.x.x.x.x.'}],
  ['Sexual Groove', 96, {AC:'x.......x.......', BD:'x.....x...x...x.', SD:'....x.......x...', CH:'x.x.x.x.x.x.x.x.', CB:'..x...........x.', HT:'..............x.'}],
  ['New Wave', 132, {AC:'x...x...x...x...', BD:'x.x.x.x.x.x.x.x.', SD:'....x.......x...', CH:'x.x.x.x.x.x.x.x.'}],
  ['Funk Pop', 106, {AC:'x.......x.......', BD:'x..x..x...x.....', SD:'....x..x.x..x...', CH:'x.x.x.x.x.x.x.x.'}]
];
for (const [n, t, s] of PO){ groove('Pop / Ballad', n, t, 0, s, 'fill'); groove('Pop / Ballad', n + ' (Answer)', t, 0, s, 'answer'); }

// ---- the 808 preset-sheet styles ----
groove('Preset Sheet', 'Rock 1', 120, 0, {AC:'x...x...x...x...', BD:'x.......x.x.....', SD:'....x.......x...', CH:'x.x.x.x.x.x.x.x.'});
groove('Preset Sheet', 'Rock 2', 126, 0, {AC:'x.......x.......', BD:'x..x....x.x.....', SD:'....x.......x...', OH:'x.x.x.x.x.x.x.x.'});
groove('Preset Sheet', 'Rock 3', 132, 0, {AC:'x...............', BD:'x.x.....x.x.....', SD:'....x.......x...', CY:'x.x.x.x.x.x.x.x.'}, 'push');
groove('Preset Sheet', 'Disco 1', 118, 0, {AC:'x...x...x...x...', BD:'x...x...x...x...', SD:'....x.......x...', OH:'..x...x...x...x.', CH:'x...x...x...x...'});
groove('Preset Sheet', 'Disco 2', 122, 0, {AC:'x...x...x...x...', BD:'x...x...x...x...', CP:'....x.......x...', OH:'..x...x...x...x.', CB:'x.x.x.x.x.x.x.x.'}, 'answer');
groove('Preset Sheet', 'Samba', 100, 0, {AC:'x..x..x.x..x..x.', BD:'x..xx..xx..xx..x', RS:'x.x..x.x..x.x..x', CH:'xxxxxxxxxxxxxxxx', LT:'......x.......x.'}, 'answer');
groove('Preset Sheet', 'Bossa Nova', 128, 0, {AC:'x..x..x...x..x..', BD:'x..xx..xx..xx..x', RS:'x..x..x...x..x..', CH:'x.x.x.x.x.x.x.x.'}, 'answer');
groove('Preset Sheet', 'Cha-Cha', 120, 0, {AC:'x...x...x...x...', BD:'x.......x.......', RS:'....x.......x...', CB:'x...x...x.x.x...', LT:'..........x.x...', CH:'x.x.x.x.x.x.x.x.'});
groove('Preset Sheet', 'Beguine', 110, 0, {AC:'x.........x.....', BD:'x.......x.......', RS:'......x...x.....', LT:'x...x.x...x...x.', HT:'..x.....x.......', CH:'x.x.x.x.x.x.x.x.'}, 'answer');
groove('Preset Sheet', 'Rhumba', 104, 0, {AC:'x..x...x........', BD:'x..x....x.......', RS:'x..x...x..x.x...', LT:'......xx......xx', CP:'x.xxx.xxx.xxx.xx'}, 'answer');
groove('Preset Sheet', 'Tango', 116, 0, {AC:'x...x...x...x...', BD:'x...x...x...x.x.', SD:'............x.xx', RS:'x...x...x...x...'}, 'answer');
groove('Preset Sheet', 'Reggae', 76, 0.1, {AC:'........x.......', BD:'........x.......', RS:'........x.......', CH:'..x...x...x...x.', OH:'..............x.'}, 'answer');
groove('Preset Sheet', 'Polka', 132, 0, {AC:'x...x...x...x...', BD:'x.......x.......', SD:'....x.......x...', CH:'..x...x...x...x.'}, 'push');
groove('Preset Sheet', 'Swing (Shuffle)', 132, 0.6, {AC:'x...x...x...x...', BD:'x.......x.......', SD:'....x.......x...', CY:'x...x.x.x...x.x.', CH:'....x.......x...'});
groove('Preset Sheet', 'Shuffle Rock', 112, 0.55, {AC:'x.......x.......', BD:'x.......x.x.....', SD:'....x.......x...', CH:'x.xx.xx.xx.xx.x.'});
// 12-step pre-scale: 8th triplets, 4 beats (shuffle, slow 12/8) and 16ths in 3/4 (waltz)
groove('Preset Sheet', 'Waltz', 96, 0, {AC:'x...........', BD:'x...........', RS:'....x...x...', CH:'x.x.x.x.x.x.'}, 'answer', 12, 0);
groove('Preset Sheet', 'Jazz Waltz', 140, 0, {AC:'x...........', BD:'x.......x...', SD:'......x.....', CY:'x...x.x.x.x.', CH:'....x...x...'}, 'fill', 12, 0);
groove('Preset Sheet', 'Slow Rock 12/8', 66, 0, {AC:'x.....x.....', BD:'x.....x.x...', SD:'...x.....x..', CH:'xxxxxxxxxxxx'}, 'fill', 12, 3);
groove('Preset Sheet', 'Blues Shuffle', 100, 0, {AC:'x..x..x..x..', BD:'x.....x.....', SD:'...x.....x..', CH:'x.xx.xx.xx.x'}, 'fill', 12, 3);
groove('Preset Sheet', 'Gospel Shuffle', 84, 0, {AC:'x.....x.....', BD:'x.x...x.x...', CP:'...x.....x..', CH:'x.xx.xx.xx.x'}, 'answer', 12, 3);
groove('Preset Sheet', 'Triplet Trap', 70, 0, {AC:'x.....x.....', BD:'x.....x..x..', SD:'......x.....', CH:'xxxxxxxxxxxx'}, 'busy', 12, 2);
groove('Preset Sheet', 'March', 112, 0, {AC:'x...x...x...x...', BD:'x.......x.......', SD:'x.xxx.x.x.xxx.x.', CY:'x.......x.......'});
groove('Preset Sheet', 'Foxtrot', 120, 0.3, {AC:'x...x...x...x...', BD:'x.......x.......', RS:'....x.......x...', CY:'x...x.x.x...x.x.'}, 'answer');
groove('Preset Sheet', 'Twist', 150, 0, {AC:'x...x...x...x...', BD:'x...x...x...x...', SD:'....x.......x...', CH:'x.x.x.x.x.x.x.x.'}, 'push');
groove('Preset Sheet', 'Funk 1', 104, 0, {AC:'x.......x.......', BD:'x.x....x..x.....', SD:'....x..x.x..x..x', CH:'x.x.x.x.x.x.x.x.'});
groove('Preset Sheet', 'Funk 2', 98, 0, {AC:'x.......x.......', BD:'x..x..x...xx....', SD:'....x.......x...', CH:'xxxxxxxxxxxxxxxx', OH:'......x.........'});
// two-part (32 step) patterns: 1st and 2nd part
rhy('Preset Sheet', 'Electro 32 (1st+2nd Part)', 120, 0,
  pat({AC:'x.......x.......x.......x...x.x.', BD:'x.....x...x.....x.....x...x..x..', SD:'....x.......x.......x.......x.xx', CH:'x.x.x.x.x.x.x.x.x.x.x.x.x.x.....', CB:'..x.......x.......x.............'}, 16, 0, 2),
  pat({AC:'x.......x.......x.......x...x.x.', BD:'x.....x...x.....x.....x.........', SD:'....x.......x.......x...........', HT:'........................x.x.....', MT:'............................x.x.', LT:'..............................xx', CH:'x.x.x.x.x.x.x.x.x.x.x.x.........'}, 16, 0, 2));
rhy('Preset Sheet', 'Boom Bap 32 (1st+2nd Part)', 90, 0.35,
  pat({AC:'x.......x.......x.......x.......', BD:'x.........x.....x......x..x.....', SD:'....x.......x.......x.......x...', CH:'x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.'}, 16, 0, 2),
  pat({AC:'x.......x.......x.......x.......', BD:'x.........x.....x.x.......x.x...', SD:'....x.......x.......x.......x.xx', CH:'x.x.x.x.x.x.x.x.x.x.x.x.x.x.x...', OH:'..............x.................'}, 16, 0, 2));

// ---- fills (intro / fill in 1-4 material) ----
const FI = [
  ['Snare Roll Fill', 120, {AC:'x...x...x...x..x', BD:'x...............', SD:'..x.x.x.xxxxxxxx'}],
  ['Tom Run Fill', 120, {AC:'x.......x.....xx', BD:'x...............', HT:'..xx............', MT:'......xx........', LT:'..........xx.xx.', CY:'...............x'}],
  ['Clap Build Fill', 124, {AC:'x...x...x...xxxx', BD:'x...x...x...x...', CP:'....x...x.x.xxxx'}],
  ['Conga Fill', 112, {AC:'x.......x.......', HT:'x.x...x.x.......', MT:'...x.....x.x....', LT:'.....x......xxxx'}],
  ['Cowbell Fill', 118, {AC:'x...x...x...x...', BD:'x.......x.......', CB:'x.x.x.x.x.xxxxxx'}],
  ['Break Fill', 110, {AC:'x..............x', BD:'x...............', CY:'x...............', SD:'..............xx'}],
  ['Kick Stutter Fill', 140, {AC:'x...x...x...x.xx', BD:'x...x...x.x.xxxx', CH:'x.x.x.x.........'}],
  ['Trigger Tick Fill', 124, {AC:'x.......x.......', BD:'x...............', TRIG:'x.x.x.x.xxxxxxxx', SD:'............x.xx'}]
];
for (const [n, t, s] of FI){ groove('Fill', n, t, 0, s, 'answer'); }

/* ---------------- banks: 12 basic rhythms + 4 intro/fill rhythms ---------------- */
const byCat = c => RHYTHMS.filter(r => r.cat === c);
const fills = byCat('Fill');
function bank(name, list, tempo, swing, fillEvery, fillIdx){
  const basic = [];
  for (let i = 0; i < 12; i++){ const r = list[i % list.length]; basic.push({A:r.rhythm.A, B:r.rhythm.B}); }
  const fill = fillIdx.map(i => ({A:fills[i % fills.length].rhythm.A, B:fills[i % fills.length].rhythm.B}));
  return {name, bank:{basic, fill, tempo, swing, fillEvery}};
}
const pick = (list, step) => list.filter((r, i) => i % step === 0);
const BANKS = [
  bank('Electro Night', pick(byCat('Electro'), 2).concat(byCat('Trigger Out')), 118, 0, 4, [0, 1, 6, 7]),
  bank('Electro Busy', byCat('Electro').filter(r => r.name.endsWith('(Busy)')), 120, 0, 8, [0, 2, 1, 5]),
  bank('Trigger Out Session', byCat('Trigger Out'), 124, 0, 4, [7, 0, 1, 6]),
  bank('Miami Bass', byCat('Miami Bass'), 132, 0.05, 4, [6, 0, 2, 5]),
  bank('Boom Bap', pick(byCat('Hip-Hop'), 2), 90, 0.35, 8, [5, 0, 1, 4]),
  bank('Hip-Hop Answers', byCat('Hip-Hop').filter(r => r.name.endsWith('(Answer)')), 94, 0.35, 8, [5, 4, 0, 1]),
  bank('Trap', byCat('Trap'), 140, 0, 4, [6, 0, 5, 1]),
  bank('House', byCat('House / Acid'), 124, 0.15, 8, [2, 1, 3, 0]),
  bank('Techno', byCat('Techno'), 130, 0, 8, [6, 1, 2, 0]),
  bank('Latin', byCat('Latin'), 116, 0.1, 4, [3, 1, 4, 0]),
  bank('Pop and Ballads', byCat('Pop / Ballad'), 100, 0, 8, [0, 5, 1, 2]),
  bank('Preset Sheet 1', byCat('Preset Sheet').slice(0, 12), 120, 0, 0, [0, 1, 5, 3]),
  bank('Preset Sheet 2', byCat('Preset Sheet').slice(12, 24), 112, 0, 0, [0, 1, 4, 5]),
  bank('Mixed Party', [RHYTHMS[0], RHYTHMS[40], RHYTHMS[50], RHYTHMS[60], RHYTHMS[70], RHYTHMS[80], RHYTHMS[90], RHYTHMS[100], RHYTHMS[110], RHYTHMS[120], RHYTHMS[130], RHYTHMS[33]], 120, 0, 4, [0, 1, 2, 7])
];

/* ---------------- FX: Rev7 + TC 2290 ---------------- */
const FX = [];
const fx = (name, p) => FX.push({name, cat:'FX', params:p});
const R = (program, time, predelay, extra) => Object.assign({'rev.program':program, 'rev.time':time, 'rev.predelay':predelay, 'rev.return':0.5}, extra || {});
const D = (extra) => Object.assign({'dly.return':0.45}, extra);
fx('Dry', {'rev.return':0, 'dly.return':0});
fx('Hall 1 Snare', R(0, 2.6, 20, {'rev.send.SD':0.35, 'rev.send.CP':0.35}));
fx('Hall 2 Big', R(1, 4, 35, {'rev.send.SD':0.4, 'rev.send.CP':0.4, 'rev.send.CY':0.3}));
fx('Room 1 Tight', R(2, 0.8, 5, {'rev.send.SD':0.25, 'rev.send.BD':0.1, 'rev.send.RS':0.25}));
fx('Room 2 Live', R(3, 1.3, 12, {'rev.send.SD':0.3, 'rev.send.LT':0.25, 'rev.send.MT':0.25, 'rev.send.HT':0.25}));
fx('Vocal 1 Clap', R(4, 1.8, 30, {'rev.send.CP':0.45}));
fx('Vocal 2 Soft', R(5, 2.2, 25, {'rev.send.CP':0.4, 'rev.send.RS':0.3}));
fx('Plate 1 Bright', R(6, 1.6, 10, {'rev.send.SD':0.4, 'rev.hf':0.9}));
fx('Plate 2 Long', R(7, 3.2, 15, {'rev.send.SD':0.4, 'rev.send.CP':0.35}));
fx('Early Ref 1', R(8, 0.4, 0, {'rev.send.SD':0.35, 'rev.er':0.9}));
fx('Early Ref 2', R(9, 0.6, 8, {'rev.send.RS':0.35, 'rev.send.CB':0.3, 'rev.er':0.85}));
fx('Gate Reverb', R(10, 0.45, 0, {'rev.send.SD':0.55, 'rev.send.LT':0.45, 'rev.send.MT':0.45, 'rev.send.HT':0.45, 'rev.return':0.6}));
fx('Reverse Gate', R(11, 0.8, 0, {'rev.send.SD':0.5, 'rev.send.CP':0.5, 'rev.return':0.6}));
fx('Dark Hall', R(0, 3, 20, {'rev.send.SD':0.35, 'rev.lpf':3500, 'rev.hf':0.3}));
fx('Bright Room', R(2, 1, 5, {'rev.send.SD':0.3, 'rev.send.CH':0.15, 'rev.hf':1, 'rev.lpf':14000}));
fx('Thin Hall', R(1, 2.5, 20, {'rev.send.SD':0.35, 'rev.hpf':600}));
fx('Dense Plate', R(6, 2, 10, {'rev.send.SD':0.4, 'rev.density':1, 'rev.diffusion':1}));
fx('Sparse Echo Room', R(3, 1.5, 40, {'rev.send.RS':0.35, 'rev.density':0.2, 'rev.diffusion':0.3}));
fx('2290 Quarter', D({'dly.sync':1, 'dly.division':5, 'dly.feedback':0.35, 'dly.send.SD':0.3, 'dly.send.CP':0.3}));
fx('2290 Dotted Eighth', D({'dly.sync':1, 'dly.division':4, 'dly.feedback':0.4, 'dly.send.CP':0.35, 'dly.send.RS':0.3}));
fx('2290 Eighth Ping Pong', D({'dly.sync':1, 'dly.division':3, 'dly.feedback':0.45, 'dly.pingpong':1, 'dly.send.CH':0.3, 'dly.send.CB':0.3}));
fx('2290 Sixteenth', D({'dly.sync':1, 'dly.division':1, 'dly.feedback':0.3, 'dly.send.RS':0.35}));
fx('2290 Slapback', D({'dly.time':95, 'dly.feedback':0.08, 'dly.send.SD':0.35}));
fx('2290 Dub Throw', D({'dly.sync':1, 'dly.division':5, 'dly.feedback':0.75, 'dly.hicut':2800, 'dly.lowcut':250, 'dly.send.SD':0.5, 'dly.send.RS':0.45, 'dly.return':0.55}));
fx('2290 Dynamic Delay', D({'dly.sync':1, 'dly.division':4, 'dly.feedback':0.55, 'dly.duck':0.85, 'dly.send.SD':0.4, 'dly.send.CP':0.4, 'dly.return':0.6}));
fx('2290 Chorus', D({'dly.time':18, 'dly.feedback':0.15, 'dly.modDepth':0.6, 'dly.modRate':0.8, 'dly.send.CH':0.4, 'dly.send.OH':0.4, 'dly.send.CP':0.3}));
fx('2290 Flanger', D({'dly.time':3, 'dly.feedback':0.65, 'dly.modDepth':0.9, 'dly.modRate':0.2, 'dly.send.CY':0.45, 'dly.send.OH':0.45, 'dly.send.CH':0.3}));
fx('2290 Long Tape', D({'dly.time':620, 'dly.feedback':0.55, 'dly.hicut':3000, 'dly.modDepth':0.2, 'dly.modRate':0.4, 'dly.send.SD':0.3}));
fx('2290 Robot Echo', D({'dly.time':45, 'dly.feedback':0.7, 'dly.send.CB':0.45, 'dly.send.TRIG':0.45}));
fx('Trigger Tick Echo', D({'dly.sync':1, 'dly.division':3, 'dly.feedback':0.45, 'dly.pingpong':1, 'dly.send.TRIG':0.5, 'rev.program':2, 'rev.time':0.8, 'rev.send.TRIG':0.2, 'rev.return':0.4}));
fx('Electro Space', Object.assign(R(6, 1.8, 15, {'rev.send.CP':0.35, 'rev.send.SD':0.25}), D({'dly.sync':1, 'dly.division':3, 'dly.feedback':0.35, 'dly.send.CB':0.3})));
fx('Dub Chamber', Object.assign(R(7, 3, 20, {'rev.send.SD':0.3, 'rev.send.RS':0.3}), D({'dly.sync':1, 'dly.division':5, 'dly.feedback':0.7, 'dly.hicut':3000, 'dly.send.SD':0.45, 'dly.send.RS':0.45})));
fx('Stadium', Object.assign(R(1, 5, 50, {'rev.send.SD':0.45, 'rev.send.CP':0.45, 'rev.send.HT':0.35, 'rev.send.MT':0.35, 'rev.send.LT':0.35}), D({'dly.time':240, 'dly.feedback':0.2, 'dly.send.SD':0.2})));
fx('Ambient Wash', Object.assign(R(0, 8, 80, {'rev.send.CP':0.5, 'rev.send.CY':0.5, 'rev.send.RS':0.5, 'rev.return':0.6}), D({'dly.sync':1, 'dly.division':6, 'dly.feedback':0.6, 'dly.modDepth':0.3, 'dly.send.RS':0.4})));
fx('Gated Echo', Object.assign(R(10, 0.5, 0, {'rev.send.SD':0.5}), D({'dly.sync':1, 'dly.division':3, 'dly.feedback':0.3, 'dly.send.SD':0.3})));

const out = {EIGHT_KITS:KITS, EIGHT_RHYTHMS:RHYTHMS, EIGHT_BANKS:BANKS, EIGHT_FX:FX, EIGHT_STOCK:STOCK};
Object.assign(g, out);
if (typeof module !== 'undefined' && module.exports) module.exports = out;
})(typeof globalThis !== 'undefined' ? globalThis : this);
