#!/usr/bin/env python3
"""Rebuild boot image with the STOCK header used verbatim.

My earlier boot_img_v4() had a wrong struct layout: it put ramdisk_size at
offset 12 (kernel_addr) and os_version at 16, etc. LK read a garbage
kernel_addr (0x20000c9c) and refused the image -> OrangeState glitches.

Fix: take the stock 4096-byte header byte-for-byte, change ONLY kernel_size
and the 5-part SHA1. ramdisk stays empty (size 0, as stock). Kernel blob is
our decompressed shim+payload, gzip'd, placed at 4096. Pad to 64MB and copy
the stock AVBf footer to 0x3ffffc0.
"""
import gzip, hashlib, struct, sys

STOCK = "/home/berucrypt00/otadump/images/boot.img"
PAYLOAD = "payload.bin"
OUT = "boot-x6886-final.img"
BOOT_PARTITION = 64 << 20
PAGE = 4096

def main():
    stock = bytearray(open(STOCK, "rb").read())
    payload = open(PAYLOAD, "rb").read()
    shim_image = open("shim/out/Image", "rb").read()

    # Build the decompressed kernel blob: shim Image (padded to image_size)
    # followed by the SWEETXNU payload at _end.
    image_size = struct.unpack_from("<Q", shim_image, 0x10)[0]
    blob = bytearray(shim_image)
    blob += b"\0" * (image_size - len(blob))
    blob += b"\0" * ((-len(blob)) % 16)
    payload_off = len(blob)
    blob += payload
    struct.pack_into("<Q", blob, 0x10, len(blob))  # image_size covers all
    blob = bytes(blob)

    kernel = gzip.compress(blob, 9, mtime=0)

    # Header = stock first page verbatim; only kernel_size changes.
    hdr = bytearray(stock[:PAGE])
    struct.pack_into("<I", hdr, 8, len(kernel))   # kernel_size
    # ramdisk_size already 0 in stock; leave it.

    # Stock has zeros at 576:608: this LK does not use the SHA1 field. Leave it
    # byte-for-byte identical to stock.

    # Stock header is 1584 bytes (0x630), but kernel starts at page 4096
    # (0x1000): there is 0x9d0 of padding after the header. Match that.
    img = bytes(hdr) + b"\0" * (PAGE - len(hdr)) + kernel
    img += b"\0" * ((-len(img)) % PAGE)

    full = bytearray(img)
    full += b"\0" * (BOOT_PARTITION - len(full))
    full[0x3ffffc0:0x4000000] = stock[0x3ffffc0:0x4000000]  # AVBf footer
    open(OUT, "wb").write(full)
    print(f"kernel {len(kernel)} (blob {len(blob)}, payload at +{payload_off:#x}), "
          f"image {len(img)}, full {len(full)} -> {OUT}")

if __name__ == "__main__":
    main()
