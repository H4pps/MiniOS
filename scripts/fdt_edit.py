"""Bounded test-only insertion of static reserved-memory regions into a QEMU DTB."""

import struct


def add_test_reservations(blob):
    if len(blob)<40: raise ValueError('Truncated DTB header')

    header=list(struct.unpack_from('>10I',blob))
    magic,total,structure,strings,reservations,version,compatible,_,names_size,structure_size=header

    if magic!=0xd00dfeed or version<17 or compatible>17 or total>len(blob) or structure%4 or structure<40 or strings<structure+structure_size or strings+names_size>total or reservations<40 or reservations%8 or reservations>=structure:
        raise ValueError('Invalid DTB header')

    body=blob[structure:structure+structure_size];names=bytearray(blob[strings:strings+names_size])
    offset=0;depth=0;root_end=None;ended=False;address_cells=2;size_cells=1

    def text(data,pos):
        end=data.find(b'\0',pos)

        if pos<0 or end<0: raise ValueError('Unterminated DTB name')

        return data[pos:end],end+1

    while offset<len(body):
        if offset+4>len(body): raise ValueError('Truncated DTB token')

        token=struct.unpack_from('>I',body,offset)[0];start=offset;offset+=4

        if token==1:
            name,offset=text(body,offset);offset=(offset+3)&~3

            if depth==0 and (name or root_end is not None): raise ValueError('Invalid DTB root')

            if depth==1 and name==b'reserved-memory': raise ValueError('Reserved-memory already exists')

            depth+=1

            if depth>32: raise ValueError('Excessive DTB depth')
        elif token==2:
            if depth==0: raise ValueError('Unbalanced DTB nodes')

            depth-=1

            if depth==0: root_end=start
        elif token==3:
            if depth==0 or offset+8>len(body): raise ValueError('Invalid DTB property')

            length,name_offset=struct.unpack_from('>II',body,offset);offset+=8

            if offset+length>len(body) or name_offset>=len(names): raise ValueError('Truncated DTB property')

            name,_=text(names,name_offset)

            if depth==1 and name in (b'#address-cells',b'#size-cells'):
                if length!=4: raise ValueError('Invalid root cells')

                value=struct.unpack_from('>I',body,offset)[0]

                if value not in (1,2): raise ValueError('Unsupported root cells')

                if name==b'#address-cells': address_cells=value
                else: size_cells=value

            offset=(offset+length+3)&~3
        elif token==4: pass
        elif token==9:
            if depth or root_end is None or offset!=len(body): raise ValueError('Invalid DTB end')

            ended=True;break
        else: raise ValueError('Unknown DTB token')

    if not ended: raise ValueError('Missing DTB end')

    def cell(value): return struct.pack('>I',value)

    def pad(value): return value+b'\0'*((-len(value))%4)

    def begin(name): return cell(1)+pad(name.encode()+b'\0')

    def prop(name,value):
        pos=len(names);names.extend(name.encode()+b'\0')

        return cell(3)+cell(len(value))+cell(pos)+pad(value)

    def number(value,cells): return value.to_bytes(cells*4,'big')

    added=begin('reserved-memory')+prop('#address-cells',cell(address_cells))+prop('#size-cells',cell(size_cells))+prop('ranges',b'')

    for name,base,size,flag in (('reusable@41000000',0x41000000,0x40000,'reusable'),('firmware@41400000',0x41400000,0x3000,None),('hole@41500000',0x41500000,0x4000,'no-map')):
        added+=begin(name)+prop('reg',number(base,address_cells)+number(size,size_cells))

        if flag: added+=prop(flag,b'')

        added+=cell(2)

    added+=cell(2)
    patched=body[:root_end]+added+body[root_end:]
    result=bytearray(blob[:structure])+patched+names

    if len(result)>0x200000: raise ValueError('DTB exceeds reserved window')

    header[1]=len(result);header[3]=structure+len(patched);header[8]=len(names);header[9]=len(patched)

    struct.pack_into('>10I',result,0,*header)

    return bytes(result)
