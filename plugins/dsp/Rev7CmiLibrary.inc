// 100 REV7 programs made for the Omega CMI's effects section (Page E): settings for the CMI's own sounds
// (choirs, strings, orchestra hits, Page S drums, basses, keys, leads) and for mixing. This plugin's own settings.
// Included by Rev7Spec.h after the main library, where L(category, name, program, values) is defined.


// ---- Fairlight Strings & Choirs
L("Fairlight Strings & Choirs", "Aah choir cathedral", 1, {{"rt", 6.5}, {"idly", 55}, {"hi", 0.35}, {"lo", 1.5}, {"diff", 9}, {"fdly", 30}, {"flev", 0.3}});
L("Fairlight Strings & Choirs", "Aah choir chamber", 2, {{"rt", 2.2}, {"idly", 25}, {"hi", 0.45}, {"lo", 1.1}, {"diff", 7}});
L("Fairlight Strings & Choirs", "Male choir crypt", 1, {{"rt", 5}, {"idly", 40}, {"hi", 0.25}, {"lo", 1.9}, {"diff", 8}, {"eq", 1}, {"eqlf", 120}, {"eqlg", -4}, {"eqmf", 1000}, {"eqmg", 0}, {"eqhf", 8000}, {"eqhg", -2}});
L("Fairlight Strings & Choirs", "Choir shimmer hall", 10, {{"idly", 0.1}, {"rt", 4.5}, {"mfreq", 0.25}, {"mdep", 0.55}, {"mdly", 3}});
L("Fairlight Strings & Choirs", "Vox pad bloom", 18, {{"rt", 3.2}, {"idly", 35}, {"hi", 0.4}, {"lo", 1.3}, {"diff", 8}});
L("Fairlight Strings & Choirs", "String section stage", 18, {{"rt", 2.4}, {"idly", 18}, {"hi", 0.35}, {"lo", 1.1}, {"diff", 6}, {"fdly", 12}, {"flev", 0.35}});
L("Fairlight Strings & Choirs", "Strings wide symphonic", 15, {{"idly", 0.1}, {"mfreq", 0.35}, {"mdep", 0.7}});
L("Fairlight Strings & Choirs", "Pizzicato room", 16, {{"rt", 1}, {"idly", 8}, {"hi", 0.5}, {"lo", 0.9}, {"diff", 6}});
L("Fairlight Strings & Choirs", "Solo violin hall", 1, {{"rt", 2.9}, {"idly", 28}, {"hi", 0.4}, {"lo", 1.2}, {"diff", 7}, {"fdly", 18}, {"flev", 0.25}});
L("Fairlight Strings & Choirs", "Slow string swell", 1, {{"rt", 4.8}, {"idly", 70}, {"hi", 0.3}, {"lo", 1.4}, {"diff", 9}});
L("Fairlight Strings & Choirs", "Choir reverse swell", 24, {{"live", 7}, {"idly", 4}, {"mode", 4}, {"size", 3.5}, {"diff", 7}, {"flev", 0}});
L("Fairlight Strings & Choirs", "Breath pad air", 3, {{"rt", 3.6}, {"idly", 40}, {"hi", 0.55}, {"lo", 0.8}, {"diff", 9}, {"eq", 1}, {"eqlf", 200}, {"eqlg", -6}, {"eqmf", 1000}, {"eqmg", 0}, {"eqhf", 9000}, {"eqhg", 3}});

