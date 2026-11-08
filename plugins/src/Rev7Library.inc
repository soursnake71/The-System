// The Omega REV7 preset library: 100 programs made for this plugin, grouped "Category / Name" (the LCD menu shows one
// submenu per category). Each is a REV7 program number plus its settings, the same way a user memory 31-90 is
// stored on the unit. Included by Rev7.cpp, inside makeSpec(), where L(category, name, program, values) is defined.
//
// The "2 Live Crew" set is written for the Miami bass sound of "Throw the D" (1986, Luke Skyywalker Records), which
// was named as a REV7 record by the person who asked for it. The settings are this plugin's, not the record's.

// ---- 2 Live Crew / Miami bass (12)
L("2 Live Crew", "Throw the D - vocal slap echo", 8, {{"fb", .28}, {"idly", .1}, {"dlyL", 118}, {"dlyR", 124}, {"hdamp", 6}, {"omix", .28}});
L("2 Live Crew", "Throw the D - shout-back hall", 2, {{"rt", 1.7}, {"idly", 22}, {"hi", .45}, {"lo", .8}, {"diff", 6}, {"omix", .3}});
L("2 Live Crew", "Throw the D - 808 kick room", 21, {{"live", 2}, {"idly", 4}, {"flev", .5}, {"mode", 5}, {"size", 1.1}, {"diff", 4}, {"omix", .18}});
L("2 Live Crew", "Throw the D - clap plate", 29, {{"rt", .7}, {"idly", 3}, {"hi", .3}, {"lo", 1.6}, {"diff", 8}, {"omix", .3}});
L("2 Live Crew", "Throw the D - gated snare", 23, {{"live", 4}, {"idly", 8}, {"mode", 3}, {"size", 1.2}, {"diff", 7}, {"omix", .3}});
L("2 Live Crew", "Throw the D - scratch flange", 9, {{"fb", .55}, {"mfreq", .35}, {"mdep", .7}, {"mdly", 1.6}, {"omix", .45}});
L("2 Live Crew", "Throw the D - cowbell ping-pong", 7, {{"idly", .1}, {"dlyL", 236}, {"dlyR", 472}, {"dlev", .6}, {"omix", .3}});
L("2 Live Crew", "Throw the D - symphonic hook double", 15, {{"mfreq", .5}, {"mdep", .4}, {"idly", 12}, {"omix", .35}});
L("2 Live Crew", "Throw the D - crowd room", 17, {{"rt", 1.3}, {"idly", 10}, {"hi", .35}, {"omix", .3}});
L("2 Live Crew", "Miami bass - reverse kick swell", 24, {{"live", 6}, {"idly", 2}, {"mode", 4}, {"size", 3.0}, {"diff", 6}, {"omix", .35}});
L("2 Live Crew", "Miami bass - long 808 tail", 1, {{"rt", 3.5}, {"idly", 15}, {"hi", .2}, {"lo", 2.0}, {"diff", 4}, {"omix", .22}});
L("2 Live Crew", "Miami bass - stab echo", 8, {{"fb", .5}, {"idly", .1}, {"dlyL", 236}, {"dlyR", 354}, {"hdamp", 8}, {"omix", .3}});

// ---- Symphonic (program 15) (10)
L("Symphonic", "Classic symphonic", 15, {{"mfreq", .6}, {"mdep", .5}, {"omix", .5}});
L("Symphonic", "Slow wide strings", 15, {{"mfreq", .2}, {"mdep", .75}, {"omix", .55}});
L("Symphonic", "Shimmer keys", 15, {{"mfreq", 1.2}, {"mdep", .35}, {"omix", .45}});
L("Symphonic", "Vocal thickener", 15, {{"mfreq", .45}, {"mdep", .3}, {"idly", 8}, {"omix", .35}});
L("Symphonic", "Detuned pad", 15, {{"mfreq", .3}, {"mdep", 1.0}, {"omix", .5}});
L("Symphonic", "Guitar ensemble", 15, {{"mfreq", .8}, {"mdep", .55}, {"idly", 4}, {"omix", .45}});
L("Symphonic", "Seasick", 15, {{"mfreq", 3.5}, {"mdep", .9}, {"omix", .5}});
L("Symphonic", "Fretless bass", 15, {{"mfreq", .4}, {"mdep", .4}, {"eq", 1}, {"eqlf", 150}, {"eqlg", -8}, {"omix", .4}});
L("Symphonic", "Electric piano", 15, {{"mfreq", .9}, {"mdep", .45}, {"omix", .45}});
L("Symphonic", "Symphonic + 1st ref", 15, {{"mfreq", .55}, {"mdep", .5}, {"fdly", 22}, {"flev", .45}, {"omix", .5}});

