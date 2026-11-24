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
