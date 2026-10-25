import sys, numpy as np
a=np.fromfile(sys.argv[1],dtype='<f4'); b=np.fromfile(sys.argv[2],dtype='<f4')
n=min(len(a),len(b)); a=a[:n]; b=b[:n]
ra=np.sqrt(np.mean(a**2)); rb=np.sqrt(np.mean(b**2)); err=np.sqrt(np.mean((a-b)**2))
nan=np.isnan(b).sum()+np.isinf(b).sum()
# first sample that differs noticeably
d=np.abs(a-b); idx=np.argmax(d>1e-4) if (d>1e-4).any() else -1
print("%-14s rmsJS %.4f rmsC %.4f err %.2e rel %.2e firstDiff %s maxd %.3g %s"%(sys.argv[3], ra, rb, err, err/max(ra,1e-9), idx//2 if idx>=0 else '-', d.max(), 'NAN!' if nan else ''))
