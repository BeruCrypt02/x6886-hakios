#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/env.sh"
REPO=$ROOT/refs/hakios-common
IOS=$ROOT/ios27

export PATH=/home/berucrypt00/x6886-mamba-build/bin:$ROOT/tools/bin:$ROOT/tools/root/usr/bin:$ROOT/tools/root/usr/lib/llvm-20/bin:/home/berucrypt00/Desktop/platform-tools-latest-linux/platform-tools:$PATH
export LD_LIBRARY_PATH=/home/berucrypt00/x6886-mamba-build/lib:$ROOT/tools/root/usr/lib/x86_64-linux-gnu
export LIBRARY_PATH=/home/berucrypt00/x6886-mamba-build/lib:$ROOT/tools/root/usr/lib/x86_64-linux-gnu
export C_INCLUDE_PATH=$ROOT/tools/root/usr/include:$ROOT/tools/root/usr/include/x86_64-linux-gnu
export XNU_SRC=$ROOT/refs/xnu-12377-hakios
export XNUDEPS=$ROOT/refs/xnu-12377-hakios-deps
export XNU_CLANG_DIR=$ROOT/tools/root/usr/lib/llvm-20/bin
export DARWIN_SDK=$ROOT/refs/theos-sdks/iPhoneOS16.5.sdk
export IOS=$IOS
export OBJ=$IOS/obj
export PACSTRIP=$ROOT/tools/bin/pacstrip
export LDID=$ROOT/tools/bin/ldid

cd "$REPO"
mkdir -p "$OBJ"

mkdir -p "$IOS/dyld-strip"
if [ ! -f "$IOS/.pacstrip.done" ]; then
  files=()
  for f in "$IOS"/dyld/dyld_shared_cache_arm64e*; do
    case "$f" in *.symbols|*.atlas) continue ;; esac
    files+=("$f")
  done
  "$ROOT/tools/bin/pacstrip" cache "$IOS/dyld-strip" "${files[@]}"
  touch "$IOS/.pacstrip.done"
fi

if [ ! -f "$IOS/.fips.done" ]; then
  python3 darwin/tools/fips26.py "$IOS/dyld-strip" \
    "AE2C707EB9149A03BA76C9B332CA5C2FBF758191271C221E28EACA40D8065C18" \
    "F0BBB5196E13FC74FEF80014965B915B0882AD557535BCAA07E34BA4959771D1"
  touch "$IOS/.fips.done"
fi

bash darwin/userland/inj26.sh
bash darwin/userland/root26-darwin.sh

bash "$ROOT/tools/fix-ramdisk.sh"

LAUNCHDAEMONS="$IOS/jobs26" WORK="$IOS/image-tree-prune" \
  python3 darwin/tools/mkimage26.py --prune "$IOS/system27-prune.apfs"

ls -lh "$IOS/root26-darwin.apfs" "$IOS/system27-prune.apfs"