// ---- Drums (14)
L("Drums", "Big 80s snare hall", 1, {{"rt", 2.2}, {"idly", 12}, {"hi", .55}, {"lo", .8}, {"diff", 8}, {"omix", .3}});
L("Drums", "Tight snare plate", 22, {{"rt", .9}, {"idly", 6}, {"hi", .7}, {"omix", .3}});
L("Drums", "Gated snare long", 23, {{"live", 7}, {"idly", 10}, {"mode", 3}, {"size", 2.2}, {"diff", 8}, {"omix", .35}});
L("Drums", "Gated snare short", 23, {{"live", 3}, {"idly", 5}, {"mode", 3}, {"size", 1.0}, {"diff", 6}, {"omix", .3}});
L("Drums", "Kick ambience", 21, {{"live", 3}, {"idly", 6}, {"flev", .6}, {"mode", 1}, {"size", 1.4}, {"diff", 5}, {"omix", .2}});
L("Drums", "Tom room", 30, {{"live", 5}, {"idly", 8}, {"mode", 2}, {"size", 1.8}, {"diff", 6}, {"omix", .25}});
L("Drums", "Overhead room", 17, {{"rt", 1.0}, {"idly", 6}, {"hi", .5}, {"omix", .2}});
L("Drums", "Hi-hat sparkle", 4, {{"rt", .8}, {"idly", 2}, {"hi", 1.0}, {"lo", .3}, {"diff", 6}, {"omix", .2}});
L("Drums", "Rimshot slap", 7, {{"idly", .1}, {"dlyL", 58}, {"dlyR", 71}, {"dlev", .5}, {"omix", .25}});
L("Drums", "Reverse snare", 24, {{"live", 6}, {"idly", 10}, {"mode", 4}, {"size", 2.8}, {"diff", 7}, {"omix", .4}});
L("Drums", "Clap hall", 29, {{"rt", 1.4}, {"idly", 15}, {"hi", .4}, {"lo", 1.2}, {"diff", 8}, {"omix", .3}});
L("Drums", "Percussion plate bright", 4, {{"rt", 1.6}, {"idly", 4}, {"hi", .8}, {"lo", .9}, {"diff", 7}, {"omix", .3}});
L("Drums", "Spring drums", 30, {{"live", 6}, {"idly", 3}, {"mode", 6}, {"size", 1.2}, {"diff", 4}, {"omix", .3}});
L("Drums", "Drum machine wash", 1, {{"rt", 4.5}, {"idly", 25}, {"hi", .35}, {"lo", 1.4}, {"diff", 9}, {"omix", .3}});