// ---- Orchestra Hits
L("Orchestra Hits", "ORCH5 classic hall", 1, {{"rt", 2.6}, {"idly", 20}, {"hi", 0.35}, {"lo", 1.2}, {"diff", 7}});
L("Orchestra Hits", "ORCH5 big stab", 1, {{"rt", 4.2}, {"idly", 35}, {"hi", 0.3}, {"lo", 1.5}, {"diff", 8}, {"fdly", 25}, {"flev", 0.4}});
L("Orchestra Hits", "Orch hit gated", 23, {{"live", 6}, {"idly", 4}, {"mode", 3}, {"size", 1.5}, {"diff", 8}, {"flev", 0}});
L("Orchestra Hits", "Orch hit reverse", 24, {{"live", 7}, {"idly", 2}, {"mode", 4}, {"size", 2.6}, {"diff", 6}, {"flev", 0}});
L("Orchestra Hits", "Orch hit slap", 7, {{"idly", 0.1}, {"dlyL", 95}, {"dlyR", 140}, {"dlev", 0.55}});
L("Orchestra Hits", "Orch hit echo hall", 8, {{"idly", 0.1}, {"dlyL", 375}, {"dlyR", 380}, {"fb", 0.45}, {"hdamp", 7}});
L("Orchestra Hits", "Stab plate", 4, {{"rt", 1.6}, {"idly", 6}, {"hi", 0.55}, {"lo", 1}, {"diff", 7}});
L("Orchestra Hits", "Stab short room", 16, {{"rt", 0.8}, {"idly", 4}, {"hi", 0.5}, {"lo", 0.9}, {"diff", 6}});
L("Orchestra Hits", "Hit into flange", 10, {{"idly", 0.1}, {"rt", 2.8}, {"mfreq", 0.3}, {"mdep", 0.7}, {"mdly", 2.2}});
L("Orchestra Hits", "Hit stadium", 1, {{"rt", 6}, {"idly", 80}, {"hi", 0.3}, {"lo", 1.7}, {"diff", 9}});

// ---- Fairlight Drums
L("Fairlight Drums", "Page S kit room", 16, {{"rt", 1}, {"idly", 6}, {"hi", 0.45}, {"lo", 1}, {"diff", 6}});
L("Fairlight Drums", "Page S kit gated", 23, {{"live", 5}, {"idly", 6}, {"mode", 3}, {"size", 1.4}, {"diff", 7}, {"flev", 0}});
L("Fairlight Drums", "Big gated snare", 23, {{"live", 7}, {"idly", 10}, {"mode", 3}, {"size", 1.9}, {"diff", 8}, {"flev", 0}});
L("Fairlight Drums", "Tight snare plate", 22, {{"rt", 0.9}, {"idly", 4}, {"hi", 0.7}, {"lo", 0.9}, {"diff", 7}});
L("Fairlight Drums", "Snare ambience", 20, {{"live", 4}, {"idly", 8}, {"mode", 5}, {"size", 1.6}, {"diff", 6}, {"flev", 0.4}});
L("Fairlight Drums", "Kick room short", 21, {{"live", 2}, {"idly", 3}, {"mode", 5}, {"size", 1}, {"diff", 4}, {"flev", 0.6}});
L("Fairlight Drums", "Kick boom", 21, {{"live", 3}, {"idly", 5}, {"mode", 1}, {"size", 2.2}, {"diff", 5}, {"flev", 0.5}, {"eq", 1}, {"eqlf", 80}, {"eqlg", 6}, {"eqmf", 1000}, {"eqmg", -3}, {"eqhf", 8000}, {"eqhg", -6}});
L("Fairlight Drums", "Toms 80s hall", 1, {{"rt", 1.8}, {"idly", 12}, {"hi", 0.4}, {"lo", 1.3}, {"diff", 7}});
L("Fairlight Drums", "Toms gated", 23, {{"live", 6}, {"idly", 8}, {"mode", 3}, {"size", 1.7}, {"diff", 8}, {"flev", 0}});
L("Fairlight Drums", "Hats shimmer", 4, {{"rt", 0.7}, {"idly", 2}, {"hi", 0.8}, {"lo", 0.6}, {"diff", 8}});
L("Fairlight Drums", "Clap plate", 29, {{"rt", 0.6}, {"idly", 3}, {"hi", 0.4}, {"lo", 1.4}, {"diff", 8}});
L("Fairlight Drums", "Clap echo", 8, {{"idly", 0.1}, {"dlyL", 236}, {"dlyR", 354}, {"fb", 0.3}, {"hdamp", 6}});
L("Fairlight Drums", "Cross-stick room", 17, {{"rt", 0.9}, {"idly", 6}, {"hi", 0.5}, {"lo", 1}, {"diff", 5}});
L("Fairlight Drums", "Perc spring", 26, {{"live", 5}, {"idly", 6}, {"mode", 6}, {"size", 1.4}, {"diff", 6}, {"flev", 0.5}});

