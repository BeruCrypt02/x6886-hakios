# Stock `super` as the XNU root — Phase 1 spec (offline verified)

## What is flashed

- Target: the stock GPT partition named **`super`** on LUN 2 (A/B groups
  `main_a`/`main_b`), 13,083,561,984 bytes. Read-only dump at `/home/berucrypt00/otadump`.
- Image: `ios27/system27-prune.apfs` written **raw at LBA 0** of `super`.
  No HakiOS header, no 512-byte prefix, no carved `xnusys` partition.
- Header proof (Python, offline): at offset `0x20` the file has
  `4e 58 53 42 00 10 00 00` = `"NXSB"`, APFS block size `0x1000` (4096),
  block count `0x100125` (1,049,133) => container = `0x10022d000`
  (4,297,248,768 bytes). The container fits inside `super`; there is no byte
  swapping or repacking to do.

## What the loader does

- `gpt_find("super")` gets the first LBA and block count from LUN 2's GPT.
- One `disk_read(lba, 1, sector_buf)`; accept only if
  `*(uint32_t *)(sector_buf + 32) == 0x4253584eu` (`"NXSB"`).
- No `xnusys` name is used for the root and no `xnusys` GPT entry is required.
  (`SYSTEM_PART` from the board header is only the payload-flash fallback.)

## `root_apfs=` form passed in boot_args

```
debug=0x8 serial=3 rd=md0 ufs=2 root_apfs=ufs_darwin -v cpus=1 loader_return_after=0
```

- Value passed: **`ufs_darwin`** — a `/dev/` device node name.
- Source of truth: `refs/hakios-common/init/init.c:2153` reads the boot arg with
  `boot_arg("root_apfs")`; `flash_root()` at :181-197 does
  `mount("apfs", "/flash", MNT_RDONLY, "/dev/" + value)`. The kernel's UFS driver
  (`bsd/dev/board/ufs.c:1681`) creates `/dev/ufs_<GPT name>` for each partition
  the loader exports through the shared block page. The loader exports stock
  `super` under the name `darwin`, hence `/dev/ufs_darwin`.
- NOT a GPT partition name (`super`, `xnusys`) and NOT an LBA. No kernel or
  userland code parses the value as a GPT name or a number.
- `ufs=2` asks this loader to serve the exported partition's blocks; the page's
  physical address is passed as `ufs_shared_pa=0x...` by the upstream loader's
  block service (`arch/arm64/boot/src/run.c:320`). That export is the remaining
  work for this boot arg to resolve to a real node.

## Why not copy the container into DRAM as md1

- XNU window is `0x140000000 + 0x0ff600000` (~3.98 GiB, handoff doc).
  `SYSTEM_PA` starts 128 MiB into it, leaving ~3.86 GiB. The container is
  ~4.00 GiB, so it does not fit. Loader-side `load_system_image()` already
  rejects an image that overruns the window.
- The HakiOS boot volume (`flashboot.apfs`, 4 MB, `/sbin/launchd` = stage0) must
  be md0; stage0 mounts `/dev/md1` (the hakios image with `/sbin/init`) at
  `/hakios`. That leaves no third md slot for a 4 GB container anyway.
- Reading through the UFS shared page avoids both problems: the kernel reads
  `super` on demand, no DRAM copy, no extra device slot.
