// Runs a test script through the page's own JS engine (FarpDSP) and writes interleaved float32 stereo.
const fs=require('fs');const vm=require('vm');
const [,,dspFile,script,out]=process.argv;
const ctx={Math,Float32Array,Float64Array,Uint8Array,Int8Array,Int32Array,Uint32Array,Array,Object,JSON,console,String,Number,Boolean};
vm.createContext(ctx);vm.runInContext(fs.readFileSync(dspFile,'utf8')+';this.FarpDSP=FarpDSP;',ctx);
const S=JSON.parse(fs.readFileSync(script,'utf8'));
const dec=v=>{if(Array.isArray(v))return v.map(dec);if(v&&typeof v==='object'){if(v.$f!==undefined){const b=Buffer.from(v.$f,'base64');return new Float32Array(b.buffer.slice(b.byteOffset,b.byteOffset+b.length))}
  if(v.$u!==undefined){const b=Buffer.from(v.$u,'base64');return new Uint8Array(b)}const o={};for(const k in v)o[k]=dec(v[k]);return o}return v};
// Math.random is only used by the FM LFO's sample-and-hold; make it repeatable
let r=0x12345678;ctx.Math=Object.create(Math);ctx.Math.random=()=>{r=(Math.imul(r,1664525)+1013904223)>>>0;return (r>>>8)/16777216};
const d=new ctx.FarpDSP(S.fs);d.post=()=>{};
const voice=S.voice?new Float32Array(fs.readFileSync(require('path').dirname(script)+'/voice.f32').buffer.slice(0)):null;
const N=128,L=new Float32Array(N),R=new Float32Array(N),res=new Float32Array(S.blocks*N*2);
const ev=S.ev.map(e=>({b:e.b,m:dec(e.m)}));let ei=0;const t0=Date.now();
for(let b=0;b<S.blocks;b++){while(ei<ev.length&&ev[ei].b<=b)d.msg(ev[ei++].m);
  const vIn=voice?voice.subarray(b*N,b*N+N):null;d.process(L,R,N,0,null,null,null,null,null,vIn);
  for(let i=0;i<N;i++){res[(b*N+i)*2]=L[i];res[(b*N+i)*2+1]=R[i]}}
fs.writeFileSync(out,Buffer.from(res.buffer));console.log('js ms',Date.now()-t0);
