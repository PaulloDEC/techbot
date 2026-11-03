import re,collections,json
txt=open('build/dn1.asm').read().split('\n;'+'='*70+'\n')
refs=collections.defaultdict(lambda: collections.Counter())
for blk in txt:
    m=re.match(r'; (\S+)\s+\((\w+):(\w+)',blk)
    if not m: continue
    fn=m.group(2)+':'+m.group(3)
    for line in blk.split('\n')[1:]:
        if 'es:' in line or 'cs:' in line: continue
        for a in re.findall(r'(?:byte|word|dword) ptr \[(0x[0-9a-f]+)\]',line):
            refs[int(a,16)][fn]+=1
        for a in re.findall(r'\[(?:bx|si|di|bx \+ si|bx \+ di) \+ (0x[0-9a-f]+)\]',line):
            refs[int(a,16)][fn+'*']+=1
json.dump({'%04x'%k:dict(v) for k,v in sorted(refs.items())},open('build/dataref.json','w'),indent=0)
for k,v in sorted(refs.items()):
    print('%04x'%k, sum(v.values()), ' '.join(sorted(v)[:10]))
