#!/bin/sh
# Preferred repack path: magiskboot owns the v4 header, chainboot owns the blob.
# Usage: repack-magiskboot.sh <stock.img> <loader.bin> <payload.bin> <out.img>
set -eu

STOCK=${1:?stock boot.img}
LOADER=${2:?mtkloader.bin}
PAYLOAD=${3:?XNUP payload.bin}
OUT=${4:?output boot.img}
CHAIN=/home/berucrypt00/hakios-x6886/build/chainboot-x6886.py
MB=/home/berucrypt00/x6886-hakios/tools/bin/magiskboot
WORK=$(mktemp -d /tmp/opencode/repack-mb.XXXXXX)
trap 'rm -rf "$WORK"' EXIT

# 1) Build the chain blob (loader + Android Image + XNUP) with the Python packer.
python3 "$CHAIN" "$STOCK" "$LOADER" "$WORK/chain.img" "$PAYLOAD" >/dev/null

# 2) Let magiskboot rebuild the v4 header from the stock image.  'unpack -n'
#    keeps the gzip kernel compressed; 'repack' detects it is already
#    compressed and does not recompress it.
cd "$WORK"
"$MB" unpack -n "$WORK/chain.img" >/dev/null
"$MB" repack "$STOCK" "$WORK/magisk.img" >/dev/null

# 3) Drop the stale vbmeta + AVBf that magiskboot copies from stock.  vbmeta is
#    disabled on X6886 and the old hash descriptors no longer match our kernel.
#    The footer's big-endian original_image_size is the clean end of the image.
python3 - "$WORK/magisk.img" "$OUT" <<'PY'
import hashlib
import struct
import sys

src, out = sys.argv[1:3]
d = open(src, 'rb').read()
f = d[-64:]
if f[:4] != b'AVBf':
    raise SystemExit('expected AVBf footer from magiskboot repack')
orig = struct.unpack_from('>Q', f, 12)[0]
vbmeta_off, vbmeta_size = struct.unpack_from('>QQ', f, 20)
clean = d[:orig]
part = 64 << 20
if len(clean) > part:
    raise SystemExit(f'clean image {len(clean)} exceeds {part}')
clean += b'\0' * (part - len(clean))
if b'AVBf' in clean or b'AVB0' in clean:
    raise SystemExit('stale AVB metadata survived the strip')
open(out, 'wb').write(clean)
print(f'{out}: {len(clean)} bytes, sha256 {hashlib.sha256(clean).hexdigest()} '
      f'(stripped vbmeta_off=0x{vbmeta_off:x} size=0x{vbmeta_size:x})')
PY
