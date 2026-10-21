import hashlib
import struct
import sys
from pathlib import Path
try:
    import lz4.block
except ImportError:
    lz4 = None

elf = Path(sys.argv[1]).read_bytes()
assert elf[:6] == b'\x7fELF\x01\x01'
fields = struct.unpack_from('<HHIIIIIHHHHHH', elf, 16)
assert fields[1] == 40
phoff, shoff = fields[4:6]
phsize, phcount, shsize, shcount = fields[8:12]
segments = []
for index in range(phcount):
    segment = struct.unpack_from('<8I', elf, phoff + index * phsize)
    if segment[0] == 1:
        segments.append(segment)
assert len(segments) == 3
assert [segment[6] for segment in segments] == [5, 4, 6]
header = bytearray(256)
header[:4] = b'NSO0'
struct.pack_into('<I', header, 12, 0x3f if lz4 is not None else 0x38)
for index in range(shcount):
    section = struct.unpack_from('<10I', elf, shoff + index * shsize)
    if section[1] == 7:
        start, length = section[4:6]
        cursor = start
        while cursor + 12 <= start + length:
            namesize, descsize, kind = struct.unpack_from('<3I', elf, cursor)
            cursor += 12
            name = elf[cursor:cursor + namesize]
            cursor += (namesize + 3) & ~3
            if kind == 3 and name == b'GNU\0':
                build_id = elf[cursor:cursor + min(descsize, 32)]
                header[64:64 + len(build_id)] = build_id
            cursor += (descsize + 3) & ~3
payload = bytearray()
for index, segment in enumerate(segments):
    _, offset, address, _, size, memsize, _, _ = segment
    data = elf[offset:offset + size]
    assert len(data) == size
    encoded = lz4.block.compress(data, store_size=False) if lz4 is not None else data
    count = len(encoded)
    assert count > 0
    if lz4 is not None:
        assert lz4.block.decompress(encoded, uncompressed_size=size) == data
    struct.pack_into('<4I', header, 16 + index * 16, 256 + len(payload), address, size,
                     memsize - size if index == 2 else 1)
    struct.pack_into('<I', header, 96 + index * 4, count)
    header[160 + index * 32:192 + index * 32] = hashlib.sha256(data).digest()
    payload.extend(encoded)
Path(sys.argv[2]).write_bytes(header + payload)
print('AArch32 NSO: 3 segments verified against ELF; SHA256 checked; LZ4:', lz4 is not None)
