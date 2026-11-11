import idautils, idc

out = []
for ea in idautils.Heads(0x10400, 0x11400):
    out.append("%x: %s" % (ea, idc.generate_disasm_line(ea, 0)))
print("\n".join(out))
