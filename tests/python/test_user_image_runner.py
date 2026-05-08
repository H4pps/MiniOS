"""Static user ELF inspection, exact kernel embedding, and build generation errors."""

from pathlib import Path
import re
import struct
import tempfile
import unittest
from unittest.mock import patch
import test_boot_runner
import embed_elf
import verify_user_elf


def image():
    data=bytearray(0x3400)
    data[:9]=b'\x7fELF\x02\x01\x01\x00\x00'

    struct.pack_into('<HHIQQQIHHHHHH',data,16,2,183,1,0x1000000,64,0x3000,0,64,56,3,64,3,0)

    for i,(address,size,memory,flags) in enumerate(((0x1000000,16,16,5),(0x1001000,14,14,4),(0x1002000,8,264,6))):
        struct.pack_into('<IIQQQQQQ',data,64+i*56,1,flags,0x1000+i*4096,address,address,size,memory,4096)

    struct.pack_into('<I',data,0x1008,0xd4000001)
    names=b'\0_user_start\0user_main\0user_write\0user_elf_exit_site\0'
    data[0x3300:0x3300+len(names)]=names

    struct.pack_into('<IIQQQQIIQQ',data,0x3040,0,2,0,0,0x3100,5*24,2,0,8,24)
    struct.pack_into('<IIQQQQIIQQ',data,0x3080,0,3,0,0,0x3300,len(names),0,0,1,0)

    for i,(name,value) in enumerate((('_user_start',0x1000000),('user_main',0x1000004),('user_write',0x100000c),('user_elf_exit_site',0x1000008)),1):
        struct.pack_into('<IBBHQQ',data,0x3100+i*24,names.index(name.encode()),0x12,0,1,value,0)

    return data


class UserImageTests(unittest.TestCase):
    def test_static_layout_bss_syscall_symbols_and_ignored_physical_address(self):
        data=image();result=verify_user_elf.inspect(data)

        self.assertEqual(result['entry'],0x1000000);self.assertEqual(result['exit_site'],0x1000008)
        self.assertGreater(result['segments'][2][3],result['segments'][2][2])
        struct.pack_into('<Q',data,64+24,0xffffffffffffffff)
        self.assertEqual(verify_user_elf.inspect(data),result)

    def test_rejects_architecture_layout_permissions_runtime_symbols_and_wrong_exit(self):
        for offset,fmt,value in ((18,'H',62),(24,'Q',0x1000004),(56,'H',2),(68,'I',7),
                                 (64+2*56+40,'Q',8),(64+32,'Q',0xffffffffffffffff),
                                 (0x1008,'I',0),(0x3040+4,'I',6),(0x3100+24+6,'H',0)):
            data=image();struct.pack_into('<'+fmt,data,offset,value)

            with self.assertRaises((ValueError,struct.error),msg=str(offset)):verify_user_elf.inspect(data)

        with self.assertRaises(ValueError):verify_user_elf.inspect(b'\x7fELF')

    def test_exact_embedding_and_file_backing(self):
        user=image();data=bytearray(4096+len(user));data[:64]=user[:64]

        struct.pack_into('<Q',data,32,64);struct.pack_into('<HH',data,54,56,1)
        struct.pack_into('<IIQQQQQQ',data,64,1,4,0,0x40200000,0x40200000,len(data),len(data),4096)
        struct.pack_into('<Q',data,0x200,len(user));data[4096:]=user
        symbols={'mini_os_user_elf':0x40201000,'mini_os_user_elf_size':0x40200200}

        with tempfile.TemporaryDirectory() as directory:
            kernel=Path(directory)/'kernel.elf';source=Path(directory)/'user.elf'

            kernel.write_bytes(data);source.write_bytes(user)

            with patch.object(verify_user_elf,'inspect_kernel',return_value=symbols):
                self.assertEqual(verify_user_elf.embedded(kernel,source),verify_user_elf.inspect(user))
                source.write_bytes(user+b'wrong')

                with self.assertRaisesRegex(ValueError,'exactly'):verify_user_elf.embedded(kernel,source)

                with self.assertRaisesRegex(ValueError,'file-backed'):verify_user_elf.at_address(data,0x50000000,8)

            with patch.object(verify_user_elf,'inspect_kernel',return_value={}):
                with self.assertRaisesRegex(ValueError,'Missing'):verify_user_elf.embedded(kernel)

    def test_generator_preserves_all_bytes_and_rejects_wrong_or_oversized_images(self):
        with tempfile.TemporaryDirectory() as directory:
            source=Path(directory)/'user.elf';output=Path(directory)/'image.cpp'
            data=image();source.write_bytes(data);embed_elf.embed(source,output)
            generated=output.read_text()

            self.assertEqual(bytes(int(byte,16) for byte in re.findall(r'0x([0-9a-f]{2})',generated)),bytes(data))
            self.assertIn(f'mini_os_user_elf_size = {len(data)}',generated)

            for invalid in (b'not ELF',b'\x7fELF\x02\x01\x01',b'\x7fELF\x01\x01\x01',data+bytes(1024*1024)):
                source.write_bytes(invalid)

                with self.assertRaises(ValueError):embed_elf.embed(source,output)
