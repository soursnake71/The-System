#!/usr/bin/env node
/* Turns ../../spec.js (the desk's single source of truth) into Source/Spec.generated.h for the VST3.
   Run: node plugin/tools/gen-spec.js  (CI runs it too and fails if the committed header is stale.) */
'use strict';
const fs = require('fs'), path = require('path');
const root = path.join(__dirname, '..', '..');
global.globalThis.XL = undefined;
eval(fs.readFileSync(path.join(root, 'spec.js'), 'utf8'));
const XL = globalThis.XL;
const q = s => JSON.stringify(String(s));
const num = x => x === Infinity ? 'INF' : x === -Infinity ? '-INF' : (Number.isInteger(x) ? x.toFixed(1) : String(x));
const LAW = {lin:'Lin', log:'Log', bip:'Bip', u10:'U10', send:'Send', vol:'Vol', fader:'Fader', faderA:'FaderA', ratio:'Ratio'};
const law = L => L ? `{Law::${LAW[L[0]]}, ${num(L[1] || 0)}, ${num(L[2] || 0)}}` : '{Law::None, 0.0, 0.0}';
let o = `// Generated from xldesk/spec.js by plugin/tools/gen-spec.js. Do not edit; edit spec.js and re-run.\n#pragma once\n#include <cmath>\n#include <limits>\nnamespace xlspec {\nconstexpr double INF = std::numeric_limits<double>::infinity();\n`;
o += `constexpr double REF = ${XL.REF};\nconstexpr int MODMAX = ${XL.MODMAX};\nconstexpr int SLOT_CC0 = ${XL.SLOT_CC0};\n`;
o += `struct Law { enum Type { None, Lin, Log, Bip, U10, Send, Vol, Fader, FaderA, Ratio } t; double a, b; };\n`;
o += `constexpr double FAD[][2] = {${XL.FAD.map(([p, d]) => `{${num(p)}, ${num(d)}}`).join(', ')}};\nconstexpr int NFAD = ${XL.FAD.length};\n`;
o += `constexpr double FADA[][2] = {${XL.FADA.map(([p, d]) => `{${num(p)}, ${num(d)}}`).join(', ')}};\nconstexpr int NFADA = ${XL.FADA.length};\n`;
o += `constexpr double SEG_DB[] = {${XL.SEG_DB.map(num).join(', ')}};\n`;
// console + generic module parameters, in a fixed order (the order is the VST3 parameter order: never reorder, only append)
o += `struct ParamDef { const char* id; const char* label; char kind; float def; Law law; const char* unit; };\n`;
const ids = Object.keys(XL.P), D = XL.defaults();
o += `constexpr ParamDef PARAMS[] = {\n` + ids.map(id => { const p = XL.P[id]; return `  {${q(id)}, ${q(p.lb)}, '${p.k === 'pot' ? 'p' : p.k === 'btn' ? 'b' : 'm'}', ${num(D[id])}f, ${law(p.law)}, ${q(p.u || '')}}`; }).join(',\n') + `\n};\nconstexpr int NPARAMS = ${ids.length};\n`;
// modules
o += `struct ModParam { const char* name; const char* label; char kind; float def; Law law; int steps; const double* stepVals; const char* const* stepText; const char* unit; };\n`;
o += `struct ModuleDef { const char* type; const char* name; const char* shortName; int rev; int width; bool stereo; const char* cat; int nParams; const ModParam* params; };\n`;
for (const t in XL.MOD) for (const p of XL.MOD[t].params) if (p.k === 'step'){
  o += `constexpr double SV_${t}_${p.n}[] = {${p.v.map(v => typeof v === 'number' ? num(v) : '-1.0').join(', ')}};\n`;
  o += `constexpr const char* ST_${t}_${p.n}[] = {${p.v.map(v => q(v)).join(', ')}};\n`; }
