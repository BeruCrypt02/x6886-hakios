# Stock `super` as the XNU root

Offline verified, 24 Nov 2026. Sources: `build/loader/mtkloader.c`,
`devices/x6886/board.toml`, `ios27/system27-prune.apfs`, and the stock dump at
`/home/berucrypt00/otadump`. No live-device or network data is used here.

## Geometry and image format

- Target: GPT partition `super` on UFS LUN 2, 13,083,561,984 bytes
  (`0x30bd75000`), A/B groups `main_a` / `main_b`.
- Image: `ios27/system27-prune.apfs` written **raw at partition LBA 0**. No
  512-byte prefix, no HAKIOS wrapper, no carved `xnusys` partition.
- Proof: `NXSB` (`0x4253584e`) at file offset `0x20`, block size `0x1000`,
  block count `0x100125` => container `0x10022d000` = 4,297,248,768 bytes.
  The whole container fits inside `super`.

## What `super_apfs_probe()` accepts (`mtkloader.c:807-838`)

1. **Raw APFS at LBA 0.** The first block already has `NXSB` at offset 32.
   The loader sets `apfs_lba_off = 0`, `apfs_part = "super"`, prints
   `super: raw APFS container at offset 0` (current tree:
   `super: raw APFS container (NXSB at +0x20)`), and returns 1.
2. **HAKIOS v1 header.** Magic `HAKIOS`, version 1, `apfs_off` and `apfs_len`.
   The loader reads the block at `apfs_off` and accepts it only if that block
   carries `NXSB`. This is an alternate accepted form, not a requirement.

**A raw APFS image written at `super` LBA 0 is accepted with no wrapper.**
Phase 1 does not need a HAKIOS header to boot the flash root.

## `root_apfs=` boot arg

Boot args: `debug=0x8 serial=3 rd=md0 ufs=2 root_apfs=ufs_darwin -v ...`

The value is a `/dev/` node name, not a GPT name or an LBA. HakiOS userland
(`refs/hakios-common/init/init.c`) does
`mount("apfs", "/flash", MNT_RDONLY, "/dev/" + value)`. The kernel UFS driver
creates `/dev/ufs_<name>` for a partition the loader exports through the shared
block page; the loader exports stock `super` as `darwin`, hence
`/dev/ufs_darwin`. `ufs=2` asks the loader to serve that partition's blocks;
that export is still outstanding work in this tree.

## Why the 4.0 GiB container is rejected in Phase 1

`load_system_image()` (`mtkloader.c:841-914`) does not hand a block device to
XNU. It:

1. Walks consecutive APFS containers from the partition LBA: read one block,
   require `NXSB` at offset 32, read block size and block count, advance by
   `count * bs`, stop at the first block without `NXSB` (`:868-896`).
2. Clamps the summed size to `SYSTEM_MAX_MB` when that is set (`:904-909`).
3. Checks `SYSTEM_PA + size > XNU_PHYS_BASE + XNU_MEM_SIZE - 64 MiB` (`:911`)
   and prints `APFS container too large for the XNU window, using embedded md0`.
4. Only if the check passes, reads the whole container into DRAM at `SYSTEM_PA`
   and hands it to XNU as md0 (`:916-934`).

With `xnu_base = 0x140000000`, `xnu_size = 0x0ff600000`,
`SYSTEM_PA = XNU_PHYS_BASE + 0x08000000 = 0x148000000`, and 64 MiB reserved for
the DTB and boot_args, the largest accepted container is `0xf3600000` =
4,083,154,944 bytes (3.803 GiB). `system27-prune.apfs` is 4,297,248,768 bytes
(4.002 GiB), so it does not fit.

**The 3.8 GiB ceiling is the loader's preload model, not an XNU or UFS
block-driver limitation.** Phase 1 therefore keeps the small
`root26-darwin.apfs` (4 MiB) / embedded md0 path and leaves
`system27-prune.apfs` out of the flashed image.

## Phase 2 options (documented only, not implemented)

- **(a) Enlarge the XNU window.** Change `xnu_base` / `xnu_size` in
  `devices/x6886/board.toml` (and `HAKIOS_LOADER_XNU_BASE` /
  `HAKIOS_LOADER_XNU_SIZE` in `build/boards/x6886/hakios_platform.h`) so that
  `SYSTEM_PA + 4,297,248,768 <= XNU_PHYS_BASE + XNU_MEM_SIZE - 64 MiB`, i.e.
  `xnu_size >= 0x10c22d000` with the current base, while the window still fits
  in real RAM. Then re-run the load path and confirm the
  `APFS container too large` branch is not taken. Cost: about 4 GiB of DRAM
  wired from boot.
