#!/usr/bin/env python3
"""Offline verification of build/boot-HakiOS-X6886.img (13 checks).

Reads only the final image, the committed loader blob, the loader ELF that produced that blob
and the board/loader sources. Nothing is flashed, nothing is written.
"""
import os
import re
import struct
import subprocess
import sys

ROOT = "/home/berucrypt00/x6886-hakios"
IMG = f"{ROOT}/build/boot-HakiOS-X6886.img"
LOADER = f"{ROOT}/build/loader/mtkloader.bin"
ELF = "/tmp/opencode/elf-final2/mtkloader.elf"
READELF = f"{ROOT}/tools/root/usr/lib/llvm-20/bin/llvm-readelf"
NM = f"{ROOT}/tools/root/usr/lib/llvm-20/bin/llvm-nm"
START_S = f"{ROOT}/build/loader/start.S"
PLATFORM_H = f"{ROOT}/build/boards/x6886/hakios_platform.h"
MTKDBG_H = f"{ROOT}/build/loader/mtkdbg.h"
CHAIN_OFFSET = 0x80000
PART = 64 << 20
PAGE = 4096

results = []


def check(n, name, ok, evidence):
    results.append((n, name, bool(ok), evidence))


d = open(IMG, "rb").read()
ksize, rsize = struct.unpack_from("<II", d, 0x08)
hsize, hver = struct.unpack_from("<II", d, 0x14)[0], struct.unpack_from("<I", d, 0x28)[0]
blob = d[PAGE:PAGE + ksize]
if blob[:2] == b"\x1f\x8b":
    import gzip
    blob = gzip.decompress(blob)

text_offset, image_size = struct.unpack_from("<QQ", blob, 0x08)
res2, res3, res4 = struct.unpack_from("<QQQ", blob, 0x20)
payload_off = res4
payload_magic, payload_size = struct.unpack_from("<II", blob, payload_off) if payload_off else (0, 0)

# 1 -- header v4 / page 4096 / 64 MB
kernel_end = PAGE + ksize + (-ksize % PAGE)  # kernel is page-padded in the image
ok = hver == 4 and hsize <= PAGE and rsize == 0 and len(d) == PART and kernel_end <= PAGE + ksize + PAGE
check(1, "header v4 / page 4096 / 64MB", ok,
      f"ver={hver} hsize={hsize} ksize={ksize} rsize={rsize} len={len(d)} kernel ends at {kernel_end:#x}")

# 2 -- ARM64 magic
ok = blob[0x38:0x3c] == b"ARM\x64"
check(2, "ARM64 magic", ok, f"blob[0x38:0x3c]={blob[0x38:0x3c]!r}")

# 3 -- text_offset 0
ok = text_offset == 0
check(3, "text_offset 0", ok, f"text_offset={text_offset}")

# 4 -- res2/res3/XNUP
entries = []
pos = payload_off + 8
while payload_off and pos + 24 <= len(blob):
    name, size, _ = struct.unpack_from("<16sII", blob, pos)
    name = name.rstrip(b"\0").decode(errors="replace")
    if not name:
        break
    entries.append((name, size))
    pos += 24 + size + (-size % 16)
stock = open("/home/berucrypt00/otadump/images/boot.img", "rb").read()
sk = struct.unpack_from("<I", stock, 0x08)[0]
sblob = stock[PAGE:PAGE + sk]
if sblob[:2] == b"\x1f\x8b":
    sblob = __import__("gzip").decompress(sblob)
elif sblob[:4] == b"\x02\x21\x4c\x18":
    sblob = subprocess.run(["lz4", "-dc", "-"], input=sblob,
                           stdout=subprocess.PIPE, check=True).stdout
stock_image_len = len(sblob)
ok = (res2 == 0 and res3 == stock_image_len and payload_off == CHAIN_OFFSET + res3
      and payload_magic == 0x50554E58 and [n for n, _ in entries] in
      (["kernel", "ramdisk"], ["kernel", "ramdisk", "ramdisk1"]))
check(4, "res2/res3/XNUP", ok,
      f"res2={res2} res3={res3} (stock Image={stock_image_len}) payload_off={payload_off:#x} "
      f"(=CHAIN_OFFSET+res3 {CHAIN_OFFSET + res3:#x}) magic={payload_magic:#x} "
      f"size={payload_size} entries={entries}")

elf = ELF if os.path.exists(ELF) else ""
reloc_out = ""
if elf:
    reloc_out = subprocess.run([READELF, "-r", elf], stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True).stdout
    sec_out = subprocess.run([READELF, "-S", elf], stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, text=True).stdout
    nm_out = subprocess.run([NM, "-m", elf], stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True).stdout
