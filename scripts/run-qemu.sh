#!/usr/bin/env bash
set -euo pipefail

ROOT=/home/berucrypt00/x6886-hakios
UPSTREAM=$ROOT/refs/hakios-common
KERNEL=$ROOT/refs/xnu-12377-hakios/BUILD/obj/mach.development.bcm2837
SHELL_ROOT=$ROOT/build/qemu/root.img
IOS=$ROOT/ios27

export PATH=/home/berucrypt00/x6886-mamba-build/bin:$ROOT/tools/bin:$ROOT/tools/root/usr/bin:$ROOT/tools/root/usr/lib/llvm-20/bin:/home/berucrypt00/Desktop/platform-tools-latest-linux/platform-tools:$PATH
export LD_LIBRARY_PATH=/home/berucrypt00/x6886-mamba-build/lib:$ROOT/tools/root/usr/lib/x86_64-linux-gnu
export LIBRARY_PATH=/home/berucrypt00/x6886-mamba-build/lib:$ROOT/tools/root/usr/lib/x86_64-linux-gnu
export C_INCLUDE_PATH=$ROOT/tools/root/usr/include:$ROOT/tools/root/usr/include/x86_64-linux-gnu
export XNU_SRC=$ROOT/refs/xnu-12377-hakios
export XNU_CLANG_DIR=$ROOT/tools/root/usr/lib/llvm-20/bin
export DARWIN_SDK=$ROOT/refs/theos-sdks/iPhoneOS16.5.sdk
export QEMU_FIRMWARE=$ROOT/tools/qemu/QEMU_CODE.fd
export QEMU_ACCEL=${QEMU_ACCEL:-tcg,tb-size=128}
export IOS=$IOS

MODE=${1:-shell}
shift || true

case "$MODE" in
  shell)
    export QEMU_MEMORY=${MEM_MB:-4096M}
    exec "$UPSTREAM/tools/qemu-kernel.sh" \
      --window --interactive --cpus 4 \
      --ramdisk "$SHELL_ROOT" \
      --boot-args 'rd=md0' \
      "$@" "$KERNEL"
    ;;
  ios)
    echo "iOS mode needs a graphical desktop and about 4096M of RAM." >&2
    echo "Close the browser and free RAM before continuing." >&2
    if [ "${1:-}" != "--yes" ]; then
      echo "Run with: $0 ios --yes" >&2
      exit 1
    fi
    shift
    export QEMU_MEMORY=${MEM_MB:-4096M}
    avail=$(free -m | awk '/^Mem:/{print $7}')
    sw=$(free -m | awk '/^Swap:/{print $4}')
    total=$((avail + sw))
    if [ "$total" -lt 6000 ]; then
      echo "need about 6G of RAM+swap, have ${total}M" >&2
      exit 1
    fi
    : "${SYSTEM_APFS:=$IOS/system27-prune.apfs}"
    : "${ROOT_APFS:=$IOS/root26-darwin.apfs}"
    : "${IDENTITY:=$IOS/DeviceTree.bin}"
    exec "$UPSTREAM/tools/qemu-kernel.sh" \
      --window --interactive --cpus 4 \
      --ramdisk "$ROOT_APFS" --disk "$SYSTEM_APFS" --identity "$IDENTITY" \
      --boot-args "${IOS_BOOT_ARGS:-rd=md0 gpu=0}" \
      "$@" "$KERNEL"
    ;;
  *)
    echo "usage: $0 shell|ios [--yes]" >&2
    exit 1
    ;;
esac
