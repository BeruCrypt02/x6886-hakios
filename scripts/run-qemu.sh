#!/usr/bin/env bash
set -euo pipefail

ROOT=/home/berucrypt00/hakios-x6886
UPSTREAM=$ROOT/refs/hakios-common
KERNEL=$ROOT/refs/xnu-12377-hakios/BUILD/obj/mach.development.bcm2837
SHELL_ROOT=$ROOT/build/qemu/root.img

export PATH=/home/berucrypt00/x6886-mamba-build/bin:$ROOT/tools/bin:$ROOT/tools/root/usr/bin:/home/berucrypt00/Desktop/platform-tools-latest-linux/platform-tools:$PATH
export QEMU_EFI=$ROOT/tools/root/usr/share/qemu-efi-aarch64/QEMU_EFI.fd
export XNU_CLANG_DIR=$ROOT/tools/root/usr/lib/llvm-20/bin
export QEMU_MEMORY=${MEM_MB:-1024M}
export QEMU_ACCEL=${QEMU_ACCEL:-tcg,tb-size=128}
export QEMU_FIRMWARE=$QEMU_EFI

MODE=${1:-shell}
shift || true

case "$MODE" in
  shell)
    [ -f "$SHELL_ROOT" ] || { echo "missing $SHELL_ROOT" >&2; exit 1; }
    exec "$UPSTREAM/tools/qemu-kernel.sh" \
      --window --interactive --cpus 4 \
      --ramdisk "$SHELL_ROOT" \
      --boot-args 'rd=md0' \
      "$@" "$KERNEL"
    ;;
  ios)
    echo "iOS mode needs a graphical desktop and about 2048M of RAM." >&2
    echo "Close the browser and free RAM before continuing." >&2
    if [ "${1:-}" != "--yes" ]; then
      echo "Run with: $0 ios --yes" >&2
      exit 1
    fi
    shift
    : "${SYSTEM_APFS:?set SYSTEM_APFS to the system.apfs path}"
    : "${ROOT_APFS:?set ROOT_APFS to the root26-darwin.apfs path}"
    : "${IDENTITY:?set IDENTITY to DeviceTree.bin from the IPSW}"
    avail=$(free -m | awk '/^Mem:/{print $7}')
    sw=$(free -m | awk '/^Swap:/{print $4}')
    total=$((avail + sw))
    if [ "$total" -lt 6000 ]; then
      echo "need about 6G of RAM+swap, have ${total}M" >&2
      exit 1
    fi
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
