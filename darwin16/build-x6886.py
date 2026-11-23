#!/usr/bin/env python3
"""Build the X6886 Darwin16 boot image: LK -> shim -> XNU (xnu-8796)."""
import gzip, os, struct, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
SHIM = os.path.join(HERE, "shim")
OUT = os.path.join(SHIM, "out")
TOOLS = os.path.join(HERE, "tools")
LLVM = "/home/berucrypt00/x6886-hakios/tools/root/usr/lib/llvm-20/bin"
CLANG = os.path.join(LLVM, "clang")
LD = os.path.join(LLVM, "ld.lld")
OBJCOPY = os.path.join(LLVM, "llvm-objcopy")
BOOT_PARTITION = 32 << 20

CFLAGS = [
    "-target", "aarch64-freestanding-none", "-mcpu=cortex-a55", "-O2",
    "-ffreestanding", "-nostdlib", "-fno-pic", "-fno-pie",
    "-mgeneral-regs-only", "-mstrict-align", "-fno-stack-protector",
    "-Wall", "-Wextra", "-I" + SHIM,
]
SOURCES = ["start.S", "main.c", "plog.c", "hw.c", "display.c", "loader.c",
           "fdt.c", "splash.c", "logview.c"]

def run(cmd):
    print(" ".join(cmd))
    subprocess.check_call(cmd)

def abs64(path):
    # crude: scan relocations via llvm-readelf
    out = subprocess.run([os.path.join(LLVM, "llvm-readelf"), "-r", path],
                         capture_output=True, text=True).stdout
    return sum(1 for line in out.splitlines() if "R_AARCH64_ABS64" in line)

def main():
    payload = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "payload.bin")
    os.makedirs(OUT, exist_ok=True)
    objects = []
    for src in SOURCES:
        obj = os.path.join(OUT, src.replace(".", "_") + ".o")
        run([CLANG] + CFLAGS + ["-c", os.path.join(SHIM, src), "-o", obj])
        objects.append(obj)
    # reject absolute relocations in C objects (position independence)
    for obj in objects:
        if obj.endswith("start_S.o"):
            continue
        n = abs64(obj)
        if n:
            sys.exit(f"error: {n} absolute relocations in {obj}")

    elf = os.path.join(OUT, "shim.elf")
    run([LD, "-T", os.path.join(SHIM, "link.ld"), "--build-id=none", "-static"]
        + objects + ["-o", elf])
    image = os.path.join(OUT, "Image")
    run([OBJCOPY, "-O", "binary", elf, image])

    data = open(image, "rb").read()
    image_size, = struct.unpack_from("<Q", data, 16)
    if data[56:60] != b"ARM\x64":
        sys.exit("error: bad Image header magic")
    if image_size < len(data):
        sys.exit(f"error: image_size {image_size:#x} < data {len(data)}")
    data += b"\0" * (image_size - len(data))

    payload_data = open(payload, "rb").read()
    if payload_data[:8] != b"SWEETXNU":
        sys.exit("error: payload is not SWEETXNU")

    # X6886 has no usable initrd (vendor_boot holds Android's, which we must not touch).
    # Append the payload to the decompressed kernel blob, right after _end (image_size).
    # The shim scans from _end with an ADR, so it finds it without any initrd.
    data += b"\0" * ((-len(data)) % 16)
    payload_off = len(data)
    blob = bytearray(data + payload_data)

    # Update the Image header's image_size to cover the whole blob so LK copies it all.
    struct.pack_into("<Q", blob, 0x10, len(blob))
    blob = bytes(blob)

    kernel = gzip.compress(blob, 9, mtime=0)
    ramdisk = b""  # empty: no initrd needed
    boot = boot_img_v4(kernel, ramdisk)
    if len(boot) > BOOT_PARTITION:
        sys.exit(f"error: boot image {len(boot)} bytes > boot_a")
    out = os.path.join(HERE, "boot-x6886-darwin16.img")
    open(out, "wb").write(boot)
    print(f"Image {len(data)} bytes, payload at +{payload_off:#x} ({len(payload_data)}), "
          f"kernel gz {len(kernel)}, boot img {len(boot)} bytes -> {out}")

def os_version(major, minor, patch, year, month):
    return (major << 14 | minor << 7 | patch) << 11 | (year - 2000) << 4 | month

def boot_img_v4(kernel, ramdisk, cmdline=b"console=null"):
    if isinstance(cmdline, str):
        cmdline = cmdline.encode()
    page = 4096
    def p(x):
        n = len(x) if isinstance(x, (bytes, bytearray)) else x
        return x + b"\0" * ((-n) % page) if isinstance(x, (bytes, bytearray)) else (n + page - 1) // page * page
    # v4 header layout: magic, kernel_sz, ramdisk_sz, os_version, header_size,
    # 4 reserved u32, header_version, cmdline[1536], signature_size
    header = struct.pack(
        "<8sIIII4II1536sI",
        b"ANDROID!",
        len(kernel),
        len(ramdisk),
        os_version(16, 0, 0, 2025, 12),
        1584,               # header_size of v4
        0, 0, 0, 0,         # reserved
        4,                  # header_version
        cmdline.ljust(1536, b"\0"),
        0,                  # signature_size
    )
    assert len(header) == 1584, len(header)
    return p(header) + p(kernel) + p(ramdisk)

if __name__ == "__main__":
    main()
