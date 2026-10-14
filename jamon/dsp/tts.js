/* Text to speech (copied from The Omega System, ../index.html). */
/* ================= speech (text to speech for the vocoder) =================
   English letter-to-sound rules in the style of the 1976 NRL report (Elovitz et al.), then a cascade/parallel formant
   synthesizer in the Klatt manner: glottal pulse source, five formant resonators, filtered noise for fricatives and bursts.
   Type /phonemes/ between slashes (e.g. /F R EH SH/) to spell a word exactly. */
const TTS_RULES = (() => {
  const R = {
    A:['[A] =AX',' [ARE] =AA R',' [AR]O=AX R','[AR]#=EH R',' ^[AS]#=EY S','[A]WA=AX','[AW]=AO',' :[ANY]=EH N IY','[A]^+#=EY','#:[ALLY]=AX L IY',' [AL]#=AX L','[AGAIN]=AX G EH N','#:[AG]E=IH JH','[A]^+:#=AE',' :[A]^+ =EY','[A]^%=EY',' [ARR]=AX R','[ARR]=AE R',' :[AR] =AA R','[AR] =ER','[AR]=AA R','[AIR]=EH R','[AI]=EY','[AY]=EY','[AU]=AO','#:[AL] =AX L','#:[ALS] =AX L Z','[ALK]=AO K','[AL]^=AO L',' :[ABLE]=EY B AX L','[ABLE]=AX B AX L','[ANG]+=EY N JH','[A]=AE'],
    B:[' [BE]^#=B IH','[BEING]=B IY IH NG',' [BOTH] =B OW TH',' [BUS]#=B IH Z','[BUIL]=B IH L','[B]=B'],
    C:[' [CH]^=K','^E[CH]=K','[CH]=CH',' S[CI]#=S AY','[CI]A=SH','[CI]O=SH','[CI]EN=SH','[C]+=S','[CK]=K','[COM]%=K AH M','[C]=K'],
    D:['#:[DED] =D IH D','.E[D] =D','#^:E[D] =T',' [DE]^#=D IH',' [DO] =D UW',' [DOES]=D AH Z',' [DOING]=D UW IH NG',' [DOW]=D AW','[DU]A=JH UW','[D]=D'],
    E:['#:[E] =',"'^:[E] =",' :[E] =IY','#[ED] =D','#:[E]D =','[EV]ER=EH V','[E]^%=IY','[ERI]#=IY R IY','[ERI]=EH R IH','#:[ER]#=ER','[ER]#=EH R','[ER]=ER',' [EVEN]=IY V EH N','#:[E]W=','@[EW]=UW','[EW]=Y UW','[E]O=IY','#:&[ES] =IH Z','#:[E]S =','#:[ELY] =L IY','#:[EMENT]=M EH N T','[EFUL]=F UH L','[EE]=IY','[EARN]=ER N',' [EAR]^=ER','[EAD]=EH D','#:[EA] =IY AX','[EA]SU=EH','[EA]=IY','[EIGH]=EY','[EI]=IY',' [EYE]=AY','[EY]=IY','[EU]=Y UW','[E]=EH'],
    F:['[FUL]=F UH L','[F]=F'],
    G:['[GIV]=G IH V',' [G]I^=G','[GE]T=G EH','SU[GGES]=G JH EH S','[GG]=G',' B#[G]=G','[G]+=JH','[GREAT]=G R EY T','#[GH]=','[G]=G'],
    H:[' [HAV]=HH AE V',' [HERE]=HH IY R',' [HOUR]=AW ER','[HOW]=HH AW','[H]#=HH','[H]='],
    I:[' [IN]=IH N',' [I] =AY','[IN]D=AY N','[IER]=IY ER','#:R[IED] =IY D','[IED] =AY D','[IEN]=IY EH N','[IE]T=AY EH',' :[I]%=AY','[I]%=IY','[IE]=IY','[I]^+:#=IH','[IR]#=AY R','[IZ]%=AY Z','[IS]%=AY Z','[I]D%=AY','+^[I]^+=IH','[I]T%=AY','#^:[I]^+=IH','[I]^+=AY','[IR]=ER','[IGH]=AY','[ILD]=AY L D','[IGN] =AY N','[IGN]^=AY N','[IGN]%=AY N','[IQUE]=IY K','[I]=IH'],
    J:['[J]=JH'], K:[' [K]N=','[K]=K'],
    L:['[LO]C#=L OW','L[L]=','#^:[L]%=AX L','[LEAD]=L IY D','[L]=L'], M:['[MOV]=M UW V','[M]=M'],
    N:['E[NG]+=N JH','[NG]R=NG G','[NG]#=NG G','[NGL]%=NG G AX L','[NG]=NG','[NK]=NG K',' [NOW] =N AW','[N]=N'],
    O:['[OF] =AX V','[OROUGH]=ER OW','#:[OR] =ER','#:[ORS] =ER Z','[OR]=AO R',' [ONE]=W AH N','[OW]=OW',' [OVER]=OW V ER','[OV]=AH V','[O]^%=OW','[O]^EN=OW','[O]^I#=OW','[OL]D=OW L','[OUGHT]=AO T','[OUGH]=AH F',' [OU]=AW','H[OU]S#=AW','[OUS]=AX S','[OUR]=AO R','[OULD]=UH D','^[OU]^L=AH','[OUP]=UW P','[OU]=AW','[OY]=OY','[OING]=OW IH NG','[OI]=OY','[OOR]=AO R','[OOK]=UH K','[OOD]=UH D','[OO]=UW','[O]E=OW','[O] =OW','[OA]=OW',' [ONLY]=OW N L IY',' [ONCE]=W AH N S',"[ON'T]=OW N T",'C[O]N=AA','[O]NG=AO',' :^[O]N=AH','I[ON]=AX N','#:[ON] =AX N','#^[ON]=AX N','[O]ST =OW','[OF]^=AO F','[OTHER]=AH DH ER','[OSS] =AO S','#^:[OM]=AH M','[O]=AA'],
    P:['[PH]=F','[PEOP]=P IY P','[POW]=P AW','[PUT] =P UH T','[P]=P'], Q:['[QUAR]=K W AO R','[QU]=K W','[Q]=K'], R:[' [RE]^#=R IY','[R]=R'],
    S:['[SH]=SH','#[SION]=ZH AX N','[SOME]=S AH M','#[SUR]#=ZH ER','[SUR]#=SH ER','#[SU]#=ZH UW','#[SSU]#=SH UW','#[SED] =Z D','#[S]#=Z','[SAID]=S EH D','^[SION]=SH AX N','[S]S=','.[S] =Z','#:.E[S] =Z','#^:##[S] =Z','#^:#[S] =S','U[S] =S',' :#[S] =Z',' [SCH]=S K','[S]C+=','#[SM]=Z M',"#[SN]'=Z AX N",'[S]=S'],
    T:[' [THE] =DH AX','[TO] =T UW','[THAT] =DH AE T',' [THIS] =DH IH S',' [THEY]=DH EY',' [THERE]=DH EH R','[THER]=DH ER','[THEIR]=DH EH R',' [THAN] =DH AE N',' [THEM] =DH EH M','[THESE] =DH IY Z',' [THEN]=DH EH N','[THROUGH]=TH R UW','[THOSE]=DH OW Z','[THOUGH] =DH OW',' [THUS]=DH AH S','[TH]=TH','#:[TED] =T IH D','S[TI]#N=CH','[TI]O=SH','[TI]A=SH','[TIEN]=SH AX N','[TUR]#=CH ER','[TU]A=CH UW',' [TWO]=T UW','[T]=T'],
    U:[' [UN]I=Y UW N',' [UN]=AH N',' [UPON]=AX P AO N','@[UR]#=UH R','[UR]#=Y UH R','[UR]^=ER','[U]^ =AH','[U]^^=AH','[UY]=AY',' G[U]#=','G[U]%=','G[U]#=W','#N[U]=Y UW','@[U]=UW','[U]=Y UW'],
    V:['[VIEW]=V Y UW','[V]=V'],
    W:[' [WERE]=W ER','[WA]S=W AA','[WA]T=W AA','[WHERE]=W EH R','[WHAT]=W AA T','[WHOL]=HH OW L','[WHO]=HH UW','[WH]=W','[WAR]=W AO R','[WOR]^=W ER','[WR]=R','[W]=W'],
    X:['[X]=K S'],
    Y:['[YOUNG]=Y AH NG',' [YOU]=Y UW',' [YES]=Y EH S',' [Y]=Y','#^:[Y] =IY','#^:[Y]I=IY',' :[Y] =AY',' :[Y]#=AY',' :[Y]^+:#=IH',' :[Y]^#=AY','[Y]=IH'],
    Z:['[Z]=Z']
  };
  const out = {};
  for (const k in R) out[k] = R[k].map(r => { const m = r.match(/^(.*)\[(.*)\](.*)=(.*)$/); return {l:m[1], m:m[2], r:m[3], p:m[4].trim() ? m[4].trim().split(/\s+/) : []}; });
  return out;
})();
const TTS_DICT = {AH:'AA', AHH:'AA AA', AHHH:'AA AA AA', AHHHH:'AA AA AA AA', OH:'OW', OOH:'UW UW', UH:'AH', YEAH:'Y EH AX', OK:'OW K EY', OKAY:'OW K EY', A:'AX', I:'AY', OF:'AX V', THE:'DH AX', TO:'T UW',
  ARE:'AA R', YOU:'Y UW', YOUR:'Y AO R', WE:'W IY', BE:'B IY', ME:'M IY', HE:'HH IY', SHE:'SH IY', MY:'M AY', BY:'B AY', DO:'D UW', WHO:'HH UW', FRESH:'F R EH SH', ROBOT:'R OW B AA T', ELECTRO:'IH L EH K T R OW',
  MUSIC:'M Y UW Z IH K', BEAT:'B IY T', BEATS:'B IY T S', BASS:'B EY S', HIP:'HH IH P', HOP:'HH AA P', JAM:'JH AE M', FUNK:'F AH NG K', FUNKY:'F AH NG K IY', DANCE:'D AE N S', PARTY:'P AA R T IY', COMPUTER:'K AX M P Y UW T ER', SNAKE:'S N EY K', DEVICE:'D IH V AY S',
  REALLY:'R IY L IY', BREAK:'B R EY K', MIRACLE:'M IH R AX K AX L', ELECTRIC:'IH L EH K T R IH K', EGYPTIAN:'IY JH IH P SH AX N', NINETY:'N AY N T IY', PEOPLE:'P IY P AX L', BOOGIE:'B UH G IY', PLANET:'P L AE N AX T', ROCK:'R AA K', ROCKIT:'R AA K IH T', SCRATCH:'S K R AE CH', TURNTABLE:'T ER N T EY B AX L', MIC:'M AY K', WORLD:'W ER L D', LOVE:'L AH V', ONE:'W AH N', DONT:'D OW N T', "DON'T":'D OW N T', WHAT:'W AH T', BABY:'B EY B IY'};
