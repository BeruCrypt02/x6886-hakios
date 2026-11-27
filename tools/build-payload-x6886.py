#!/usr/bin/env python3
"""Build the XNUP payload (kernel + ramdisk) for the X6886 boot image."""
import hashlib
import struct
import sys
from pathlib import Path

ROOT = str(Path(__file__).resolve().parent.parent)
kernel_path = ROOT + "/refs/xnu-12377-hakios/BUILD/obj/DEVELOPMENT_ARM64_BCM2837/mach.development.bcm2837"
ramdisk_path = ROOT + "/ios27/flashboot.apfs"
ramdisk1_path = ROOT + "/ios27/root26-darwin.apfs"
out_path = sys.argv[1] if len(sys.argv) > 1 else ROOT + "/build/payload-x6886.bin"


def add(body, name, data):
    return body + struct.pack("<16sII", name.encode(), len(data), 0) + data + b"\0" * (-len(data) % 16)


kernel = open(kernel_path, "rb").read()
ramdisk = open(ramdisk_path, "rb").read()
ramdisk1 = open(ramdisk1_path, "rb").read()
body = add(b"", "kernel", kernel)
body = add(body, "ramdisk", ramdisk)
body = add(body, "ramdisk1", ramdisk1)
body += b"\0" * 24
payload = struct.pack("<II", 0x50554E58, len(body)) + body
open(out_path, "wb").write(payload)
print(f"payload: {out_path}")
print(f"size: {len(payload)}")
print(f"kernel sha256: {hashlib.sha256(kernel).hexdigest()}")
print(f"ramdisk sha256: {hashlib.sha256(ramdisk).hexdigest()}")
print(f"ramdisk1 sha256: {hashlib.sha256(ramdisk1).hexdigest()}")