// ---- Vocals (14)
L("Vocals", "Lead vocal plate", 3, {{"rt", 1.9}, {"idly", 40}, {"hi", .4}, {"diff", 6}, {"omix", .25}});
L("Vocals", "Ballad hall", 1, {{"rt", 3.0}, {"idly", 55}, {"hi", .35}, {"lo", 1.1}, {"diff", 7}, {"omix", .28}});
L("Vocals", "Rap vocal room", 16, {{"rt", 0.9}, {"idly", 12}, {"hi", .35}, {"omix", .2}});
L("Vocals", "Rap slap", 7, {{"idly", .1}, {"dlyL", 95}, {"dlyR", 103}, {"dlev", .55}, {"omix", .22}});
L("Vocals", "Quarter echo", 8, {{"fb", .35}, {"dlyL", 450}, {"dlyR", 450}, {"hdamp", 7}, {"omix", .25}});
L("Vocals", "Eighth echo", 8, {{"fb", .3}, {"dlyL", 250}, {"dlyR", 260}, {"hdamp", 6}, {"omix", .25}});
L("Vocals", "Double tracking (ADT)", 1, {{"rt", .3}, {"idly", .1}, {"fdly", 28}, {"flev", .9}, {"hi", .2}, {"omix", .35}});
L("Vocals", "Backing vocal chorus", 11, {{"mfreq", .4}, {"mdep", .55}, {"amdep", .15}, {"omix", .45}});
L("Vocals", "Gospel church", 1, {{"rt", 4.2}, {"idly", 45}, {"hi", .4}, {"lo", 1.3}, {"diff", 8}, {"omix", .3}});
L("Vocals", "Telephone room", 26, {{"live", 2}, {"idly", 4}, {"flev", .4}, {"mode", 1}, {"size", .6}, {"diff", 3}, {"eq", 1}, {"eqlf", 300}, {"eqlg", -12}, {"eqhf", 4000}, {"eqhg", -12}, {"omix", .4}});
L("Vocals", "Whisper plate", 3, {{"rt", 2.8}, {"idly", 60}, {"hi", .6}, {"lo", .6}, {"diff", 8}, {"omix", .3}});
L("Vocals", "Spoken word ambience", 5, {{"live", 3}, {"idly", 8}, {"mode", 1}, {"size", 1.2}, {"diff", 6}, {"omix", .2}});
L("Vocals", "Vocal flange throw", 10, {{"rt", 2.0}, {"mfreq", .25}, {"mdep", .6}, {"mdly", 2.0}, {"omix", .35}});
L("Vocals", "Hype man hall", 2, {{"rt", 1.4}, {"idly", 18}, {"hi", .5}, {"diff", 6}, {"omix", .3}});

// ---- Keys and synths (12)
L("Keys", "Grand piano hall", 25, {{"live", 5}, {"idly", 15}, {"flev", .6}, {"mode", 2}, {"size", 2.4}, {"diff", 7}, {"omix", .3}});
L("Keys", "Rhodes chorus", 12, {{"mfreq", .8}, {"mdep", .45}, {"amdep", .2}, {"omix", .45}});
L("Keys", "Organ room", 26, {{"live", 5}, {"idly", 10}, {"flev", .55}, {"mode", 1}, {"size", 2.0}, {"diff", 7}, {"omix", .3}});
L("Keys", "Synth brass hall", 27, {{"live", 5}, {"idly", 14}, {"flev", .5}, {"mode", 2}, {"size", 2.0}, {"diff", 6}, {"omix", .3}});
L("Keys", "Pad infinity", 1, {{"rt", 10}, {"idly", 80}, {"hi", .5}, {"lo", 1.2}, {"diff", 10}, {"omix", .4}});
L("Keys", "Strings concert", 18, {{"rt", 2.8}, {"idly", 20}, {"hi", .4}, {"omix", .3}});
L("Keys", "Synth lead echo", 8, {{"fb", .45}, {"dlyL", 330}, {"dlyR", 335}, {"hdamp", 5}, {"omix", .3}});
L("Keys", "Arp ping-pong", 7, {{"idly", .1}, {"dlyL", 180}, {"dlyR", 360}, {"dlev", .7}, {"omix", .35}});
L("Keys", "Clav phasing", 13, {{"mfreq", .7}, {"mdep", .6}, {"mdly", 2.0}, {"omix", .5}});
L("Keys", "Vibrato organ", 14, {{"mfreq", 6.5}, {"mdep", .35}, {"omix", .55}});
L("Keys", "Electro stab plate", 4, {{"rt", 1.1}, {"idly", 5}, {"hi", .6}, {"diff", 7}, {"omix", .3}});
L("Keys", "Reverb flange pad", 10, {{"rt", 4.0}, {"mfreq", .15}, {"mdep", .8}, {"mdly", 3.0}, {"omix", .4}});

