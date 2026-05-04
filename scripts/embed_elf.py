#!/usr/bin/env python3
"""Embed the compiled user ELF bytes without external binary conversion tools."""
from pathlib import Path
import sys

def embed(source,destination):
    data=source.read_bytes()
    if len(data)<64 or len(data)>1024*1024 or data[:7]!=b'\x7fELF\x02\x01\x01':
        raise ValueError('Expected a bounded ELF64 little-endian input')
    rows=[', '.join(f'0x{byte:02x}' for byte in data[i:i+16]) for i in range(0,len(data),16)]
    destination.write_text('#include <stddef.h>\n#include <stdint.h>\nextern "C" {\n'
        'extern const uint8_t mini_os_user_elf[] = {\n'+',\n'.join(rows)+'\n};\n'
        f'extern const size_t mini_os_user_elf_size = {len(data)};\n'+'}\n')
if __name__=='__main__':
    try:
        if len(sys.argv)!=3:raise ValueError('Usage: embed_elf.py INPUT_ELF OUTPUT_CPP')
        embed(Path(sys.argv[1]),Path(sys.argv[2]))
    except (OSError,ValueError) as error:
        print(f'ELF embedding failed: {error}',file=sys.stderr);sys.exit(1)