// ---- Basses
L("Basses", "Bass dry room", 19, {{"live", 3}, {"idly", 6}, {"mode", 5}, {"size", 1.2}, {"diff", 4}, {"flev", 0.6}});
L("Basses", "Bass short plate", 4, {{"rt", 0.6}, {"idly", 2}, {"hi", 0.5}, {"lo", 0.6}, {"diff", 5}, {"eq", 1}, {"eqlf", 250}, {"eqlg", -10}, {"eqmf", 1000}, {"eqmg", 0}, {"eqhf", 8000}, {"eqhg", 0}});
L("Basses", "Synth bass chorus", 12, {{"idly", 0.1}, {"mfreq", 0.5}, {"mdep", 0.4}, {"amdep", 0.05}});
L("Basses", "Slap bass slap", 7, {{"idly", 0.1}, {"dlyL", 60}, {"dlyR", 85}, {"dlev", 0.4}, {"eq", 1}, {"eqlf", 200}, {"eqlg", -8}, {"eqmf", 1000}, {"eqmg", 0}, {"eqhf", 8000}, {"eqhg", 2}});
L("Basses", "Fretless symphonic", 15, {{"idly", 0.1}, {"mfreq", 0.4}, {"mdep", 0.45}, {"eq", 1}, {"eqlf", 150}, {"eqlg", -8}, {"eqmf", 1000}, {"eqmg", 0}, {"eqhf", 8000}, {"eqhg", 0}});
L("Basses", "Bass flange", 9, {{"idly", 0.1}, {"fb", 0.4}, {"mfreq", 0.25}, {"mdep", 0.6}, {"mdly", 1.4}});
L("Basses", "Sub bass air", 3, {{"rt", 1.2}, {"idly", 10}, {"hi", 0.6}, {"lo", 0.3}, {"diff", 6}, {"eq", 1}, {"eqlf", 400}, {"eqlg", -15}, {"eqmf", 1000}, {"eqmg", 0}, {"eqhf", 8000}, {"eqhg", 2}});
L("Basses", "Bass echo 8th", 8, {{"idly", 0.1}, {"dlyL", 250}, {"dlyR", 250}, {"fb", 0.2}, {"hdamp", 8}, {"eq", 1}, {"eqlf", 250}, {"eqlg", -9}, {"eqmf", 1000}, {"eqmg", 0}, {"eqhf", 8000}, {"eqhg", -3}});

// ---- Keys & Bells
L("Keys & Bells", "Grainy piano hall", 18, {{"rt", 2.3}, {"idly", 15}, {"hi", 0.35}, {"lo", 1.1}, {"diff", 7}});
L("Keys & Bells", "Piano stage", 25, {{"live", 4}, {"idly", 10}, {"mode", 6}, {"size", 2}, {"diff", 5}, {"flev", 0.6}});
L("Keys & Bells", "Harpsichord room", 16, {{"rt", 1.3}, {"idly", 10}, {"hi", 0.55}, {"lo", 0.9}, {"diff", 6}});
L("Keys & Bells", "Clav chorus", 11, {{"idly", 0.1}, {"mfreq", 0.7}, {"mdep", 0.35}, {"amdep", 0.15}});
L("Keys & Bells", "Bells cathedral", 1, {{"rt", 7.5}, {"idly", 40}, {"hi", 0.5}, {"lo", 1}, {"diff", 9}});
L("Keys & Bells", "Tubular bells hall", 1, {{"rt", 4}, {"idly", 30}, {"hi", 0.45}, {"lo", 1.2}, {"diff", 8}});
L("Keys & Bells", "Marimba room", 17, {{"rt", 1.2}, {"idly", 9}, {"hi", 0.5}, {"lo", 0.9}, {"diff", 6}});
L("Keys & Bells", "Kalimba echo", 8, {{"idly", 0.1}, {"dlyL", 310}, {"dlyR", 465}, {"fb", 0.35}, {"hdamp", 5}});
L("Keys & Bells", "Organ rotary", 13, {{"idly", 0.1}, {"mfreq", 0.8}, {"mdep", 0.6}, {"mdly", 1.5}});
L("Keys & Bells", "Electric piano tremolo", 14, {{"idly", 0.1}, {"mfreq", 4.5}, {"mdep", 0.35}});

