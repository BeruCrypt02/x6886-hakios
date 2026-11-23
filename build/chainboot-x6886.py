#!/usr/bin/env python3
"""Place a bare-metal program in front of Android's kernel in a boot image.

Header layout (AOSP boot_img_hdr_v4, system/tools/mkbootimg/include/bootimg/bootimg.h):

  0x00 magic[8]            0x08 kernel_size       0x0c ramdisk_size
  0x10 os_version          0x14 header_size       0x18 reserved[4]
  0x28 header_version      0x2c cmdline[1536]     0x62c signature_size
  0x630 id[32]             0x650 header_salt[32]  0x670 extra_size
  0x674 root_salt[32]      0x694 reserved2[16]    0x6a4 boot_salt[32]
  0x6c4 recovery_salt[32]

Notes for v4 (verified against magiskboot unpack/repack on the X6886 dump):
  * There is no page_size field in v2+.  Offset 0x24 is reserved[3] and must be
    left untouched.  The page size is a device constant (4096 here).
  * There is no SHA1/id-hash field to update.  magiskboot repack changes only
    kernel_size at 0x08; writing a 5-part hash at 0x240 corrupts cmdline.
  * The old 1632/1648/1652/1668 reads were v2-era guesses; v4 has only
    extra_size at 0x670 among those.  We assert it is zero for this dump.
  * The stock AVBf footer at the end of the partition is NOT copied: the new
    kernel overwrites the embedded vbmeta anyway, so the output carries no
    stale footer pointing at zeroed/garbage vbmeta.  vbmeta is disabled on
    this device.
"""
import gzip
import struct
import sys

CHAIN_OFFSET = 0x40000
BOOT_PARTITION = 64 << 20          # X6886 boot_a / boot_b are 64 MiB, not 32
PAGE_SIZE = 4096                   # device constant; not stored in a v4 header
V4_HEADER_VERSION = 4
V4_EXTRA_SIZE_OFF = 0x670          # 1648, AOSP boot_img_hdr_v4.extra_size


def pad(b, n):
    return b + b'\0' * (-len(b) % n)


def u32(d, off):
    return struct.unpack_from('<I', d, off)[0]


def main():
    if len(sys.argv) not in (4, 5):
        raise SystemExit('usage: chainboot-x6886.py <stock.img> <loader.bin> <out.img> [payload.bin]')
    src, prog_path, out = sys.argv[1:4]
    payload = open(sys.argv[4], 'rb').read() if len(sys.argv) == 5 else b''

    d = open(src, 'rb').read()
    if d[:8] != b'ANDROID!':
        raise SystemExit('not an Android boot image')

    ksize = u32(d, 0x08)
    rsize = u32(d, 0x0c)
    hsize = u32(d, 0x14)
    hver = u32(d, 0x28)
    if hver != V4_HEADER_VERSION:
        raise SystemExit(f'header v{hver}, expected v{V4_HEADER_VERSION}')
    if hsize > PAGE_SIZE:
        raise SystemExit(f'header_size {hsize} exceeds page {PAGE_SIZE}')
    extra_size = u32(d, V4_EXTRA_SIZE_OFF)
    if extra_size:
        raise SystemExit(f'stock image has {extra_size} bytes of v4 extra data; unsupported')

    off = PAGE_SIZE
    kernel = d[off:off + ksize]
    off += (-ksize) % PAGE_SIZE + ksize
    ramdisk = d[off:off + rsize]
    if len(kernel) != ksize or len(ramdisk) != rsize:
        raise SystemExit('truncated boot image')

    if kernel[:2] == b'\x1f\x8b':
        image = gzip.decompress(kernel)
    elif kernel[:4] == b'\x02\x21\x4c\x18':
        import os
        import subprocess
        import tempfile
        fd, tmp = tempfile.mkstemp(dir='/tmp/opencode')
        os.close(fd)
        try:
            with open(tmp, 'wb') as f:
                subprocess.run(['lz4', '-dc', '-'], input=kernel, stdout=f, check=True)
            image = open(tmp, 'rb').read()
        finally:
            os.unlink(tmp)
    else:
        image = kernel
    if image[0x38:0x3c] != b'ARM\x64':
        raise SystemExit('Android kernel is not an arm64 Image')
    linux_image_size = u32(image, 0x10)

    prog = bytearray(open(prog_path, 'rb').read())
    if prog[0x38:0x3c] != b'ARM\x64':
        raise SystemExit('program has no arm64 header')
    if len(prog) > CHAIN_OFFSET:
        raise SystemExit('program too large')

    payload_off = CHAIN_OFFSET + len(pad(image, 16)) if payload else 0
    total = max(CHAIN_OFFSET + linux_image_size, payload_off + len(payload))
    struct.pack_into('<Q', prog, 0x10, (total + 0xfffff) & ~0xfffff)
    struct.pack_into('<Q', prog, 0x28, len(image))
    struct.pack_into('<Q', prog, 0x30, payload_off)
    blob = bytes(prog) + b'\0' * (CHAIN_OFFSET - len(prog)) + image
    if payload:
        blob = pad(blob, 16) + payload
    new_kernel = gzip.compress(blob, 9, mtime=0)

    # The stock header is kept byte-for-byte: cmdline, id, salts and reserved
    # words are all preserved.  kernel_size is the only field we update.
    hdr = bytearray(d[:PAGE_SIZE])
    struct.pack_into('<I', hdr, 0x08, len(new_kernel))

    img = bytes(hdr) + pad(new_kernel, PAGE_SIZE) + pad(ramdisk, PAGE_SIZE)
    if len(img) > BOOT_PARTITION:
        raise SystemExit(f'{out}: {len(img)} bytes, more than the {BOOT_PARTITION} boot partition '
                         f'({len(img) - BOOT_PARTITION} too many): the payload has to shrink')
    img = pad(img, BOOT_PARTITION)
    if b'AVBf' in img or b'AVB0' in img:
        raise SystemExit('stale AVB metadata in output; refusing to write')
    open(out, 'wb').write(img)
    print(f'{out}: {len(img)} bytes (loader {len(prog)}, Image {len(image)}, payload {len(payload)}, '
          f'kernel gz {len(new_kernel)}, no AVB footer)')


if __name__ == '__main__':
    main()
