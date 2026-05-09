#!/usr/bin/env python3
"""Inspect the compiled static user ELF and its exact embedding in the kernel."""

from pathlib import Path
import struct
import sys
from verify_elf import inspect as inspect_kernel, require


def inspect(data):
    require(len(data) >= 64 and data[:9] == b'\x7fELF\x02\x01\x01\x00\x00', 'Expected static ELF64 little-endian user image')
    kind,machine,version,entry,phoff,shoff,flags,ehsize,phsize,phcount,shsize,shcount,names=struct.unpack_from('<HHIQQQIHHHHHH',data,16)

    require((kind,machine,version,flags,ehsize,phsize,shsize)==(2,183,1,0,64,56,64),'Invalid AArch64 user ELF header')
    require(entry==0x1000000 and phcount==3,'Unexpected user entry or segment count')
    require(phoff+phcount*phsize<=len(data) and shoff+shcount*shsize<=len(data),'Truncated user tables')
    loads=[]

    for i in range(phcount):
        kind,flags,offset,address,_,file_size,memory_size,alignment=struct.unpack_from('<IIQQQQQQ',data,phoff+i*phsize)

        require(kind==1 and flags in (4,5,6),'Unexpected user program header or permissions')
        require(file_size<=memory_size and offset+file_size<=len(data),'Invalid user file extent')
        require(alignment==4096 and offset%alignment==address%alignment,'Misaligned user extent')
        require(0x1000000<=address<address+memory_size<=0x1003000,'User segments exceed guarded image window')
        loads.append((address,offset,file_size,memory_size,flags))

    require(tuple(s[4] for s in loads)==(5,4,6),'Expected separate RX, R and RW user segments')
    require(loads[2][3]>loads[2][2]>0,'User example must exercise initialized data and BSS')
    require(all(a[0]+a[3]<=b[0] for a,b in zip(loads,loads[1:])),'Overlapping user segments')
    sections=[struct.unpack_from('<IIQQQQIIQQ',data,shoff+i*shsize) for i in range(shcount)]
    symbols={}

    for section in sections:
        _,kind,flags,_,offset,size,link,_,_,entsize=section

        require(not flags&0x400 and not (kind in (4,6,9,14,15,16) and size),'Unsupported user runtime dependencies')
        require(kind==8 or offset+size<=len(data),'Truncated user section')

        if kind!=2:continue

        require(entsize==24 and size%entsize==0 and link<len(sections),'Invalid user symbol table')
        strings=sections[link];table=data[strings[4]:strings[4]+strings[5]]

        for pos in range(offset,offset+size,entsize):
            nameoff,_,_,index,value,_=struct.unpack_from('<IBBHQQ',data,pos)
            end=table.find(b'\0',nameoff)

            require(0<=nameoff<len(table) and end>=0,'Invalid user symbol name')
            name=table[nameoff:end].decode()

            require(not name or index!=0,'Unresolved user symbol: '+name)

            if name:symbols[name]=value

    require(symbols.get('_user_start')==entry and 'user_main' in symbols and 'user_write' in symbols,'Missing compiled user entry or ABI')
    text=loads[0]

    for name,number in (('user_write',1),('user_monotonic_time',3),('user_sys_info',4)):
        address=symbols.get(name,0)
        require(address%4==0 and text[0]<=address and address+12<=text[0]+text[2], 'Missing or invalid user wrapper: '+name)
        instructions=struct.unpack_from('<III',data,text[1]+address-text[0])
        require(instructions==(0xd2800008|(number<<5),0xd4000001,0xd65f03c0), 'Incorrect syscall wrapper: '+name)

    site=symbols.get('user_elf_exit_site',0)

    require(text[0]<=site and site+4<=text[0]+text[2] and site%4==0,'Invalid user exit site')
    require(struct.unpack_from('<I',data,text[1]+site-text[0])[0]==0xd4000001,'User exit is not SVC #0')

    return {'entry':entry,'segments':loads,'exit_site':site}


def at_address(data,address,size):
    phoff=struct.unpack_from('<Q',data,32)[0]
    phsize,phcount=struct.unpack_from('<HH',data,54)

    for i in range(phcount):
        kind,_,offset,virtual,_,filesz,_,_=struct.unpack_from('<IIQQQQQQ',data,phoff+i*phsize)

        if kind==1 and virtual<=address and address+size<=virtual+filesz:
            return data[offset+address-virtual:offset+address-virtual+size]

    raise ValueError('Embedded user data is not in a file-backed kernel segment')


def embedded(kernel,source=None):
    symbols=inspect_kernel(kernel,verbose=False)

    require('mini_os_user_elf' in symbols and 'mini_os_user_elf_size' in symbols,'Missing embedded user ELF')
    data=kernel.read_bytes()
    size=struct.unpack('<Q',at_address(data,symbols['mini_os_user_elf_size'],8))[0]

    require(64<=size<=1024*1024,'Invalid embedded user size')
    image=at_address(data,symbols['mini_os_user_elf'],size)

    if source is not None:require(image==source.read_bytes(),'Kernel does not embed the compiled user ELF exactly')

    return inspect(image)


if __name__=='__main__':
    try:
        require(len(sys.argv)==3,'Usage: verify_user_elf.py KERNEL_ELF USER_ELF')
        result=embedded(Path(sys.argv[1]),Path(sys.argv[2]))

        print(f"User ELF verified: entry={result['entry']:#x}, three protected segments, BSS, static ABI and exact embedding")
    except (OSError,ValueError,struct.error) as error:
        print(f'User ELF verification failed: {error}',file=sys.stderr);sys.exit(1)