// ---- Leads & Voices
L("Leads & Voices", "Shakuhachi temple", 1, {{"rt", 5.5}, {"idly", 45}, {"hi", 0.3}, {"lo", 1.5}, {"diff", 8}, {"fdly", 30}, {"flev", 0.25}});
L("Leads & Voices", "Pan pipes valley", 8, {{"idly", 0.1}, {"dlyL", 420}, {"dlyR", 430}, {"fb", 0.45}, {"hdamp", 7}});
L("Leads & Voices", "Sax lead plate", 3, {{"rt", 1.9}, {"idly", 25}, {"hi", 0.45}, {"lo", 1}, {"diff", 7}});
L("Leads & Voices", "Guitar lead echo", 8, {{"idly", 0.1}, {"dlyL", 340}, {"dlyR", 345}, {"fb", 0.4}, {"hdamp", 6}});
L("Leads & Voices", "Guitar wide chorus", 11, {{"idly", 0.1}, {"mfreq", 0.3}, {"mdep", 0.55}, {"amdep", 0.2}});
L("Leads & Voices", "Vocal sample slap", 7, {{"idly", 0.1}, {"dlyL", 110}, {"dlyR", 125}, {"dlev", 0.5}});
L("Leads & Voices", "Vocal stutter echo", 8, {{"idly", 0.1}, {"dlyL", 125}, {"dlyR", 125}, {"fb", 0.55}, {"hdamp", 4}});
L("Leads & Voices", "Crowd hey hall", 2, {{"rt", 1.6}, {"idly", 20}, {"hi", 0.4}, {"lo", 1}, {"diff", 6}});
L("Leads & Voices", "Synth lead flange", 9, {{"idly", 0.1}, {"fb", 0.55}, {"mfreq", 0.4}, {"mdep", 0.6}, {"mdly", 1.6}});
L("Leads & Voices", "Talking voice room", 30, {{"live", 4}, {"idly", 12}, {"mode", 2}, {"size", 1.8}, {"diff", 5}, {"flev", 0}});

// ---- 80s Production
L("80s Production", "Synth-pop snare", 23, {{"live", 6}, {"idly", 8}, {"mode", 3}, {"size", 1.6}, {"diff", 8}, {"flev", 0}});
L("80s Production", "Synth-pop vocal plate", 3, {{"rt", 2}, {"idly", 50}, {"hi", 0.4}, {"lo", 0.9}, {"diff", 7}});
L("80s Production", "New wave stab hall", 2, {{"rt", 1.9}, {"idly", 15}, {"hi", 0.4}, {"lo", 1.1}, {"diff", 7}});
L("80s Production", "Art-pop reverse", 24, {{"live", 6}, {"idly", 3}, {"mode", 4}, {"size", 2}, {"diff", 6}, {"flev", 0}});
L("80s Production", "Ballad vocal hall", 1, {{"rt", 3}, {"idly", 60}, {"hi", 0.35}, {"lo", 1}, {"diff", 7}});
L("80s Production", "Power drums room", 30, {{"live", 6}, {"idly", 15}, {"mode", 2}, {"size", 2.4}, {"diff", 6}, {"flev", 0}});
L("80s Production", "Film score hall", 1, {{"rt", 4.5}, {"idly", 35}, {"hi", 0.35}, {"lo", 1.4}, {"diff", 8}});
L("80s Production", "Soundtrack cue", 18, {{"rt", 3.4}, {"idly", 25}, {"hi", 0.3}, {"lo", 1.3}, {"diff", 8}});
L("80s Production", "Electro clap echo", 8, {{"idly", 0.1}, {"dlyL", 176}, {"dlyR", 264}, {"fb", 0.3}, {"hdamp", 6}});
L("80s Production", "Electro vocal", 12, {{"idly", 0.1}, {"mfreq", 0.6}, {"mdep", 0.5}, {"amdep", 0.1}});
L("80s Production", "Dub stab", 8, {{"idly", 0.1}, {"dlyL", 375}, {"dlyR", 500}, {"fb", 0.6}, {"hdamp", 8}});
L("80s Production", "Dance hall short", 2, {{"rt", 1.2}, {"idly", 10}, {"hi", 0.5}, {"lo", 1}, {"diff", 6}});
L("80s Production", "Funk guitar slap", 7, {{"idly", 0.1}, {"dlyL", 70}, {"dlyR", 105}, {"dlev", 0.45}});
L("80s Production", "Big ballad drums", 1, {{"rt", 2.4}, {"idly", 12}, {"hi", 0.4}, {"lo", 1.2}, {"diff", 7}});