// ---- Guitar and bass (10)
L("Guitar & Bass", "Clean guitar spring", 28, {{"live", 6}, {"idly", 5}, {"flev", .6}, {"mode", 6}, {"size", 1.5}, {"diff", 5}, {"omix", .3}});
L("Guitar & Bass", "Chorus guitar", 11, {{"mfreq", .6}, {"mdep", .6}, {"amdep", .1}, {"omix", .5}});
L("Guitar & Bass", "Flanged guitar", 9, {{"fb", .6}, {"mfreq", .2}, {"mdep", .8}, {"mdly", 2.4}, {"omix", .5}});
L("Guitar & Bass", "Lead guitar hall", 1, {{"rt", 2.4}, {"idly", 35}, {"hi", .4}, {"diff", 7}, {"omix", .3}});
L("Guitar & Bass", "Rockabilly slap", 8, {{"fb", .15}, {"dlyL", 105}, {"dlyR", 110}, {"hdamp", 5}, {"omix", .3}});
L("Guitar & Bass", "Dotted echo", 8, {{"fb", .4}, {"dlyL", 375}, {"dlyR", 380}, {"hdamp", 6}, {"omix", .3}});
L("Guitar & Bass", "Bass room", 19, {{"live", 4}, {"idly", 10}, {"flev", .6}, {"mode", 5}, {"size", 1.6}, {"diff", 5}, {"omix", .18}});
L("Guitar & Bass", "Slap bass", 20, {{"live", 3}, {"idly", 8}, {"flev", .65}, {"mode", 5}, {"size", 1.4}, {"diff", 5}, {"omix", .2}});
L("Guitar & Bass", "Bass chorus", 12, {{"mfreq", .5}, {"mdep", .35}, {"amdep", 0}, {"eq", 1}, {"eqlf", 200}, {"eqlg", -10}, {"omix", .4}});
L("Guitar & Bass", "Acoustic room", 28, {{"live", 4}, {"idly", 8}, {"flev", .5}, {"mode", 1}, {"size", 1.8}, {"diff", 6}, {"omix", .25}});

// ---- Electro and hip-hop (12)
L("Electro & Hip-Hop", "Electro funk plate", 4, {{"rt", 1.3}, {"idly", 8}, {"hi", .55}, {"lo", 1.1}, {"diff", 7}, {"omix", .3}});
L("Electro & Hip-Hop", "Robot voice room", 6, {{"live", 2}, {"idly", 3}, {"mode", 5}, {"size", .6}, {"diff", 2}, {"omix", .35}});
L("Electro & Hip-Hop", "Breakdance hall", 2, {{"rt", 2.0}, {"idly", 15}, {"hi", .4}, {"lo", 1.4}, {"diff", 6}, {"omix", .28}});
L("Electro & Hip-Hop", "Scratch echo", 8, {{"fb", .4}, {"dlyL", 160}, {"dlyR", 240}, {"hdamp", 7}, {"omix", .3}});
L("Electro & Hip-Hop", "Orchestra hit hall", 1, {{"rt", 2.8}, {"idly", 20}, {"hi", .3}, {"lo", 1.5}, {"diff", 9}, {"omix", .35}});
L("Electro & Hip-Hop", "Sub drop room", 21, {{"live", 1}, {"idly", 2}, {"flev", .8}, {"mode", 5}, {"size", .8}, {"diff", 3}, {"omix", .15}});
L("Electro & Hip-Hop", "Old school vocal hall", 2, {{"rt", 1.5}, {"idly", 30}, {"hi", .35}, {"diff", 5}, {"omix", .25}});
L("Electro & Hip-Hop", "Laser flange", 9, {{"fb", .9}, {"mfreq", 2.0}, {"mdep", .9}, {"mdly", .6}, {"omix", .45}});
L("Electro & Hip-Hop", "Space echo", 8, {{"fb", .75}, {"dlyL", 420}, {"dlyR", 300}, {"hdamp", 8}, {"omix", .35}});
L("Electro & Hip-Hop", "Freestyle synth chorus", 12, {{"mfreq", 1.0}, {"mdep", .55}, {"amdep", .15}, {"omix", .45}});
L("Electro & Hip-Hop", "Reverse gate vocal", 24, {{"live", 5}, {"idly", 15}, {"mode", 4}, {"size", 2.0}, {"diff", 6}, {"omix", .4}});
L("Electro & Hip-Hop", "Boom bap room", 16, {{"rt", .8}, {"idly", 6}, {"hi", .3}, {"omix", .2}});

