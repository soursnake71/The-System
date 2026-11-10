"""The drum kit: every piece the engine can write, with its General MIDI note.

The separation model gives six stems (kick, snare, toms, hi-hat, ride, crash); the articulation stage then splits
them into the pieces below (closed / pedal / open hi-hat, up to six toms by pitch, ride bow / bell, two crashes,
snare / side stick).
"""

# name -> (GM note, label)
PIECES = {
    "kick": (36, "Kick"),
    "snare": (38, "Snare"),
    "side_stick": (37, "Side stick"),
    "hh_closed": (42, "Hi-hat closed"),
    "hh_pedal": (44, "Hi-hat pedal"),
    "hh_open": (46, "Hi-hat open"),
    "tom_high": (50, "Tom high"),
    "tom_himid": (48, "Tom hi-mid"),
    "tom_lowmid": (47, "Tom low-mid"),
    "tom_low": (45, "Tom low"),
    "floor_high": (43, "Floor tom high"),
    "floor_low": (41, "Floor tom low"),
    "crash1": (49, "Crash 1"),
    "crash2": (57, "Crash 2"),
    "ride": (51, "Ride"),
    "ride_bell": (53, "Ride bell"),
}

NOTE_OF = {k: v[0] for k, v in PIECES.items()}
LABEL_OF = {k: v[1] for k, v in PIECES.items()}

# the separation stems
STEMS = ("kick", "snare", "toms", "hh", "ride", "crash")

# tom names from highest to lowest for 1..6 toms found
TOM_LAYOUT = {
    1: None,  # chosen by absolute pitch, see articulate.toms
    2: ["tom_himid", "floor_high"],
    3: ["tom_himid", "tom_low", "floor_high"],
    4: ["tom_high", "tom_himid", "tom_low", "floor_high"],
    5: ["tom_high", "tom_himid", "tom_lowmid", "tom_low", "floor_high"],
    6: ["tom_high", "tom_himid", "tom_lowmid", "tom_low", "floor_high", "floor_low"],
}