// ---- Sound Design
L("Sound Design", "Endless space", 1, {{"rt", 10}, {"idly", 100}, {"hi", 0.2}, {"lo", 2.4}, {"diff", 10}});
L("Sound Design", "Frozen cave", 1, {{"rt", 9}, {"idly", 90}, {"hi", 0.1}, {"lo", 2}, {"diff", 10}, {"eq", 1}, {"eqlf", 300}, {"eqlg", -6}, {"eqmf", 1000}, {"eqmg", 0}, {"eqhf", 5000}, {"eqhg", -8}});
L("Sound Design", "Metallic tunnel", 9, {{"idly", 0.1}, {"fb", 0.85}, {"mfreq", 0.1}, {"mdep", 0.3}, {"mdly", 6}});
L("Sound Design", "Robot voice", 9, {{"idly", 0.1}, {"fb", 0.9}, {"mfreq", 8}, {"mdep", 0.2}, {"mdly", 1}});
L("Sound Design", "Spring tank", 26, {{"live", 6}, {"idly", 4}, {"mode", 6}, {"size", 1}, {"diff", 3}, {"flev", 0.5}});
L("Sound Design", "Haunted hallway", 10, {{"idly", 0.1}, {"rt", 5.5}, {"mfreq", 0.15}, {"mdep", 0.8}, {"mdly", 5}});
L("Sound Design", "Space echo", 8, {{"idly", 0.1}, {"dlyL", 600}, {"dlyR", 450}, {"fb", 0.7}, {"hdamp", 8}});
L("Sound Design", "Ping-pong void", 7, {{"idly", 0.1}, {"dlyL", 300}, {"dlyR", 600}, {"dlev", 0.8}});
L("Sound Design", "Underwater", 13, {{"idly", 0.1}, {"mfreq", 0.15}, {"mdep", 0.9}, {"mdly", 8}, {"eq", 1}, {"eqlf", 200}, {"eqlg", 0}, {"eqmf", 1000}, {"eqmg", 0}, {"eqhf", 3000}, {"eqhg", -12}});
L("Sound Design", "Seasick tape", 15, {{"idly", 0.1}, {"mfreq", 4}, {"mdep", 0.9}});
L("Sound Design", "Helicopter", 14, {{"idly", 0.1}, {"mfreq", 12}, {"mdep", 0.85}});
L("Sound Design", "Weather storm room", 1, {{"rt", 3.5}, {"idly", 20}, {"hi", 0.3}, {"lo", 2}, {"diff", 9}});

// ---- Mixing Tools
L("Mixing Tools", "Small booth", 5, {{"live", 2}, {"idly", 2}, {"mode", 1}, {"size", 0.5}, {"diff", 4}, {"flev", 0}});
L("Mixing Tools", "Small room", 16, {{"rt", 0.6}, {"idly", 4}, {"hi", 0.5}, {"lo", 0.9}, {"diff", 5}});
L("Mixing Tools", "Medium room", 17, {{"rt", 1.1}, {"idly", 8}, {"hi", 0.45}, {"lo", 1}, {"diff", 6}});
L("Mixing Tools", "Live room", 30, {{"live", 5}, {"idly", 10}, {"mode", 2}, {"size", 2}, {"diff", 5}, {"flev", 0}});
L("Mixing Tools", "Bright plate", 4, {{"rt", 1.5}, {"idly", 5}, {"hi", 0.7}, {"lo", 0.8}, {"diff", 8}, {"eq", 1}, {"eqlf", 300}, {"eqlg", -4}, {"eqmf", 1000}, {"eqmg", 0}, {"eqhf", 8000}, {"eqhg", 3}});
L("Mixing Tools", "Dark plate", 3, {{"rt", 2}, {"idly", 15}, {"hi", 0.2}, {"lo", 1.2}, {"diff", 7}});
L("Mixing Tools", "Short hall", 2, {{"rt", 1.4}, {"idly", 12}, {"hi", 0.45}, {"lo", 1}, {"diff", 6}});
L("Mixing Tools", "Long hall", 1, {{"rt", 3.8}, {"idly", 30}, {"hi", 0.35}, {"lo", 1.3}, {"diff", 8}});
L("Mixing Tools", "Stereo widener", 7, {{"idly", 0.1}, {"dlyL", 12}, {"dlyR", 19}, {"dlev", 0.7}});
L("Mixing Tools", "Doubler", 12, {{"idly", 0.1}, {"mfreq", 0.4}, {"mdep", 0.3}, {"amdep", 0}});