for (const t in XL.MOD) o += `constexpr ModParam MP_${t}[] = {\n` + XL.MOD[t].params.map(p => `  {${q(p.n)}, ${q(p.lb)}, '${p.k === 'pot' ? 'p' : p.k === 'btn' ? 'b' : 's'}', ${num(p.dn)}f, ${law(p.law)}, ${p.k === 'step' ? p.v.length : 0}, ${p.k === 'step' ? `SV_${t}_${p.n}` : 'nullptr'}, ${p.k === 'step' ? `ST_${t}_${p.n}` : 'nullptr'}, ${q(p.u || '')}}`).join(',\n') + `\n};\n`;
const types = Object.keys(XL.MOD);
o += `constexpr ModuleDef MODULES[] = {\n` + types.map(t => { const M = XL.MOD[t]; return `  {${q(t)}, ${q(M.name)}, ${q(M.short)}, ${M.rev}, ${M.w}, ${M.st ? 'true' : 'false'}, ${q(M.cat)}, ${M.params.length}, MP_${t}}`; }).join(',\n') + `\n};\nconstexpr int NMODULES = ${types.length};\n`;
// locations
const DT = XL.defTypes();
o += `struct WhereDef { const char* id; int nTypes; const char* types[8]; const char* defType; };\n`;
o += `constexpr WhereDef WHERE[] = {\n` + Object.keys(XL.WHERE).map(w => `  {${q(w)}, ${XL.WHERE[w].length}, {${XL.WHERE[w].map(q).join(', ')}}, ${q(DT[w])}}`).join(',\n') + `\n};\nconstexpr int NWHERE = ${Object.keys(XL.WHERE).length};\n`;
o += `struct TypeName { const char* type; const char* name; };\nconstexpr TypeName TYPE_NAMES[] = {${Object.entries(XL.TYPE_NAME).map(([k, v]) => `{${q(k)}, ${q(v)}}`).join(', ')}};\nconstexpr int NTYPE_NAMES = ${Object.keys(XL.TYPE_NAME).length};\n`;
// MIDI map
const mm = Object.entries(XL.MIDIMAP);
o += `struct MidiMapEntry { int ch, cc; const char* target; };\nconstexpr MidiMapEntry MIDIMAP[] = {\n` + mm.map(([k, id]) => { const [ch, cc] = k.split(':'); return `  {${ch}, ${cc}, ${q(id)}}`; }).join(',\n') + `\n};\nconstexpr int NMIDIMAP = ${mm.length};\n`;
// laws, same maths as XL.law
o += `
inline double tableLaw (const double T[][2], int n, double p)
{
    if (p <= .0005) return -INF;
    for (int i = 1; i < n; ++i)
        if (p <= T[i][0])
        {
            if (std::isinf (T[i - 1][1])) return T[i][1] + 40.0 * std::log10 (p / T[i][0]);
            return T[i - 1][1] + (T[i][1] - T[i - 1][1]) * (p - T[i - 1][0]) / (T[i][0] - T[i - 1][0]);
        }
    return T[n - 1][1];
}
inline double law (const Law& L, double p)
{
    switch (L.t)
    {
        case Law::Lin:    return L.a + (L.b - L.a) * p;
        case Law::Log:    return L.a * std::pow (L.b / L.a, p);
        case Law::Bip:    return (p - .5) * 2.0 * L.a;
        case Law::U10:    return p <= .002 ? -INF : p >= .5 ? (p - .5) * 20.0 : 60.0 * std::log10 (p / .5);
        case Law::Send:   return p <= .002 ? -INF : p >= .75 ? (p - .75) * 24.0 : 60.0 * std::log10 (p / .75);
        case Law::Vol:    return p <= .002 ? -INF : 40.0 * std::log10 (p);
        case Law::Fader:  return tableLaw (FAD, NFAD, p);
        case Law::FaderA: return tableLaw (FADA, NFADA, p);
        case Law::Ratio:  return p >= .999 ? INF : 1.0 / (1.0 - p);
        case Law::None:
        default:          return p;
    }
}
} // namespace xlspec
`;
const out = path.join(__dirname, '..', 'Source', 'Spec.generated.h');
if (process.argv.includes('--check')){ const cur = fs.existsSync(out) ? fs.readFileSync(out, 'utf8').replace(/\r\n/g, '\n') : ''; if (cur !== o){ /* Windows checkouts use CRLF */ console.error('Spec.generated.h is out of date: run node xldesk/plugin/tools/gen-spec.js'); process.exit(1); } console.log('Spec.generated.h is up to date'); }
else { fs.writeFileSync(out, o); console.log('wrote', out, ids.length, 'parameters,', types.length, 'modules'); }
