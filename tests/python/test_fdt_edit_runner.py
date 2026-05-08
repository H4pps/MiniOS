"""The DTB fixture editor preserves bounds and rejects malformed structure."""

import struct
import unittest
import test_boot_runner
from fdt_edit import add_test_reservations


def blob(cells=2):
    names=b'#address-cells\0#size-cells\0'
    body=struct.pack('>II',1,0)

    for offset in (0,15):body+=struct.pack('>IIII',3,4,offset,cells)

    body+=struct.pack('>II',2,9)
    header=struct.pack('>10I',0xd00dfeed,56+len(body)+len(names),56,56+len(body),40,17,16,0,len(names),len(body))

    return header+b'\0'*16+body+names


class DtbEditorTests(unittest.TestCase):
    def test_preserves_header_and_supports_cell_widths(self):
        for cells in (1,2):
            source=blob(cells); result=add_test_reservations(source)

            self.assertEqual(struct.unpack_from('>I',result,4)[0],len(result))
            self.assertEqual(result[40:56],source[40:56])
            self.assertIn(b'reusable@41000000',result);self.assertIn(b'no-map\0',result)

            with self.assertRaises(ValueError):add_test_reservations(result)

    def test_malformed_headers_tokens_and_depth_are_rejected(self):
        source=blob()

        for bad in (source[:39],b'bad!'+source[4:],source[:-1],source[:56]+struct.pack('>I',7)+source[60:]):
            with self.assertRaises(ValueError):add_test_reservations(bad)

        bad=bytearray(source);struct.pack_into('>I',bad,20,16)

        with self.assertRaises(ValueError):add_test_reservations(bytes(bad))

    def test_excessive_depth_is_rejected(self):
        source=bytearray(blob());body=struct.pack('>II',1,0)+struct.pack('>II',1,0x6e000000)*33+struct.pack('>I',2)*34+struct.pack('>I',9)
        names=source[struct.unpack_from('>I',source,12)[0]:]
        result=source[:56]+body+names

        struct.pack_into('>I',result,4,len(result));struct.pack_into('>I',result,12,56+len(body));struct.pack_into('>I',result,36,len(body))

        with self.assertRaises(ValueError):add_test_reservations(bytes(result))