- **(b) Mount `super` directly from the kernel.** Change the loader to pass an
  LBA/device to XNU instead of preloading the container, and let XNU's UFS
  driver read APFS from the UFS partition on demand. This needs the XNU MTK UFS
  driver wired (`ufs=2` / `root_apfs=ufs_darwin` shared-page export). No DRAM
  copy, no window cap, no extra md slot.

Neither option changes Phase 1 code or the Phase 1 image layout.

## Build the Phase 1 image

`super` starts at LBA `0x60000` on UFS LUN 2 and spans `0x30bd75` blocks. The
partition is 13,083,561,984 bytes (`0x30bd75000`). Write the image to the
partition start, not to an LBA inside it. The UFS block size is 4096.

The loader accepts two forms at `super` LBA 0. Raw APFS with `NXSB` at byte 32
needs no wrapper. The HAKIOS v1 form puts a 512-byte header in block 0 and the
APFS container in block 1. Phase 1 uses the HAKIOS form with
`ios27/root26-darwin.apfs`, because the loader preloads that 4 MiB container
into DRAM and hands it to XNU as md0.

```sh
cd /home/berucrypt00/hakios-x6886
python3 - <<'PY'
import struct
B = 4096
SUPER = 0x30bd75000
hdr = b'HAKIOS\0\0' + struct.pack('<IIQQ', 1, 0, B, B) + b'\0' * 480
root = open('ios27/root26-darwin.apfs', 'rb').read()
assert len(root) == B and root[32:36] == b'NXSB'
img = hdr + b'\0' * (B - len(hdr)) + root + b'\0' * B
img += b'\0' * (SUPER - len(img))
open('build/hakios-super.img', 'wb').write(img)
print('build/hakios-super.img', len(img), 'bytes')
PY
```

Block 0 holds the header. Block 1 holds the container at partition byte 4096.
Block 2 is zero and stops `load_system_image()` from walking into later data.
The rest of the image is zero padding to the full partition size.

Check the image before any device work:

```sh
python3 - <<'PY'
import struct
d = open('build/hakios-super.img', 'rb').read(3 * 4096)
assert d[:8] == b'HAKIOS\0\0'
assert struct.unpack_from('<IIQQ', d, 8) == (1, 0, 4096, 4096)
assert d[4096 + 32:4096 + 36] == b'NXSB'
assert d[8192:12288] == b'\0' * 4096
print('header, APFS container, and terminator OK')
PY
```

`ios27/system27-prune.apfs` is 4,297,248,768 bytes. The loader rejects any
container larger than `0xf3600000` bytes, so leave it out of this image. Raw
`system27-prune.apfs` written at LBA 0 is still accepted by the GPT probe, but
`load_system_image()` refuses to preload it. Phase 2 handles that case.

## Flash from stock Recovery

Boot stock Recovery, push both images, and write them with `dd`. The `super`
write covers the whole partition.

```sh
adb push build/hakios-super.img /sdcard/hakios-super.img
adb push build/boot-x6886-final.img /sdcard/hakios-boot.img
adb shell 'dd if=/sdcard/hakios-super.img of=/dev/block/by-name/super bs=4096 conv=fsync'
adb shell 'dd if=/sdcard/hakios-boot.img of=/dev/block/by-name/boot_a bs=4096 conv=fsync'
```

Leave GPT, preloader, LK, tee, vbmeta, userdata, metadata, `tr_product`,
`tr_region`, and slot B alone. Phase 1 touches `super` and `boot_a` only.

## Rollback

Take a stock `super` backup before the first flash. There is no full stock
`super` dump in `/home/berucrypt00/otadump`; `super_empty.img` is 6224 bytes and
cannot restore the partition.

```sh
adb shell 'dd if=/dev/block/by-name/super of=/sdcard/stock-super.img bs=4096'
adb pull /sdcard/stock-super.img /home/berucrypt00/otadump/stock-super.img
```

If `boot_a` hangs, restore it from BootROM mode with Penumbra. The device must
enumerate on USB VCOM and a compatible DA with satisfied auth must be on the
host.

```sh
./antumbra write boot_a /home/berucrypt00/otadump/images/boot.img --da <compatible_DA.bin>
./antumbra reboot normal --da <compatible_DA.bin>
```

Stock Android or stock Recovery boots at that point. From Recovery, write the
stock `super` backup back:

```sh
adb push /home/berucrypt00/otadump/stock-super.img /sdcard/stock-super.img
adb shell 'dd if=/sdcard/stock-super.img of=/dev/block/by-name/super bs=4096 conv=fsync'
```

`mtk.py w boot_a <image>` replaces the two `antumbra` write and reboot commands
when the operator uses mtkclient instead. Penumbra cannot enable `fastboot
boot` on this LK, and the BootROM path stays conditional on DA and auth.
