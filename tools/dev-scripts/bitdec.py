import sys
SZ={1:1,2:8,3:8,4:16,5:16,6:16,7:32,8:32,9:64,10:64,13:32,14:64}
def dec(hexs, start):
    b=bytes.fromhex(hexs.replace(',','').replace(' ',''))
    bits=[(x>>i)&1 for x in b for i in range(8)]
    p=start; out=[]
    def rd(n):
        nonlocal p
        v=sum(bits[p+i]<<i for i in range(n)); p+=n; return v
    while p+5<=len(bits):
        t=rd(5)
        if t not in SZ: out.append(f"?type{t}@{p-5}"); break
        out.append((t, hex(rd(SZ[t]))))
    return out
for h in sys.argv[1:]:
    for s in range(0,9): print(s, dec(h,s)[:8])
    print()
