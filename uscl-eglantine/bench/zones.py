"""Average Nx2Profile back-thread reports: zones.py LOG first_frac last_frac"""
import re, sys
L=[l for l in open(sys.argv[1],errors="replace") if "GPU back thread per second" in l]
lo,hi=float(sys.argv[2]),float(sys.argv[3]); L=L[int(len(L)*lo):int(len(L)*hi)]
acc={}; cnt={}
for l in L:
    for m in re.finditer(r"\| ([a-z /]+?) ([\d.]+) ms \((\d+)/s\)", l):
        acc[m[1]]=acc.get(m[1],0)+float(m[2]); cnt[m[1]]=cnt.get(m[1],0)+int(m[3])
n=len(L); draws=cnt["draw"]/n
busy=sum(v for k,v in acc.items() if k not in("split wait",))/n
print(f"{n} reports, draws/s {draws:.0f}, busy {busy:.0f} ms/s (excl split wait), per draw {busy*1000/draws:.2f} us")
for k,v in sorted(acc.items(),key=lambda x:-x[1]):
    print(f"  {k:<22}{v/n:7.1f} ms/s {100*v/n/busy:5.1f}%  {v/n*1e6/draws:6.0f} ns/draw  {cnt[k]/n/draws:6.2f}/draw")
