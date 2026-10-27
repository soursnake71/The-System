# Measures John Nolting's SP-12 recording of noise through each output (noise_20k_all_ch.wav, ccrma.stanford.edu/~nolting/220c/)
# and writes measured.json for twelve/tools/sp-fit.mjs and sp-sweep.mjs. usage: python3 sp-measure.py <dir with ref/ and spice/, an/>
import numpy as np, scipy.io.wavfile as w, scipy.signal as sg, sys
S=sys.argv[1]
fs,x=w.read(S+'/ref/noise_20k_all_ch.wav'); x=x.astype(float)/32768; m=x[:,0]
e=np.convolve(np.abs(m),np.ones(441)/441,'same')
on=e>0.01
# segments
idx=np.flatnonzero(np.diff(on.astype(int)))
segs=[]; s=None
for i in idx:
    if on[i+1]: s=i
    elif s is not None: segs.append((s,i)); s=None
segs=[(a,b) for a,b in segs if b-a>0.05*fs]
print(len(segs),'segments', [(round(a/fs,2),round((b-a)/fs,3)) for a,b in segs][:40])
segs=segs[:7]+[(int(21.68*fs),int(23.6*fs))]
names=[8,7,6,5,4,3,2,1]
P={}
for (a,b),n in zip(segs,names):
    f,p=sg.welch(m[a+2000:b-200],fs,nperseg=4096); P[n]=p
def spice(name):
    d=np.loadtxt(S+'/spice/'+name+'.dat',skiprows=1); return d[:,0],d[:,1]
freqs=[100,300,1000,2000,3000,4000,5000,6000,8000,10000,12000,13000,15000,18000]
print('measured dB re output 8 (real SP-12)   vs  SPICE circuit (dB re its 100 Hz)')
for n in [7,6,5,4,3,2,1]:
    r=10*np.log10(P[n]/P[8]); r0=np.interp(100,f,r)
    row=' '.join(f'{np.interp(q,f,r)-r0:6.1f}' for q in freqs)
    print(f'out{n} meas ',row)
    if 3<=n<=6:
        sf,sm=spice(f'fixed{n}'); sm=sm-np.interp(100,sf,sm)
        print(f'out{n} spice',' '.join(f'{np.interp(q,sf,sm):6.1f}' for q in freqs))
print('freqs     ',' '.join(f'{q:6d}' for q in freqs))
# levels
for n in names: print('out',n,'level dB',round(10*np.log10(np.mean(P[n][(f>100)&(f<500)])),1))
import json
out={'f':[float(v) for v in f]}
for n in [1,2,3,4,5,6,7]:
    r=10*np.log10(P[n]/P[8]); out[str(n)]=[float(v-np.interp(200,f,r)) for v in r]
# dynamic outputs over time: 23 ms frames from the trigger, spectrum relative to output 8's average
for n,(a,b) in zip([2,1],[segs[6],segs[7]]):
    fr=[]
    for t0 in np.arange(0,0.6,0.01):
        s=a+int(t0*fs); seg=m[s:s+1024]
        if len(seg)<1024: break
        F,pp=sg.welch(seg,fs,nperseg=512)
        fr.append([float(v) for v in 10*np.log10(pp/np.interp(F,f,P[8]))])
    out['t'+str(n)]={'f':[float(v) for v in F],'frames':fr}
json.dump(out,open(S+'/an/measured.json','w'))
print('saved')
