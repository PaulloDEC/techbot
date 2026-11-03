import sys; sys.argv=[sys.argv[0]]
exec(open('tools/dn1dis.py').read().split("if __name__ == '__main__':")[0])
for lin in sorted(insns):
    s,ins=insns[lin]
    if ins.mnemonic in ('jmp','call','lcall','ljmp') and ins.operands[0].type!=X86_OP_IMM:
        prev=[insns[a][1] for a in range(lin-14,lin) if a in insns]
        print(hex(s),hex(ins.address),ins.mnemonic,ins.op_str,' <= ',' ; '.join(p.mnemonic+' '+p.op_str for p in prev[-4:]))
