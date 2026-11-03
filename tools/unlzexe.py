"""LZEXE 0.91 unpacker -> plain MZ exe."""
import struct, sys
def unpack(d):
    (sig,lastp,np,nrel,hdrp,minx,maxx,ss,sp,ck,ip,cs,relo,ov)=struct.unpack('<2s13H',d[:28])
    assert d[0x1c:0x20]==b'LZ91'
    hdr=hdrp*16; eseg=hdr+cs*16
    rip,rcs,rsp,rss,csize,inc,dlen=struct.unpack('<7H',d[eseg:eseg+14])
    # relocations
    p=eseg+0x158; rels=[]; off=0; seg=0
    while True:
        span=d[p]; p+=1
        if span==0:
            span=struct.unpack('<H',d[p:p+2])[0]; p+=2
            if span==0: seg+=0xfff; continue
            if span==1: break
        off+=span; seg+=(off&~0xf)>>4; off&=0xf
        rels.append((off,seg))
    # data
    p=hdr; out=bytearray()
    def w():
        nonlocal p; v=d[p]|d[p+1]<<8; p+=2; return v
    buf=w(); cnt=16
    def bit():
        nonlocal buf,cnt
        b=buf&1; cnt-=1
        if cnt==0: buf=w(); cnt=16
        else: buf>>=1
        return b
    def byte():
        nonlocal p; v=d[p]; p+=1; return v
    while True:
        if bit(): out.append(byte()); continue
        if not bit():
            ln=(bit()<<1|bit())+2; span=byte()|0xff00
        else:
            lo=byte(); hi=byte(); span=lo|((hi&~7)<<5)|0xe000; ln=(hi&7)+2
            if ln==2:
                ln=byte()
                if ln==0: break
                if ln==1: continue
                ln+=1
        span-=0x10000
        for _ in range(ln): out.append(out[span])
    # build new exe
    nhdr=0x1c+len(rels)*4; nhdrp=(nhdr+511)//512*32
    total=nhdrp*16+len(out)
    minalloc=max(0,(inc*16 - len(out) + csize*16)//16) if False else minx  # keep
    h=struct.pack('<2s13H',b'MZ',total%512,(total+511)//512,len(rels),nhdrp,
                  (csize+inc)*0 + minx + (csize*16 - len(out)+15)//16 if False else minx,
                  maxx,rss,rsp,0,rip,rcs,0x1c,0)
    h+=b''.join(struct.pack('<HH',o,s) for o,s in rels)
    h=h.ljust(nhdrp*16,b'\0')
    return h+bytes(out), dict(ip=rip,cs=rcs,sp=rsp,ss=rss,nrel=len(rels),size=len(out),rels=rels)
if __name__=='__main__':
    d=open(sys.argv[1],'rb').read()
    exe,info=unpack(d)
    open(sys.argv[2],'wb').write(exe)
    print({k:(hex(v) if isinstance(v,int) else len(v)) for k,v in info.items()})
