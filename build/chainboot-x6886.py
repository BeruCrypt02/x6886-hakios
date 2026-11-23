#!/usr/bin/env python3
"""Place a program in front of Android's kernel in a boot image."""
import gzip
import hashlib
import struct
import sys

CHAIN_OFFSET = 0x40000
BOOT_PARTITION = 32 << 20


def pad(b, n):
    return b + b'\0' * (-len(b) % n)


def main():
    src, prog_path, out = sys.argv[1:4]
    payload = open(sys.argv[4], 'rb').read() if len(sys.argv) > 4 else b''
    d = open(src, 'rb').read()
    assert d[:8] == b'ANDROID!', 'not an Android boot image'
    ksize, _, rsize, _, ssize, _, _, page, hver = struct.unpack_from('<9I', d, 8)
    if page == 0:
        page = 4096
    if hver not in (2, 4):
        raise SystemExit(f'header v{hver}, expected 2 or 4')
    if hver == 4:
        rdo_size = struct.unpack_from('<I', d, 1652)[0]
        dtb_size = struct.unpack_from('<I', d, 1668)[0]
    else:
        rdo_size = struct.unpack_from('<I', d, 1632)[0]
        dtb_size = struct.unpack_from('<I', d, 1648)[0]

    def pages(n):
        return (n + page - 1) // page * page

    off = page
    kernel = d[off:off + ksize]; off += pages(ksize)
    ramdisk = d[off:off + rsize]; off += pages(rsize)
    second = d[off:off + ssize]; off += pages(ssize)
    rdo = d[off:off + rdo_size]; off += pages(rdo_size)
    dtb = d[off:off + dtb_size]

    if kernel[:2] == b'\x1f\x8b':
        image = gzip.decompress(kernel)
    elif kernel[:4] == b'\x02\x21\x4c\x18':
        import subprocess
        import tempfile
        import os
        fd, tmp = tempfile.mkstemp()
        os.close(fd)
        with open(tmp, 'wb') as f:
            subprocess.run(['lz4', '-dc', '-'], input=kernel, stdout=f, check=True)
        image = open(tmp, 'rb').read()
        os.unlink(tmp)
    else:
        image = kernel
    assert image[0x38:0x3c] == b'ARM\x64', 'Android kernel is not an arm64 Image'
    linux_image_size = struct.unpack_from('<Q', image, 0x10)[0]

    prog = bytearray(open(prog_path, 'rb').read())
    assert prog[0x38:0x3c] == b'ARM\x64', 'program has no arm64 header'
    assert len(prog) <= CHAIN_OFFSET, 'program too large'
    payload_off = CHAIN_OFFSET + len(pad(image, 16)) if payload else 0
    total = max(CHAIN_OFFSET + linux_image_size, payload_off + len(payload))
    struct.pack_into('<Q', prog, 0x10, (total + 0xfffff) & ~0xfffff)
    struct.pack_into('<Q', prog, 0x28, len(image))
    struct.pack_into('<Q', prog, 0x30, payload_off)
    blob = bytes(prog) + b'\0' * (CHAIN_OFFSET - len(prog)) + image
    if payload:
        blob = pad(blob, 16) + payload
    new_kernel = gzip.compress(blob, 9, mtime=0)

    hdr = bytearray(d[:page])
    struct.pack_into('<I', hdr, 8, len(new_kernel))
    struct.pack_into('<I', hdr, 36, page)
    sha = hashlib.sha1()
    for part in (new_kernel, ramdisk, second, rdo, dtb):
        sha.update(part)
        sha.update(struct.pack('<I', len(part)))
    hdr[576:608] = pad(sha.digest(), 32)

    img = bytes(hdr) + pad(new_kernel, page) + pad(ramdisk, page) + pad(second, page) + pad(rdo, page) + pad(dtb, page)
    if len(img) > BOOT_PARTITION:
        raise SystemExit(f'{out}: {len(img)} bytes, more than the {BOOT_PARTITION} of the boot partition '
                         f'({len(img) - BOOT_PARTITION} too many): the payload has to shrink')
    open(out, 'wb').write(img)
    print(f'{out}: {len(img)} bytes (program {len(prog)}, Image {len(image)}, payload {len(payload)}, kernel gz {len(new_kernel)})')


if __name__ == '__main__':
    main()
