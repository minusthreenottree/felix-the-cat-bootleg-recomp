import sys
from capstone import *

rom = open(sys.argv[1], "rb").read()
start = int(sys.argv[2], 16)
end = int(sys.argv[3], 16)
md = Cs(CS_ARCH_M68K, CS_MODE_BIG_ENDIAN | CS_MODE_M68K_000)
md.detail = False
code = rom[start:end]
addr = start
for ins in md.disasm(code, addr):
    print("%06X: %-24s %s %s" % (ins.address,
          ins.bytes.hex(),
          ins.mnemonic, ins.op_str))
