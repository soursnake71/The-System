# Builds engine test scripts from the page's recorded messages. Each script: {"fs":..,"blocks":..,"voice":0/1,"ev":[{"b":..,"m":..}]}
import json, sys, math, struct
log=[json.loads(l) for l in open(sys.argv[1])]
L=lambda i: log[i]
def notes(ev, b_on, b_off, ns, inst=None, m=7):
    for n in ns:
        on={"t":"on","n":n,"v":0.9}; off={"t":"off","n":n}
        if inst: on["inst"]=inst; off["inst"]=inst
        else: on["m"]=m
        ev.append({"b":b_on,"m":on}); ev.append({"b":b_off,"m":off})
def write(name, ev, blocks, voice=0, fs=48000):
    json.dump({"fs":fs,"blocks":blocks,"voice":voice,"ev":ev}, open("scripts/%s.json"%name,"w"))
mixmsg=L(3)
# strings presets
for k,idx in [(0,2),(5,5),(12,8),(20,11),(30,14),(41,17),(50,20)]:
    ev=[{"b":0,"m":L(idx)},{"b":0,"m":mixmsg}]
    notes(ev,10,300,[36,43,48,55,60,64,67]); notes(ev,320,420,[50,57,62],m=7)
    write("str_p%d"%k, ev, 560)
# a preset with flanger + delay + reverb forced on
p=dict(L(17)["p"]); p.update({"flOn":1,"dlOn":1,"rvOn":1,"fxMix":0.8,"dlMix":0.4,"rvMix":0.5,"bits":10,"chs":1,"multi":1})
ev=[{"b":0,"m":{"t":"all","p":p}},{"b":0,"m":mixmsg}]; notes(ev,10,250,[41,48,53,57,60]); write("str_fx",ev,500)
p=dict(L(17)["p"]); p.update({"model":1}); ev=[{"b":0,"m":{"t":"all","p":p}},{"b":0,"m":mixmsg}]; notes(ev,10,250,[41,48,53,57,60]); write("str_mk1",ev,500)
base=[{"b":0,"m":L(17)},{"b":0,"m":mixmsg}]
# P-5
ev=list(base); notes(ev,10,250,[48,55,60,64],inst="p5"); write("p5_init",ev,450)
ev=list(base)+[{"b":0,"m":L(40)}]; notes(ev,10,250,[48,55,60,64],inst="p5"); write("p5_sync",ev,450)
# E-II
for k,i in [(0,50),(3,53),(8,56)]:
    ev=list(base)+[{"b":0,"m":L(i)},{"b":0,"m":L(i+1)}]; notes(ev,10,250,[48,55,60,67],inst="e2"); write("e2_%d"%k,ev,450)
# FM-4
for k,i in [(0,88),(3,90),(8,92)]:
    ev=list(base)+[{"b":0,"m":L(i)}]; notes(ev,10,250,[48,55,60,64],inst="dx"); write("dx_%d"%k,ev,450)
# PD-8
for k,i in [(0,103),(3,105),(8,107),(9,148)]:
    ev=list(base)+[{"b":0,"m":L(i)}]; notes(ev,10,250,[48,55,60,64],inst="cz"); write("cz_%d"%k,ev,450)
# WAVES (SD-1)
for k,i in [(0,26),(3,119),(8,122)]:
    ev=list(base)+[{"b":0,"m":L(i)},{"b":0,"m":L(i+1)}]; notes(ev,10,250,[48,55,60,64],inst="sd1"); write("sd1_%d"%k,ev,450)
# PCM-32 (CM-32P)
for k,i in [(0,23),(8,138)]:
    ev=list(base)+[{"b":0,"m":L(i)},{"b":0,"m":L(i+1)}]; notes(ev,10,250,[48,55,60,64],inst="cm"); write("cm_%d"%k,ev,450)
# P-5 Rev 3 firmware: boot (9 s of autotune at 20 ms per block = 450 blocks), then play
ev=list(base)+[{"b":0,"m":L(25)},{"b":0,"m":{"t":"eng","i":"p5r3","op":"run","v":1}}]; notes(ev,470,700,[48,55,60,64],inst="p5r3"); write("p5r3",ev,900)
# Six-Trak firmware: boot ~13.25 s at 20 ms per block = 663 blocks
ev=list(base)+[{"b":0,"m":L(28)},{"b":0,"m":{"t":"eng","i":"six","op":"run","v":1}}]; notes(ev,680,900,[48,55,60,64],inst="six"); write("six",ev,1100)
# vocoders over the strings with a recorded-style voice
for eng in ["clear","svc","talker"]:
    vm=dict(L(151)); vm["eng"]=eng; vm["ch"]=-1; vm["tk"]={"mode":0,"mic":0.5,"inst":0.5,"out":0.7}
    ev=list(base)+[{"b":0,"m":vm}]; notes(ev,10,350,[48,55,60,63,67]); write("voc_"+eng,ev,450,voice=1)
vm=dict(L(151)); vm["eng"]="talker"; vm["ch"]=-1; vm["tk"]={"mode":5,"mic":0.5,"inst":0.6,"out":0.7}
ev=list(base)+[{"b":0,"m":vm}]; notes(ev,10,350,[48,55,60]); write("voc_talker5",ev,450,voice=1)
vm=dict(L(151)); vm["eng"]="clear"; vm["ch"]=7; ev=list(base)+[{"b":0,"m":L(90)},{"b":0,"m":vm}]; notes(ev,10,350,[48,55,60],inst="dx"); write("voc_dx",ev,450,voice=1)
# the voice: a 120 Hz glottal buzz through moving formants, 48 kHz, float32
N=1100*128; out=bytearray()
ph=0.0; seed=1
for i in range(N):
    t=i/48000.0
    f0=110+20*math.sin(2*math.pi*0.7*t)
    ph+=f0/48000.0
    ph-=math.floor(ph)
    g=(1-ph)**3*2-0.5
    f1=500+300*math.sin(2*math.pi*1.3*t); f2=1500+600*math.sin(2*math.pi*0.9*t)
    env=0.5*(1+math.sin(2*math.pi*2.1*t))
    seed=(seed*1103515245+12345)&0x7fffffff
    nz=(seed/0x7fffffff*2-1)*0.05
    v=(g*(0.6*math.sin(2*math.pi*f1*t)+0.4*math.sin(2*math.pi*f2*t))+nz)*env*0.4
    out+=struct.pack('<f',v)
open('scripts/voice.f32','wb').write(out)
print('ok')
