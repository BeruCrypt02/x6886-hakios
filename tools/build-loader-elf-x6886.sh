#!/bin/bash
# Recreate the loader ELF (symbols, GOT, BSS) that verifier checks 5-12 read.
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/env.sh"
CLANG=$ROOT/tools/root/usr/lib/llvm-20/bin/clang
LLD=$ROOT/tools/root/usr/lib/llvm-20/bin/ld.lld
SRC=$ROOT/refs/oosexgang-xnu27/loader/link.ld
OUT=/tmp/hakios-x6886/elf-final2
mkdir -p "$OUT"

# build.sh fallback when no touch firmware is present (the same stub used for mtkloader.bin)
cat > "$OUT/touchfw.h" <<'EOF'
#ifndef TOUCHFW_H
#define TOUCHFW_H
#include <stdint.h>
#define TOUCH_FW_SIZE 0u
#define TOUCH_FW_FLAG 0x0000u
static const uint8_t touch_fw[1] = { 0 };
#endif
EOF

CFLAGS=(--target=aarch64-linux-gnu -ffreestanding -nostdlib -fno-builtin -mgeneral-regs-only -mstrict-align -Os -Wall -Wextra
	-I"$OUT" -I"$ROOT/build/boards/x6886" -I"$ROOT/xnuloader" -I"$ROOT/refs/oosexgang-xnu27/loader")

"$CLANG" "${CFLAGS[@]}" -c "$ROOT/xnuloader/start.S" -o "$OUT/start.o"
"$CLANG" "${CFLAGS[@]}" -c "$ROOT/xnuloader/mtkloader.c" -o "$OUT/mtkloader.o"
"$LLD" --defsym=KERNEL_LOAD=0x40010000 -T "$SRC" "$OUT/start.o" "$OUT/mtkloader.o" -o "$OUT/mtkloader.elf"
echo "ELF: $OUT/mtkloader.elf"
