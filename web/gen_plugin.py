# Builds the plugin's page (web/index.html) from the F.A.R.P. web app: adds the plugin bridge and leaves out
# the screens the plugin doesn't have (sequencer, pads, files); the host does sequencing and recording.
import sys, os, re
src = sys.argv[1] if len(sys.argv) > 1 else '/home/claude/farp.html'
here = os.path.dirname(os.path.abspath(__file__))
s = open(src, encoding='utf-8').read()
shim = open(os.path.join(here, 'plugin_shim.js'), encoding='utf-8').read()

def rep(a, b, count=1):
    global s
    n = s.count(a)
    if n != count:
        raise SystemExit('patch failed (%d matches): %r' % (n, a[:80]))
    s = s.replace(a, b)

# storage and audio engine
rep("const LS={get(k,d){", "const LS=window.__PLUGIN_LS||{get(k,d){")
rep("ctx=new (window.AudioContext||window.webkitAudioContext)({latencyHint:'interactive'});",
    "ctx=window.FARP_PLUGIN?window.__fakeCtx:new (window.AudioContext||window.webkitAudioContext)({latencyHint:'interactive'});")
rep("  try{\n    const src=[WmsDsp,", "  try{if(window.FARP_PLUGIN){node=window.__fakeNode;send=window.__farpSend;}else{\n    const src=[WmsDsp,")
rep("    send=m=>node.port.postMessage(m);\n  }catch(err){", "    send=m=>node.port.postMessage(m);\n  }}catch(err){")
# screens: no sequencer, pads or files in the plugin
rep("['seq','SEQUENCER'],['pads','PADS'],", "['pads','PADS'],")
rep(",['file','FILES · MIDI']", "")
rep("['seq','SEQ'],['pads','PADS'],", "['pads','PADS'],")
rep(",['file','I/O']", "")
# pads: the plugin's sampler plays them; the project (not the web view) keeps the user's samples
rep("Sampler.trigger=function(i,vel,at,x){\n  if(!ctx||!padBus)return;const raw=",
    "Sampler.trigger=function(i,vel,at,x){\n  if(window.FARP_PLUGIN){if(!ctx)return;if(window.__syncPads)__syncPads();const t=Math.max(at||0,ctx.currentTime);"
    "__farpSend({t:'padtrig',i,v:vel,at:t,x:x?{tune:x.tune,decay:x.decay,cut:x.cut,slice:x.slice,vel:x.vel}:null});"
    "const ms=Math.max(0,(t-ctx.currentTime)*1000);setTimeout(()=>flashPad(LVX.on&&i===LVX.src&&x&&x.__k!==undefined?x.__k:i),ms);return}\n  if(!ctx||!padBus)return;const raw=")
rep("async function loadUserSamples(){", "async function loadUserSamples(){if(window.FARP_PLUGIN)return;")
rep("async function idbPut(i,v){", "async function idbPut(i,v){if(window.FARP_PLUGIN)return;")
rep("||!!window.FarpAndroid;\n[pdDir,pdMulti]", "||(!!window.FarpAndroid&&!window.FARP_PLUGIN);\n[pdDir,pdMulti]")
# the studio rack runs in the plugin: send its slots there
rep("function wire(){if(!ready)return;", "function wire(){if(window.FARP_PLUGIN){__farpSend({t:'rack',route:RK.route,slots:RK.slots.map(s=>s.u&&RACK[s.u]?{u:s.u,cfg:cfgOf(s)}:null)});return}if(!ready)return;")
rep("function push(i){const n=nodes[i]", "function push(i){if(window.FARP_PLUGIN){wire();return}const n=nodes[i]")
rep("async function init(){\n  if(ready||", "async function init(){if(window.FARP_PLUGIN){ready=true;window.__rackRepush=wire;wire();return}\n  if(ready||")
rep("+s * 60000 / (SEQ.bpm || 120)", "+s * 60000 / (window.__hostBpm || SEQ.bpm || 120)")
# ROMs from the plugin's ROM folder
rep("JSON.parse(FarpAndroid.romList()||'[]');romPath=FarpAndroid.romFolder()||'';",
    "JSON.parse((await FarpAndroid.romList())||'[]');romPath=(await FarpAndroid.romFolder())||'';")
rep("fetch('https://appassets.androidplatform.net/roms/'+", "fetch((window.__ROMBASE||'https://appassets.androidplatform.net/roms/')+")
# vocoder: lines go to the plugin; the "microphone" is the plugin's input
rep("const save=()=>LS.set('farp_voc',V);", "const save=()=>{LS.set('farp_voc',V);try{if(window.__vocState)__vocState(V,phrases)}catch(e){}};")
rep("if(fileVoice){phrases=[fileVoice];V.li=0;phrase=fileVoice;return}", "if(fileVoice){phrases=[fileVoice];V.li=0;phrase=fileVoice;if(window.__vocState)__vocState(V,phrases);return}")
rep("if(V.li>=phrases.length)V.li=0;phrase=phrases[V.li]||null}", "if(V.li>=phrases.length)V.li=0;phrase=phrases[V.li]||null;if(window.__vocState)__vocState(V,phrases)}")
rep("function pickLine(i){", "window.__vocPick=i=>pickLine(i);function pickLine(i){")
rep("['mic','Microphone']", "[\'mic\',window.FARP_PLUGIN?\'Plugin input (mic / sidechain)\':\'Microphone\']")
rep("toast('Mic on. Use headphones so the speakers don’t feed back.')", "toast(window.FARP_PLUGIN?'Voice from the plugin’s audio input':'Mic on. Use headphones so the speakers don’t feed back.')")

# head: charset, state from the plugin, the bridge, plugin-only styles
s = s.replace('<meta name="claude:viewport-fit-sides" content="cover">\n', '').replace('<meta charset="utf-8">\n', '', 1)
k = s.index('</style>\n') + len('</style>\n')
head, body = s[:k], s[k:]
css = ('<style>html{box-sizing:border-box}body{margin:0}'
       '.transport{display:none!important}.keys-hint{display:none!important}'
       '</style>\n')
out = ('<!doctype html>\n<html lang="en">\n<head>\n<meta charset="utf-8">\n'
       '<meta name="viewport" content="width=device-width,initial-scale=1">\n'
       '<script src="state.js"></script>\n<script>\n' + shim + '\n</script>\n'
       + head + css + '</head>\n<body>\n' + body.lstrip('\n') + '\n</body>\n</html>\n')
open(os.path.join(here, 'index.html'), 'w', encoding='utf-8').write(out)
print('index.html', len(out))
