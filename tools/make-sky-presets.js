// Writes sky/presets.json (the 50 factory presets + drum patterns) from sky/presets.js. Run: node tools/make-sky-presets.js
const fs = require('fs'), path = require('path');
const P = require('../sky/presets.js');
const NAMES = ['Holding Stack', 'Final Approach', 'Heathrow Dawn', 'Contrails', 'Jet Stream', 'Night Mail', 'Red Eye', 'Cruise Altitude', 'Wake Turbulence', 'Squawk 7700',
  'Runway Lights', 'Glide Slope', 'Tower Frequency', 'Clear Skies', 'Cumulus', 'Crosswind', 'Go Around', 'Flight Level 350', 'Transatlantic', 'Polar Route',
  'Midnight Departure', 'Heli Orbit', 'Air Traffic', 'Radar Sweep', 'Vapour Trail', 'Thermals', 'Mach One', 'Ground Effect', 'Jetlag', 'Long Haul',
  'Afterburner', 'Stratosphere', 'Landing Gear', 'Blue Hour', 'Sky Lane', 'Waypoint', 'Beacon', 'Tailwind', 'Altimeter', 'Cabin Pressure',
  'Gatwick Shuffle', 'Approach Control', 'Overhead Break', 'Sonic Boom', 'Morning Push', 'Arrivals Board', 'Terminal Five', 'Ascent', 'Descent', 'Touchdown',
  'Night Approach', 'Holding Pattern', 'Clearance', 'Mayday', 'Pan-Pan', 'Vector', 'Final Call', 'Jet Bridge', 'Taxiway', 'Thrust Reverse',
  'Ceiling Zero', 'Tropopause', 'Dead Reckoning', 'Great Circle', 'Wingtip Vortex', 'Slipstream', 'Contrail City', 'Redline', 'Black Box', 'Air Corridor',
  'Sundown Arrivals', 'Dawn Patrol', 'Downwind Leg', 'Base Turn', 'Short Final', 'Flare', 'Rollout', 'Pushback', 'Departure Board', 'Gate Change',
  'Jet Lag Disco', 'Cloud Nine', 'Turbulence', 'Air Pocket', 'Mach Loop', 'Squawk Ident', 'Radar Contact', 'Glidepath', 'Tower Lights', 'Last Flight Out',
  'Northern Lights Route', 'Equator Crossing', 'Seaplane', 'Night Freight', 'Airshow', 'Barrel Roll', 'Stall Warning', 'Autopilot', 'Fuel Dump', 'Wheels Up'];
const presets = NAMES.map((name, i) => P.generatePreset(P.rng(1000 + i * 7919), name));
presets.unshift(Object.assign(P.defaultPreset(), {name:'Init'}));
const out = {version:3, patterns:P.PATTERNS, rhythms:P.RHYTHMS, library:P.LIBRARY.map((x, i) => ({name:x[0], cat:x[1], patch:P.libraryPatch(i)})), drums:P.DRUMS, drumNotes:P.DRUM_NOTES, presets};
fs.writeFileSync(path.join(__dirname, '../sky/presets.json'), JSON.stringify(out));
console.log('wrote', presets.length, 'presets,', Object.keys(P.PATTERNS).length, 'patterns');
