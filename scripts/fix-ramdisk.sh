#!/usr/bin/env bash
# Rebuild stage0 and init as static arm64 programs (no LC_LOAD_DYLINKER), then repack
# flashboot.apfs and root26-darwin.apfs. The upstream root26-darwin.sh links both with
# /usr/lib/dyld, which is absent from the RAM disk: load_machfile returns LOAD_ENOENT
# and initproc dies before launchd starts. ld64.lld -no_adhoc_codesign -no_fixup_chains
# makes them static; place-kernel.py --program places the segments; ldid -Cadhoc marks
# init as a platform binary so launchd's job runner may exec it.
set -euo pipefail

ROOT=/home/berucrypt00/x6886-hakios
REPO=$ROOT/refs/hakios-common
IOS=$ROOT/ios27
L=$ROOT/tools/root/usr/lib/llvm-20/bin
W=${WORK:-/tmp/hakios-static}

export PATH=$ROOT/tools/bin:$ROOT/tools/root/usr/bin:$L:$PATH

rm -rf "$W"
mkdir -p "$W"

for c in "$REPO/lib/libmini/mini.c" "$REPO/lib/libmini/start.S"; do
  IPHONEOS_DEPLOYMENT_TARGET=16.5 "$L/clang" -target arm64-apple-darwin -O2 -ffreestanding \
    -fno-builtin -fno-stack-protector -I"$REPO/lib/libmini" -c "$c" -o "$W/$(basename "$c").o"
done

for prog in stage0 init; do
  IPHONEOS_DEPLOYMENT_TARGET=16.5 "$L/clang" -target arm64-apple-darwin -O2 -ffreestanding \
    -fno-builtin -fno-stack-protector -Wall -Wextra -I"$REPO/lib/libmini" \
    -c "$REPO/init/$prog.c" -o "$W/$prog.o"
  "$L/ld64.lld" -arch arm64 -platform_version 2 16.5 16.5 -e _start -no_adhoc_codesign \
    -no_fixup_chains -pagezero_size 0x100000000 -o "$W/$prog.lld" \
    "$W/$prog.o" "$W/mini.c.o" "$W/start.S.o"
  python3 "$REPO/kernel/host/llvm/place-kernel.py" --program "$W/$prog.lld" "$W/$prog" >/dev/null
done

"$ROOT/tools/bin/ldid" -Cadhoc -S "$W/init"

rm -rf "$W/flashboot-tree" "$W/root26-tree"
mkdir -p "$W/flashboot-tree/sbin" "$W/root26-tree"
7z x -o"$W/flashboot-tree" "$IOS/flashboot.apfs" -y >/dev/null
7z x -o"$W/root26-tree" "$IOS/root26-darwin.apfs" -y >/dev/null
cp "$W/stage0" "$W/flashboot-tree/sbin/launchd"
chmod 755 "$W/flashboot-tree/sbin/launchd"
cp "$W/init" "$W/root26-tree/sbin/init"
chmod 755 "$W/root26-tree/sbin/init"

python3 "$REPO/tools/apfs/mkapfs.py" -o "$IOS/flashboot.apfs" Boot="$W/flashboot-tree" | tail -1
python3 "$REPO/tools/apfs/mkapfs.py" -o "$IOS/root26-darwin.apfs" hakios="$W/root26-tree" | tail -1
