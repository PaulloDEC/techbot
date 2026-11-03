import re,sys,collections
txt=open('build/dn1.asm').read().split('\n;'+'='*70+'\n')
seg=sys.argv[1]
for blk in txt:
    m=re.match(r'; (\S+)\s+\((\w+):(\w+)',blk)
    if not m or m.group(2)!=seg: continue
    lines=blk.split('\n')
    n=len(lines)
    strs=re.findall(r'; "([^"]*)"',blk)
    ports=set(re.findall(r'(?:mov dx, |out |in al, )(0x[0-9a-f]+)',blk))
    calls=collections.Counter(re.findall(r'call (?:far )?(\w+)',blk))
    ints=set(re.findall(r'int (0x\w+)',blk))
    rets=set(re.findall(r'(retf? ?\d*)\s*$',blk,re.M))
    hdr=lines[0][:110]
    print(m.group(3),n,'ret=',','.join(sorted(rets)),'ints',','.join(ints),'ports',','.join(sorted(ports))[:60],'| calls',','.join(c.replace('sub_','') for c in list(calls)[:8]),'|',' / '.join(s[:25] for s in strs[:3]))
