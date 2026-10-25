/* The F.A.R.P. plugin bridge. The page's sound engine runs in C++ inside the plugin; this script stands in for
   the browser audio pieces the page expects (AudioContext, the engine node, local storage, the ROM folder),
   and carries the page's messages to the plugin and the engine's messages back. */
(function(){
  'use strict';
  window.FARP_PLUGIN=true;
  const ST=window.__FARP_STATE||{ls:{},sr:48000,romDir:''};
  const backend=()=>window.__JUCE__&&window.__JUCE__.backend;
  const emit=(id,obj)=>{const b=backend();if(b)b.emitEvent(id,obj)};

  // ---- typed arrays travel as base64 ----
  const b64=u=>{let s='';for(let i=0;i<u.length;i+=0x8000)s+=String.fromCharCode.apply(null,u.subarray(i,i+0x8000));return btoa(s)};
  const unb64=s=>{const t=atob(s),u=new Uint8Array(t.length);for(let i=0;i<t.length;i++)u[i]=t.charCodeAt(i);return u};
  const enc=v=>{
    if(v instanceof Float32Array)return {$f:b64(new Uint8Array(v.buffer,v.byteOffset,v.byteLength))};
    if(v instanceof Float64Array)return {$f:b64(new Uint8Array(Float32Array.from(v).buffer))};
    if(v instanceof Uint8Array||v instanceof Int8Array)return {$u:b64(new Uint8Array(v.buffer,v.byteOffset,v.byteLength))};
    if(Array.isArray(v))return v.map(enc);
    if(v&&typeof v==='object'){const o={};for(const k in v){const x=v[k];if(typeof x!=='function'&&x!==undefined)o[k]=enc(x)}return o}
    return v};
  const dec=v=>{
    if(Array.isArray(v))return v.map(dec);
    if(v&&typeof v==='object'){if(typeof v.$u==='string')return unb64(v.$u);if(typeof v.$f==='string'){const u=unb64(v.$f);return new Float32Array(u.buffer)}
      const o={};for(const k in v)o[k]=dec(v[k]);return o}
    return v};
  window.__farpSend=m=>{try{emit('farp',JSON.stringify(enc(m)))}catch(e){console.warn('farp send',e)}};

  // ---- settings live in the plugin's saved state, not the web view ----
  window.__PLUGIN_LS={
    get(k,d){const v=ST.ls[k];if(v===undefined)return d;try{return JSON.parse(v)}catch(e){return d}},
    set(k,v){let s;try{s=JSON.stringify(v)}catch(e){return}if(s===undefined)return;if(ST.ls[k]===s)return;ST.ls[k]=s;emit('farpLs',{k,v:s})}};

  // ---- native functions (JUCE's promise protocol) ----
  let nextId=1<<20;const waiting=new Map();
  const call=(name,...params)=>new Promise(res=>{const id=nextId++;waiting.set(id,res);emit('__juce__invoke',{name,params,resultId:id})});
  const hook=()=>{const b=backend();if(!b){setTimeout(hook,20);return}
    b.addEventListener('__juce__complete',({promiseId,result})=>{const r=waiting.get(promiseId);if(r){waiting.delete(promiseId);r(result)}})};
  hook();
  // the ROM folder works the way it does in the Android app
  window.__ROMBASE='/roms/';
  window.FarpAndroid={romList:()=>call('romList'),romFolder:()=>call('romFolder'),pickRomFolder:()=>{call('pickRomFolder')}};

  // the host sends MIDI: the page doesn't open MIDI devices itself
  try{Object.defineProperty(navigator,'requestMIDIAccess',{value:undefined,configurable:true})}catch(e){}

  // ---- the engine's clock: the plugin reports its time with every update ----
  let clockT=0,clockAt=performance.now();
  const now=()=>clockT+(performance.now()-clockAt)/1000;

  // ---- stand-ins for the browser audio graph ----
  const offline=(()=>{try{return new OfflineAudioContext(1,1,44100)}catch(e){return null}})();
  const param=v=>({value:v,setValueAtTime(x){this.value=x;return this},linearRampToValueAtTime(x){this.value=x;return this},exponentialRampToValueAtTime(x){this.value=x;return this},
    setTargetAtTime(x){this.value=x;return this},cancelScheduledValues(){return this},cancelAndHoldAtTime(){return this}});
  class FakeNode{
    constructor(){this.gain=param(1);this.pan=param(0);this.delayTime=param(0);this.frequency=param(440);this.Q=param(1);this.detune=param(0);this.type='';this.__out=[];this.channelCount=2}
    connect(t,o,i){this.__out.push(t);if(t===window.__fakeNode&&i===2)this.__voice=true;if(this.__mic&&t&&t.__voice)window.__farpSend({t:'vin',on:1});return t}
    disconnect(){if(this.__mic)window.__farpSend({t:'vin',on:0});this.__out=[]}
  }
  // spoken phrases: each buffer goes to the plugin once, then plays by id
  const ids=new WeakMap();let nextBuf=1;
  const phraseId=buf=>{if(!buf)return 0;let id=ids.get(buf);if(id)return id;id=nextBuf++;ids.set(buf,id);
    window.__farpSend({t:'vphr',id,fs:buf.sampleRate,data:buf.getChannelData(0)});return id};
  window.__phraseId=phraseId;
  class FakeSource extends FakeNode{
    constructor(){super();this.buffer=null;this.loop=false;this.onended=null;this.playbackRate=param(1);this.__t=0}
    start(t){const voice=this.__out.some(n=>n&&n.__voice);if(!voice||!this.buffer)return;const at=Math.max(t||0,now());
      window.__farpSend({t:'vsay',id:phraseId(this.buffer),at,loop:this.loop});
      if(!this.loop){const ms=(at-now()+this.buffer.duration)*1000;this.__t=setTimeout(()=>{if(this.onended)this.onended()},Math.max(0,ms))}}
    stop(t){clearTimeout(this.__t);if(this.__out.some(n=>n&&n.__voice))window.__farpSend({t:'vstop',at:Math.max(t||0,now())})}
  }
  class FakeWorkletNode extends FakeNode{constructor(){super();this.port={postMessage:m=>window.__farpSend(m),onmessage:null}}}
  window.AudioWorkletNode=FakeWorkletNode;
  window.__fakeNode=new FakeWorkletNode();
  window.__fakeCtx={
    get currentTime(){return now()},
    get sampleRate(){return ST.sr||48000},
    state:'running',onstatechange:null,destination:new FakeNode(),baseLatency:0,outputLatency:0,
    resume(){return Promise.resolve()},suspend(){return Promise.resolve()},close(){return Promise.resolve()},
    createBuffer(ch,len,sr){return new AudioBuffer({numberOfChannels:ch,length:Math.max(1,len),sampleRate:sr})},
    decodeAudioData(ab,ok,bad){const p=offline?offline.decodeAudioData(ab):Promise.reject(new Error('no decoder'));if(ok)p.then(ok,bad);return p},
    createBufferSource(){return new FakeSource()},
    createGain(){return new FakeNode()},createStereoPanner(){return new FakeNode()},createDelay(){return new FakeNode()},createBiquadFilter(){return new FakeNode()},
    createOscillator(){const n=new FakeNode();n.start=n.stop=()=>{};return n},createDynamicsCompressor(){return new FakeNode()},createWaveShaper(){return new FakeNode()},
    createConstantSource(){const n=new FakeNode();n.offset=param(0);n.start=n.stop=()=>{};return n},createAnalyser(){const n=new FakeNode();n.getFloatTimeDomainData=()=>{};n.getByteFrequencyData=()=>{};return n},
    createMediaStreamSource(){const n=new FakeNode();n.__mic=true;return n},
    createScriptProcessor(){return new FakeNode()},
    audioWorklet:null};
  // the vocoder's "microphone" is the plugin's audio input
  try{if(!navigator.mediaDevices)Object.defineProperty(navigator,'mediaDevices',{value:{},configurable:true});
    navigator.mediaDevices.getUserMedia=()=>Promise.resolve({getTracks:()=>[],getAudioTracks:()=>[]})}catch(e){}

  // ---- the engine's messages and the host's MIDI keys ----
  window.__farpRx=pk=>{
    clockT=pk.clock||0;clockAt=performance.now();
    if(typeof onDsp!=='function')return;
    for(const m of pk.msgs||[]){try{if(m.t==='vli'){if(window.__vocPick)__vocPick(m.li);continue}onDsp(dec(m))}catch(e){console.warn(e)}}
    for(const k of pk.keys||[]){try{const n=Math.abs(k)-1;if(typeof markKey==='function')markKey(n,k>0)}catch(e){}}
    if(pk.scope)try{onDsp(pk.scope)}catch(e){}
  };

  // ---- the vocoder's lines, so the plugin can speak them when the host plays chords ----
  let lastCfg='';
  window.__vocState=(V,phrases)=>{try{
    const list=(phrases||[]).map(phraseId);
    const cfg={t:'vcfg',on:V.on&&V.src==='phrase'?1:0,trig:V.trig||'say',li:V.li||0,ids:list};
    const s=JSON.stringify(cfg);if(s!==lastCfg){lastCfg=s;window.__farpSend(cfg)}}catch(e){console.warn(e)}};

  // keyboard routing for the host's MIDI: the same rules as the on-screen keyboard
  let lastRoute='';
  const pushRoute=()=>{try{const r={t:'route',keys:window.KEYS_INST||'',split:window.SPLIT||{on:false},vr:window.VOC_ROUTE||null};
    const s=JSON.stringify(r);if(s!==lastRoute){lastRoute=s;window.__farpSend(r)}}catch(e){}};
  setInterval(pushRoute,250);

  // power on by itself once the page has loaded
  window.addEventListener('load',()=>{window.__farpSend({t:'vreset'});setTimeout(async()=>{try{if(typeof power==='function')await power()}catch(e){console.warn(e)}pushRoute();emit('farpReady',{})},30)});
})();