// ---- Spaces (14)
L("Spaces", "Studio live room", 17, {{"rt", 1.1}, {"idly", 8}, {"hi", .5}, {"omix", .25}});
L("Spaces", "Small club", 2, {{"rt", 1.0}, {"idly", 10}, {"hi", .45}, {"lo", 1.1}, {"diff", 5}, {"omix", .25}});
L("Spaces", "Concert hall", 1, {{"rt", 2.4}, {"idly", 35}, {"hi", .4}, {"lo", 1.2}, {"diff", 7}, {"omix", .28}});
L("Spaces", "Arena", 1, {{"rt", 4.0}, {"idly", 70}, {"hi", .3}, {"lo", 1.5}, {"diff", 8}, {"fdly", 60}, {"flev", .2}, {"omix", .3}});
L("Spaces", "Cathedral", 1, {{"rt", 7.5}, {"idly", 90}, {"hi", .3}, {"lo", 1.7}, {"diff", 10}, {"omix", .35}});
L("Spaces", "Stairwell", 30, {{"live", 9}, {"idly", 6}, {"mode", 3}, {"size", 3.0}, {"diff", 4}, {"omix", .3}});
L("Spaces", "Tiled bathroom", 5, {{"live", 9}, {"idly", 2}, {"mode", 1}, {"size", .7}, {"diff", 3}, {"omix", .3}});
L("Spaces", "Wood room", 5, {{"live", 3}, {"idly", 5}, {"mode", 1}, {"size", 1.5}, {"diff", 7}, {"omix", .25}});
L("Spaces", "Parking garage", 6, {{"live", 8}, {"idly", 12}, {"mode", 2}, {"size", 4.0}, {"diff", 5}, {"omix", .3}});
L("Spaces", "Canyon echo", 8, {{"fb", .45}, {"idly", 40}, {"dlyL", 450}, {"dlyR", 430}, {"hdamp", 9}, {"omix", .3}});
L("Spaces", "Dark chamber", 17, {{"rt", 2.0}, {"idly", 14}, {"hi", .15}, {"omix", .3}});
L("Spaces", "Bright chamber", 3, {{"rt", 1.6}, {"idly", 10}, {"hi", .9}, {"lo", .7}, {"diff", 7}, {"omix", .28}});
L("Spaces", "Huge room size", 30, {{"live", 7}, {"idly", 25}, {"mode", 2}, {"size", 12}, {"diff", 8}, {"omix", .3}});
L("Spaces", "Plate spring", 5, {{"live", 6}, {"idly", 4}, {"mode", 6}, {"size", 2.0}, {"diff", 6}, {"omix", .3}});

// ---- Special effects (2)
L("Special FX", "Infinite freeze echo", 8, {{"fb", .99}, {"dlyL", 450}, {"dlyR", 449}, {"hdamp", 1}, {"omix", .35}});
L("Special FX", "Through the wall", 2, {{"rt", 1.8}, {"idly", 30}, {"hi", .1}, {"eq", 1}, {"eqhf", 3000}, {"eqhg", -15}, {"omix", .6}});
