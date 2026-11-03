import sys; sys.argv=[sys.argv[0]]
exec(open('tools/dn1dis.py').read().split("if __name__ == '__main__':")[0])
cov=bytearray(len(img))
for lin,(s,ins) in insns.items():
    for i in range(ins.size): cov[lin+i]=1
for s in CODESEGS[:3]:
    a=s*16; e=seg_end(s); c=sum(cov[a:e]); print(hex(s), c, e-a)
    # list uncovered gaps > 16
    gap=None
    for x in range(a,e+1):
        u = x<e and not cov[x]
        if u and gap is None: gap=x
        if not u and gap is not None:
            if x-gap>=24: print('  gap',hex(gap-a),hex(x-a),x-gap, img[gap:gap+8].hex())
            gap=None