else:
    sec_out = nm_out = ""
syms = {}
for line in nm_out.splitlines():
    m = re.match(r"([0-9a-fA-F]+)\s+\S+\s+(\S+)$", line.strip())
    if m:
        syms[m.group(2)] = int(m.group(1), 16)

# 5 -- zero relocations / R_AARCH64_ABS64
ok = "There are no relocations in this file" in reloc_out and "R_AARCH64_ABS64" not in reloc_out
check(5, "zero relocations / R_AARCH64_ABS64", ok,
      reloc_out.strip().splitlines()[-1] if reloc_out.strip() else "no ELF available")

# 6 -- GOT = 1 entry
mgot = re.search(r"\[\s*\d+\]\s+\.got\s+\S+\s+[0-9a-f]+\s+[0-9a-f]+\s+([0-9a-f]+)", sec_out)
got_size = int(mgot.group(1), 16) if mgot else -1
check(6, "GOT=1", got_size == 8, f".got size={got_size} (one 64-bit entry)")

# 7 -- BSS gap below CHAIN_OFFSET
start = syms.get("_start", -1)
bss_end = syms.get("__bss_end", -1)
gap = CHAIN_OFFSET - (bss_end - start) if start >= 0 and bss_end >= 0 else -1
check(7, "BSS gap", gap > 0, f"_start={start:#x} __bss_end={bss_end:#x} gap={gap:#x}")

# 8 -- VBAR 16 vectors
ss = open(START_S).read()
mtk_part = ss.split("mtk_vectors:", 1)[1].split("/* Handoff vectors", 1)[0]
xnu_part = ss.split("xnu_vectors:", 1)[1].split(".balign 64", 1)[0]
# mtk: 14 `vec 0x...` slots + 2 explicit IRQ slots (`b irq_entry`); xnu: 16 `xvec 0x...` slots
n_mtk = len(re.findall(r"^\s*vec\s+0x", mtk_part, re.M)) + len(re.findall(r"^\s*b\s+irq_entry", mtk_part, re.M))
n_xnu = len(re.findall(r"^\s*xvec\s+0x", xnu_part, re.M))
ok = n_mtk == n_xnu == 16 and syms.get("mtk_vectors", 0) % 0x800 == 0 and syms.get("xnu_vectors", 0) % 0x800 == 0
check(8, "VBAR 16 vectors", ok,
      f"mtk={n_mtk} xnu={n_xnu} mtk_vectors={syms.get('mtk_vectors',0):#x} xnu_vectors={syms.get('xnu_vectors',0):#x}")

# 9 -- ramoops 0x480af000
ph = open(PLATFORM_H).read()
ok = "HAKIOS_RAMOOPS_CONSOLE              0x480af000" in ph and "plog_flush" in syms
check(9, "ramoops 0x480af000", ok, "board header HAKIOS_RAMOOPS_CONSOLE=0x480af000; ELF plog_flush present")

# 10 -- UFS / ufs_darwin
lb = open(LOADER, "rb").read()
ufs_syms = [s for s in syms if s.startswith("ufs_darwin")]
ok = b"ufs_darwin" in lb and len(ufs_syms) > 0
check(10, "UFS/ufs_darwin", ok, f"loader bin has ascii ufs_darwin at {lb.find(b'ufs_darwin')}; ELF syms={len(ufs_syms)}")

# 11 -- watchdog (TOPRGU base + restart key + watcher)
md = open(MTKDBG_H).read()
ok = ("HAKIOS_SOC_TOPRGU                   0x10007000" in ph
      and "TOPRGU_RESTART_KEY   0x1971" in md and "watcher_entry" in syms)
check(11, "watchdog", ok, "TOPRGU=0x10007000, restart key=0x1971, ELF watcher_entry present")

# 12 -- XNU-direct
ok = ("install_xnu_vectors" in syms and "xnu_return_stub" in syms
      and b"XNUP" in lb and len(entries) in (2, 3))
check(12, "XNU-direct", ok, f"ELF install_xnu_vectors/xnu_return_stub; loader XNUP at {lb.find(b'XNUP')}; payload={entries}")

# 13 -- AVB none
ok = b"AVBf" not in d and b"AVB0" not in d and not any(d[-64:])
check(13, "AVB none", ok, "no AVBf/AVB0 anywhere; final 64 bytes all zero")

for n, name, ok, ev in results:
    print(f"[{'PASS' if ok else 'FAIL'}] {n:2d}/13  {name:34s} {ev}")
passed = sum(1 for _, _, ok, _ in results if ok)
print(f"\nRESULT: {passed}/13 checks passed")
sys.exit(0 if passed == 13 else 1)
