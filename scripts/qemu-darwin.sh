#!/usr/bin/env bash
set -euo pipefail

ROOT=/home/berucrypt00/x6886-hakios
REPO=$ROOT/refs/hakios-common
IOS=$ROOT/ios27
KERNEL=$ROOT/refs/xnu-12377-hakios/BUILD/obj/mach.development.bcm2837

export PATH=$ROOT/tools/bin:$ROOT/tools/root/usr/bin:$ROOT/tools/root/usr/lib/llvm-20/bin:/home/berucrypt00/Desktop/platform-tools-latest-linux/platform-tools:$PATH

FLASH=$IOS/flashboot.apfs
SMALL=$IOS/root26-darwin.apfs
SYSTEM=$IOS/system27-prune.apfs
IDENTITY=$IOS/DeviceTree.bin
FIRMWARE=$ROOT/tools/qemu/QEMU_CODE.fd
VARS=$ROOT/tools/qemu/QEMU_VARS.fd
WORK=${WORK:-/tmp/qemu-darwin-run}

mkdir -p "$WORK/esp/EFI/BOOT"
cp "$REPO/arch/arm64/boot/loader.efi" "$WORK/esp/EFI/BOOT/BOOTAA64.EFI"

python3 "$REPO/arch/arm64/boot/mkidentity.py" --blob "$WORK/identity.bin" "$IDENTITY"

CMD='debug=0x8 serial=3 -v cpus=4 virtio_net=1 virtio_input=1 virtio_blk=1 rd=md0 root_apfs=rvblk0 gpu=0 launchd_unsecure_cache=1'

python3 "$REPO/arch/arm64/boot/mkpayload.py" -o "$WORK/payload.bin" \
  kernel="$KERNEL" \
  ramdisk="$SMALL" \
  identity="$WORK/identity.bin" \
  --cmdline="$CMD" \
  --rdres=$(printf "%x" $(stat -c%s "$FLASH"))

cp "$VARS" "$WORK/vars.fd"

exec qemu-system-aarch64 \
  -M virt,gic-version=3 \
  -cpu cortex-a76 \
  -smp 4 \
  -m 8192M \
  -accel tcg,tb-size=128 \
  -drive if=pflash,format=raw,readonly=on,file="$FIRMWARE" \
  -drive if=pflash,format=raw,file="$WORK/vars.fd" \
  -drive file=fat:rw:"$WORK/esp",format=raw,if=virtio \
  -device virtio-rng-pci \
  -device ramfb \
  -device "loader,file=$FLASH,addr=0xFC000000,force-raw=on" \
  -device "loader,file=$WORK/payload.bin,addr=0xF0000000,force-raw=on" \
  -drive "file=$SYSTEM,format=raw,if=none,id=sysdisk,readonly=on" \
  -device virtio-blk-device,drive=sysdisk \
  -global virtio-mmio.force-legacy=false \
  -netdev user,id=net0 \
  -device virtio-net-device,netdev=net0 \
  -device virtio-keyboard-device \
  -device virtio-tablet-device \
  -display gtk \
  -serial stdio \
  -monitor unix:"$WORK/monitor",server,nowait \
  -no-reboot
