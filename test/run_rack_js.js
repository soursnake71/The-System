// Runs one rack unit (the page's own worklet code) over the test input; writes interleaved stereo float32.
const fs=require('fs');const vm=require('vm');const [,,unit,preset,out]=process.argv;
const {RACK,syncMs}=require('./rack_defs.js');
const d=RACK[unit];const s={u:unit,p:preset==='def'?{}:Object.assign({},d.presets[preset])};
const p=Object.assign(d.def(),s.p);if(p.sync&&p.sync!=='free'){if(unit==='pt')p.dA=syncMs(p.sync);else p.delay=syncMs(p.sync)}if(p.syncB&&p.syncB!=='free')p.dB=syncMs(p.syncB);
let r=0x12345678;const M=Object.create(Math);M.random=()=>{r=(Math.imul(r,1664525)+1013904223)>>>0;return (r>>>8)/16777216};
const procs={};
const ctx={Math:M,Float32Array,Float64Array,Array,Object,JSON,console,sampleRate:48000,currentFrame:0,currentTime:0,
  AudioWorkletProcessor:class{constructor(){this.port={postMessage(){},onmessage:null}}},registerProcessor:(n,c)=>{procs[n]=c}};
vm.createContext(ctx);vm.runInContext(fs.readFileSync(__dirname+'/rack_src.js','utf8'),ctx);
const P=new procs[d.proc]({processorOptions:{cfg:p}});
const inp=new Float32Array(fs.readFileSync(__dirname+'/rack_in.f32').buffer.slice(0));const B=128,n=inp.length/B|0;const res=new Float32Array(n*B*2);
const L=new Float32Array(B),R=new Float32Array(B);
for(let k=0;k<n;k++){const a=inp.subarray(k*B,k*B+B);P.process([[a]],[[L,R]]);for(let i=0;i<B;i++){res[(k*B+i)*2]=L[i];res[(k*B+i)*2+1]=R[i]}}
fs.writeFileSync(out,Buffer.from(res.buffer));
fs.writeFileSync(out+'.cfg.json',JSON.stringify(p));
