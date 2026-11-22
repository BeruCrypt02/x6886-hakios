# Bring-up plan

## Gate 0: host toolchain

Done: local toolchain in `tools/`, LLVM 20, lld 20, QEMU 8.2, dtc, cross gcc, magiskboot, gh, adb, fastboot. Source `env.sh` first.

Host has 7.7 GiB RAM and about 2 GiB free. QEMU guest memory must stay at 1024M or 1536M with `QEMU_ACCEL='tcg,tb-size=128'`. The upstream default 6G will OOM this machine.

## Gate 1: Apple-free XNU in QEMU

Build: `cd refs/hakios-common && source ../../env.sh && TOOLCHAIN=llvm make kernel-check`.

Root image: `tools/qemu-root.sh build/qemu/root.img` from the same checkout. Done, 4 MiB APFS.

Boot check after the kernel exists:

```sh
QEMU_MEMORY=1024M QEMU_ACCEL='tcg,tb-size=128' tools/qemu-kernel.sh \
  --ramdisk /home/berucrypt00/Documents/Default Project/X6886-HakiOS/build/qemu/root.img \
  --boot-args rd=md0 --cpus 2 \
  --until 'a shell on the console' \
  --type 'help\nps\n' \
  /home/berucrypt00/Documents/Default Project/X6886-HakiOS/refs/xnu-12377-hakios/BUILD/obj/mach.development.bcm2837
```

## Gate 2: Darwin userland in QEMU

Needs an IPSW from Apple. This is the first item that requires the user. A Darwin 27 / iOS 27.2 IPSW for iPhone12,1 matches the upstream notes. Without it, only the open kernel and its small root can run.

## Gate 3: X6886 board package

Write from `board/x6886-base.dts` and `board/x6886-overlays.dts`. HakiOS upstream board format is Qualcomm-oriented; expect to adapt the generator or write the DTS by hand from the extracted nodes.

Start with `features = <>;` and one core. First log line to look for: `board: x6886 ...`.

## Gate 4: slot B device test

Read-only checks first: `fastboot getvar all`, `fastboot oem device-info`, `ls -l /dev/block/by-name`, compare `lk_a` and `lk_b`, `tee_a` and `tee_b`, `vbmeta_a` and `vbmeta_b`.

P7/PayJoy check: LK contains `INFO: fastboot disabled by payjoy!!!`. Determine the flag state before any slot B write.

Build U-Boot for MT6789 from the branch that supports it. Flash only `boot_b` after user approval. Use U-Boot fastboot/USB to load the kernel, system image and a temporary RAM userdata into the phone's 8 GB RAM.

Never run `fastboot flash`, `fastboot erase`, or `fastboot --set-active` without explicit approval.