const TTS_NUM = ['zero', 'one', 'two', 'three', 'four', 'five', 'six', 'seven', 'eight', 'nine', 'ten', 'eleven', 'twelve', 'thirteen', 'fourteen', 'fifteen', 'sixteen', 'seventeen', 'eighteen', 'nineteen'];
const TTS_TENS = ['', '', 'twenty', 'thirty', 'forty', 'fifty', 'sixty', 'seventy', 'eighty', 'ninety'];
function ttsNumber(n){
  if (n < 20) return TTS_NUM[n];
  if (n < 100) return TTS_TENS[Math.floor(n / 10)] + (n % 10 ? ' ' + TTS_NUM[n % 10] : '');
  if (n < 1000) return TTS_NUM[Math.floor(n / 100)] + ' hundred' + (n % 100 ? ' ' + ttsNumber(n % 100) : '');
  if (n < 1e6) return ttsNumber(Math.floor(n / 1000)) + ' thousand' + (n % 1000 ? ' ' + ttsNumber(n % 1000) : '');
  return String(n).split('').map(d => TTS_NUM[+d]).join(' ');
}
const TTS_VOW = 'AEIOUY', TTS_VOICED = 'BDVGJLMNRWZ';
function ttsWord(word){
  if (TTS_DICT[word]) return TTS_DICT[word].split(' ');
  const w = ' ' + word + ' ', out = [];
  const isV = c => TTS_VOW.includes(c), isC = c => c >= 'A' && c <= 'Z' && !isV(c);
  // context matchers; i is the index to test, dir = +1 (right) or -1 (left); return new index or -1
  const ctx = (pat, i, dir) => {
    const seq = dir > 0 ? pat.split('') : pat.split('').reverse();
    for (const p of seq){
      const c = w[i] || ' ';
      if (p === '#'){ if (!isV(c)) return null; while (isV(w[i] || ' ')) i += dir; }
      else if (p === ':'){ while (isC(w[i] || ' ')) i += dir; }
      else if (p === '^'){ if (!isC(c)) return null; i += dir; }
      else if (p === '.'){ if (!TTS_VOICED.includes(c)) return null; i += dir; }
      else if (p === '+'){ if (!'EIY'.includes(c)) return null; i += dir; }
      else if (p === '@'){ if (!'TSRDLZNJ'.includes(c) && !(dir > 0 ? ['TH', 'CH', 'SH'].includes(w.substr(i, 2)) : false)) return null; i += dir; }
      else if (p === '&'){ if (!'SCGZXJ'.includes(c)) return null; i += dir; }
      else if (p === '%'){ if (dir < 0) return null; const sfx = ['ING', 'ELY', 'ER', 'ES', 'ED', 'E'].find(s => w.substr(i, s.length) === s); if (!sfx) return null; i += sfx.length; }
      else if (p === ' '){ if (c >= 'A' && c <= 'Z') return null; i += dir; }
      else { if (c !== p) return null; i += dir; }
    }
    return i;
  };
  let i = 1;
  while (i < w.length - 1){
    const ch = w[i], rules = TTS_RULES[ch];
    if (!rules){ i++; continue; }
    let done = false;
    for (const r of rules){
      if (w.substr(i, r.m.length) !== r.m) continue;
      if (r.l && ctx(r.l, i - 1, -1) === null) continue;
      if (r.r && ctx(r.r, i + r.m.length, 1) === null) continue;
      out.push(...r.p); i += r.m.length; done = true; break;
    }
    if (!done) i++;
  }
  return out;
}
// text -> list of {ph, word} with pauses ('_' short, '__' long) and sentence ends ('.', '?')
function ttsParse(text){
  const res = [];
  const toks = text.replace(/(\d+)/g, (m, d) => ' ' + ttsNumber(+d) + ' ').match(/\/[^/]*\/|[A-Za-z']+|[.!?]+|[,;:]|-|\s+/g) || [];
  let wi = 0;
  for (const t of toks){
    if (/^\s+$/.test(t)) continue;
    if (t[0] === '/'){ res.push(...t.slice(1, -1).trim().toUpperCase().split(/\s+/).filter(Boolean).map(ph => ({ph, w:wi}))); wi++; continue; }
    if (/^[.!?]+$/.test(t)){ res.push({ph:t[0] === '?' ? '?' : '.', w:wi}); continue; }
    if (/^[,;:-]$/.test(t)){ res.push({ph:'_', w:wi}); continue; }
    const W = t.toUpperCase().replace(/[^A-Z']/g, '');
    let ph = ttsWord(W);
    // a vowel letter repeated in the spelling ("freeeesh", "ahhhh") stretches the word's main vowel
    const stretch = Math.max(0, (W.match(/([AEIOUH])\1+/g) || []).reduce((n, m) => n + m.length - 1, 0));
    ph = ph.map(p => ({ph:p, w:wi, x:stretch}));
    res.push(...ph); wi++;
  }
  // collapse doubled consonants ("stuff" -> S T AH F)
  return res.filter((p, k) => !(k && p.ph === res[k - 1].ph && !TTS_PH[p.ph]?.v));
}
// phoneme table: formants F1..F3 (Hz), duration (ms), voicing, frication (center, bw, amp), aspiration, nasal, stop type
const TTS_PH = (() => {
  const V = (f1, f2, f3, d, f1b, f2b, f3b) => ({v:1, f:[f1, f2, f3], d, f2:f1b ? [f1b, f2b, f3b] : null});
  return {
    IY:V(270, 2290, 3010, 130), IH:V(390, 1990, 2550, 90), EH:V(530, 1840, 2480, 110), AE:V(660, 1720, 2410, 150), AA:V(730, 1090, 2440, 150), AO:V(570, 840, 2410, 150),
    UH:V(440, 1020, 2240, 90), UW:V(300, 870, 2240, 130), AH:V(640, 1190, 2390, 100), ER:V(490, 1350, 1690, 140), AX:V(500, 1500, 2500, 60),
    EY:V(480, 1900, 2500, 170, 300, 2200, 2900), AY:V(700, 1200, 2500, 200, 320, 2100, 2800), OW:V(560, 900, 2400, 170, 350, 750, 2300), AW:V(720, 1200, 2450, 200, 380, 900, 2300), OY:V(560, 850, 2400, 200, 330, 2100, 2800),
    L:{v:1, son:1, f:[360, 1100, 2700], d:70}, R:{v:1, son:1, f:[420, 1250, 1600], d:70}, W:{v:1, son:1, f:[300, 650, 2200], d:60}, Y:{v:1, son:1, f:[260, 2070, 3020], d:60},
    M:{v:1, nas:1, f:[270, 1000, 2200], d:75}, N:{v:1, nas:1, f:[270, 1600, 2600], d:70}, NG:{v:1, nas:1, f:[270, 2000, 2600], d:80},
    S:{fr:[6200, 2000, 1], f:[320, 1700, 2600], d:110}, Z:{v:.5, fr:[6000, 2000, .6], f:[240, 1700, 2600], d:90}, SH:{fr:[3200, 1800, 1], f:[300, 1840, 2750], d:120}, ZH:{v:.5, fr:[3000, 1800, .6], f:[300, 1840, 2750], d:90},
    F:{fr:[5000, 5000, .35], f:[340, 1100, 2400], d:100}, V:{v:.6, fr:[5000, 5000, .25], f:[220, 1100, 2400], d:70}, TH:{fr:[5500, 5000, .3], f:[320, 1400, 2700], d:100}, DH:{v:.6, fr:[5500, 5000, .2], f:[270, 1400, 2700], d:60},
    HH:{asp:1, d:70},
    P:{stop:'p', f:[300, 900, 2400], burst:[1000, 3000], d:90}, B:{stop:'b', f:[200, 900, 2400], burst:[1000, 3000], d:70},
    T:{stop:'p', f:[300, 1700, 2600], burst:[4500, 3000], d:90}, D:{stop:'b', f:[200, 1700, 2600], burst:[4000, 3000], d:70},
    K:{stop:'p', f:[300, 2000, 2700], burst:[2200, 1500], d:90}, G:{stop:'b', f:[200, 2000, 2700], burst:[2000, 1500], d:70},
    CH:{stop:'p', aff:'SH', f:[300, 1700, 2600], burst:[3500, 2500], d:70}, JH:{stop:'b', aff:'ZH', f:[200, 1700, 2600], burst:[3200, 2500], d:60}
  };
})();
// opts: {pitch (Hz), speed (1 = normal), robot (monotone), sr}
function ttsRender(text, opts = {}){
  const sr = opts.sr || 22050, f0 = opts.pitch || 110, sp = 1 / (opts.speed || 1), robot = !!opts.robot;
  const seq = ttsParse(text);
  // build frames: one per 5 ms, holding targets; formants glide toward each new target
  const fr = [];
  const push = (n, p) => { for (let k = 0; k < n; k++) fr.push(Object.assign({}, p, k ? {acc:false} : null)); };   // the word-stress lift starts once
  const ms = d => Math.max(1, Math.round(d * sp / 5));
  let lastF = [500, 1500, 2500], wordStart = -1;
  const nextVowel = k => { for (let j = k + 1; j < seq.length; j++){ const q = TTS_PH[seq[j].ph]; if (q && q.v === 1 && !q.son && !q.nas) return q; if (!q) break; } return TTS_PH.AX; };
  const ends = [];
  seq.forEach((s, k) => {
    if (s.ph === '_'){ push(ms(140), {av:0, f:lastF}); return; }
    if (s.ph === '.' || s.ph === '?'){ ends.push({at:fr.length, q:s.ph === '?'}); push(ms(260), {av:0, f:lastF}); return; }
    const P = TTS_PH[s.ph]; if (!P) return;
    const acc = s.w !== wordStart; wordStart = s.w;
    if (P.v === 1 && !P.son && !P.nas){
      const n = ms(P.d * (1 + (s.x || 0) * .6));
      if (P.f2){ const h = Math.round(n * .45); push(h, {av:1, f:P.f, acc}); push(n - h, {av:1, f:P.f2}); lastF = P.f2; }
      else { push(n, {av:1, f:P.f, acc}); lastF = P.f; }
    } else if (P.son || P.nas){ push(ms(P.d), {av:P.nas ? .55 : .8, f:P.f, nas:P.nas}); lastF = P.f; }
    else if (P.fr){ push(ms(P.d), {av:(P.v || 0) * .6, f:P.f, af:P.fr[2], fc:P.fr[0], fb:P.fr[1]}); lastF = P.f; }
    else if (P.asp){ const nv = nextVowel(k); push(ms(P.d), {av:0, ah:.5, f:nv.f}); lastF = nv.f; }
    else if (P.stop){
      const vo = P.stop === 'b', nv = nextVowel(k);
      push(ms(P.d * .6), {av:vo ? .12 : 0, f:[200, P.f[1], P.f[2]], close:1});
      push(ms(12), {av:vo ? .3 : 0, f:P.f, af:.9, fc:P.burst[0], fb:P.burst[1]});
      if (P.aff){ const A = TTS_PH[P.aff]; push(ms(60), {av:(A.v || 0) * .5, f:A.f, af:A.fr[2], fc:A.fr[0], fb:A.fr[1]}); }
      else if (!vo) push(ms(35), {av:0, ah:.4, f:nv.f});
      lastF = P.f;
    }
  });
  if (!fr.length) return new Float32Array(1);
  push(ms(80), {av:0, f:lastF});
  // pitch contour: declination over each sentence, a lift on the first vowel of each word, fall (or rise) at the end
  let s0 = 0; const endsAll = ends.length ? ends : [{at:fr.length, q:false}];
  for (const e of endsAll){
    const len = Math.max(1, e.at - s0);
    for (let k = s0; k < e.at && k < fr.length; k++){
      const x = (k - s0) / len; let p = robot ? 1 : 1.08 - .2 * x;
      if (!robot && x > .82) p *= e.q ? 1 + (x - .82) * 1.6 : 1 - (x - .82) * .6;
      fr[k].f0 = f0 * p;
    }
    s0 = e.at;
  }
  for (let k = 0; k < fr.length; k++){ if (fr[k].f0 == null) fr[k].f0 = f0; if (!robot && fr[k].acc){ for (let j = k; j < Math.min(fr.length, k + 14); j++) fr[j].f0b = (fr[j].f0b || 1) * (1 + .1 * Math.sin(Math.PI * (j - k) / 14)); } }
  // synthesis
  const hop = Math.round(sr * .005), out = new Float32Array(fr.length * hop);
  const res = () => ({y1:0, y2:0});
  const R = [res(), res(), res(), res(), res()], RF = [res(), res()], RN = res();
  const BW = [70, 90, 150, 250, 300];
  const coef = (f, bw) => { const r = Math.exp(-Math.PI * bw / sr), C = -r * r, B = 2 * r * Math.cos(2 * Math.PI * f / sr), A = 1 - B - C; return [A, B, C]; };
  const run = (st, c, x) => { const y = c[0] * x + c[1] * st.y1 + c[2] * st.y2; st.y2 = st.y1; st.y1 = y; return y; };
  let cur = Object.assign({}, fr[0], {f:fr[0].f.slice()}), ph = 0, prevU = 0, nz = 0;
  const glide = 1 - Math.exp(-1 / (sr * .018)), amp = 1 - Math.exp(-1 / (sr * .006));
  let av = 0, af = 0, ah = 0, fc = 4000, fb = 2000, n = 0, lastFc = 0, fcCoef = null, fcNorm = 1;
  for (let k = 0; k < fr.length; k++){
    const T = fr[k], tf = T.f, tav = T.av || 0, taf = T.af || 0, tah = T.ah || 0;
    if (T.fc){ fc = T.fc; fb = T.fb; }
    const pitch = T.f0 * (T.f0b || 1);
    for (let s = 0; s < hop; s++, n++){
      for (let j = 0; j < 3; j++) cur.f[j] += (tf[j] - cur.f[j]) * glide;
      av += (tav - av) * amp; af += (taf - af) * amp * 2; ah += (tah - ah) * amp;
      // glottal flow (Rosenberg), its derivative is the source
      ph += pitch * (1 + (robot ? 0 : (Math.random() - .5) * .01)) / sr; if (ph >= 1) ph -= 1;
      const u = ph < .4 ? .5 * (1 - Math.cos(Math.PI * ph / .4)) : ph < .56 ? Math.cos(Math.PI * (ph - .4) / .32) : 0;
      const g = (u - prevU) * 4; prevU = u;
      const white = Math.random() * 2 - 1; nz = white;
      let x = g * av + white * ah * .25;
      if (T.nas) x = run(RN, coef(270, 120), x);
      for (let j = 0; j < 5; j++){ const F = j < 3 ? cur.f[j] : j === 3 ? 3500 : 4500; x = run(R[j], coef(F, BW[j] * (T.nas && j === 0 ? 2 : 1)), x); }
      // frication: noise through two resonators at the fricative's centre
      if (fc !== lastFc){ lastFc = fc; fcCoef = coef(fc, fb); const w = 2 * Math.PI * fc / sr, re = 1 - fcCoef[1] * Math.cos(w) - fcCoef[2] * Math.cos(2 * w), im = fcCoef[1] * Math.sin(w) + fcCoef[2] * Math.sin(2 * w); fcNorm = Math.hypot(re, im) / fcCoef[0]; }
      const fz = run(RF[1], fcCoef, run(RF[0], fcCoef, white)) * fcNorm * fcNorm * af * .12;
      out[n] = x * .5 + fz;
    }
  }
  // tidy: remove DC, normalize
  let m = 0, dc = 0, xp = 0;
  for (let i = 0; i < out.length; i++){ const y = out[i] - xp + .995 * dc; xp = out[i]; dc = y; out[i] = y; m = Math.max(m, Math.abs(y)); }
  if (m) for (let i = 0; i < out.length; i++) out[i] *= .9 / m;
  return out;
}

const TTS_VOICES = {robot:{pitch:98, robot:true}, deep:{pitch:76}, normal:{pitch:112}, high:{pitch:190}};
