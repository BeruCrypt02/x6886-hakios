# HakiOS iOS 27 on Infinix X6886 — VERIFIED handoff (24 Nov 2026)

> This file replaces an earlier version that contained wrong assumptions
> (wrong framebuffer pitch, wrong pstore zone, wrong boot-header diagnosis).
> Everything below was re-derived offline from primary sources by three
> independent audits (hardware/DTB, loader source, boot-image binary).

## Device & hard constraints

- Infinix X6886 (HOT 60 Pro+), XOS 16, Android 16 / SDK 36.
- SoC **MT6789** (Helio G200/G99, 2x A76 + 6x A55). NOT Dimensity 7200.
- GPU Mali-G57 MC2, **Job Manager (JM) / Valhall v9**, driver `mali_kbase_mt6789_a16w_jm`. NOT CSF. There is NO `mali_csffw.bin` / valhall CSF firmware for this chip.
- Touch: FocalTech FTS on **spi1** (`spi:fts`), panel `nt37706a_fhdp_dsi_vdo_dsc_boe_boe_144hz_x6886` (1080x2400, DSI Video + DSC, 144 Hz proven by name, not by the atag fps field).
- Fingerprint: `tran_fp_legacy` on **spi3** via Trustonic TEE (`mcDrvModule`). Skip FOD.
- Stock dump (READ ONLY): `/home/berucrypt00/otadump` — any edit is logged as a patch.
- Stock boot backup: `/home/berucrypt00/otadump/images/boot.img` (64 MB, v4 header).
- Project: `/home/berucrypt00/hakios-x6886` (symlink `/home/berucrypt00/x6886-hakios`).
- vbmeta disabled on device; Penumbra BROM rescue ready.
- Stock LK has NO `fastboot boot` → testing requires flashing `boot_a`.
- NEVER touch: GPT, vendor_boot (holds custom recovery), userdata, super, lk, preloader, slot B, set-active.
- Target: `boot_a` only. Use `-s 153857057T001452` for adb/fastboot.
- Ignore `/home/berucrypt00/Documents/port-tools`, COS/ColorOS port, fenrir scripts.

## Proved addresses (from atag, cmdline, iomem, DTB, refs bare-metal code)

| Item | Value | Proof |
|---|---|---|
| fb_base | `0xFE0C0000` | atag,videolfb od words 0x00/0x04 |
| fb vram | `0x01DF0000` | 3 x 1088 x 2400 x 4 = 0x1DE9C00, ALIGN 64K = 0x1DF0000 (exact atag match) |
| fb stride/pitch | **4352 (0x1100)** | 1080 -> 32px-align 1088 px x 4. NOT 7680 (that was 1920x4) |
| DISP_OVL0 | `0x14005000` | DTB node /soc/disp_ovl0@14005000 |
| DISP_OVL0_2L | `0x14006000` | DTB node /soc/disp_ovl0_2l@14006000 |
| OVL_SRC_CON | +`0x2c` | mtkdbg.h + MT6789-family patches 0188/0190 |
| OVL layer 0 | CON +0x30, PITCH +0x44, ADDR +0xf40 | same refs |
| OVL layer 1 | PITCH +0x64, ADDR +0xf60 | patch 0188; layer stride = 0x20 |
| OVL layer 2/3 | CON +0x70/0x90, PITCH +0x84/0xa4, ADDR +0xf80/0xfa0 | extrapolated from 0x20 stride (NOT directly read) |
| OVL SRC_SIZE | UNPROVEN — do not rely on it | no local definition |
| console-ramoops-0 | **`0x480af000`** size 0x40000 | LIVE /sys/module/ramoops/parameters: record_size 0x1000, dmesg 31 records (0x1f000) -> console -> ftrace 0x1000 -> pmsg |
| pmsg-ramoops-0 | `0x480f0000` size 0x80000 | same live parameters |
| ECC | disabled | ramoops ecc=0, no ramoops.ecc= in cmdline |
| XNU DRAM | `0x140000000 + 0x0ff600000` | ends exclusive 0x23f600000, strictly below reserved 0x23f600000-0x23fbfffff |
| kernel_load | `0x40010000` | /proc/iomem "40010000-... : Kernel code" |

## Bugs found and fixed in the iOS 27 (xnu-12377) boot path

All fixed by Subagent B, commits `0cbb0da` + `dc8976e`, final loader
`build/loader/mtkloader.bin` 41,400 B SHA256 `0ac7218ff2587ad1804d25ef6cbf007817617d46ecb43dc1c3992a72b1e532ec`:

1. **Stacks below `_start` (the real silent-hang cause).** With `_start=0x40010000` and DRAM from `0x40000000`, the old stacks were `0x40008000` / `0x4000e000` / `0x40000000` (exact DRAM base) — the first push wrote outside RAM. Fixed: four aligned `.bss` stack buffers; BSS cleared before any `mov sp`; all SP writes now `adrp/add` PC-relative. Verified: `primary_stack_top=0x40023000`, `fault=0x40027000`, `watcher=0x4002f000`, all inside `.bss` above `_start`.
2. **`display_find()` impossible condition** `pitch >= 4320 && pitch <= 4096` (always false) → fell back to wrong 7680. Fixed: scans enabled layers on both OVL0 and OVL0_2L, reads SRC_CON/PITCH/ADDR, upper bound 8192, fallback `0xFE0C0000 / 4352`. Feeds `boot_args.Video`.
3. **pstore logging wrote to wrong zones.** `RAMOOPS_CONSOLE2=0x4811f000` sat inside pmsg and corrupted `HAKIOS:BOOT-XNU`. Removed. `RAMOOPS_CONSOLE=0x480af000` (the real console zone, shifted 4 KiB down by the non-zero `ftrace_size=0x1000`), `RAMOOPS_PMSG=0x480f0000`. Framebuffer banner is the primary channel.
4. **UFS LUN 0 hardcode** (4 MB boot LUN) → GPT/super reads failed. `ufs_read()` now uses LUN 2 (`UFS_MAIN_LUN`), selectable LUN.
5. **Bringup defaults & DRAM window.** `MENU_DEFAULT_XNU=1`, `XNU_VERBOSE=1`, `XNU_ROOT_EMMC=0`, `XNU_WDT_SECONDS=30` (safety; disable with `-DXNU_WDT_SECONDS=0`). XNU window clamped to `0x140000000+0x0ff600000`. Old 768 MB window was overrun by 4 GB APFS.
6. **3-second on-screen banner + pstore log** before XNU: CurrentEL, real load address, LK dtb, OVL0 src/addr/pitch, XNUP entry, with TOPRGU kick (0x10007000+0x08, key 0x1971) every 250 ms.
7. **Cache/EL handoff audited:** `dsb sy; ic iallu; dsb sy; isb` in both EL1 and EL2; EL2 clears SCTLR_EL1 M/C/W before `eret`; clean kernel/DTB/ramdisk/boot_args before jump.

## Boot-image packaging bugs fixed (Subagent C)

`build/chainboot-x6886.py` was corrupting v4 headers:
- parsed v4 with v2 offsets; wrote page to offset 36 (reserved in v4); wrote a 5-part SHA1 into `hdr[576:608]` (inside v4 cmdline/id); `BOOT_PARTITION` 32 MB instead of 64 MB; stale `AVBf` footer pointing at zeroed vbmeta.
Fixed: spec-correct v4 header (magiskboot confirmed only bytes 0x08-0x0a / kernel_size differ from stock), no SHA1 field, 64 MB partition, no AVB footer. Added `build/repack-magiskboot.sh` as an independent cross-check. Both paths produce byte-identical images.

## Final image + offline verification

```
/home/berucrypt00/hakios-x6886/build/boot-x6886-final.img
SHA256 eeb2e43bee20278d833d333794b2f6fb43f1d8f735a14d61dfed6da8f4cd64ec
```
- magiskboot: HEADER_VER 4, KERNEL_SZ 22,769,663, RAMDISK_SZ 0, PAGESIZE 4096, KERNEL_FMT gzip.
- Kernel blob: `ARM\x64` magic, text_offset 0, image_size 0x3c00000, res2=40,024,576, res3=0x266ba00 with `XNUP` present, Android Image byte-identical to stock.
- mtkloader ELF: zero relocations, zero ABS64; stacks in `.bss` above `_start`.
- AVB: last 64 bytes zero; no `AVBf`/`AVB0` anywhere.

## Immediate next step (Beru runs manually — I do NOT flash)

```sh
fastboot -s 153857057T001452 flash boot_a /home/berucrypt00/hakios-x6886/build/boot-x6886-final.img
```

Expected: a clean on-screen 3-second banner (no glitch stripes) with CPU/OVL0/XNUP trace, then XNU boots with `-v debug=0x8 serial=3`. If anything fails, the banner and `console-ramoops-0` at 0x480af000 will say exactly where.

## Remaining uncertainty

- OVL0_2L may be the active composer; `display_alpha_off()` still only handles OVL0 — banner could be semi-transparent on the 2L path, but pstore remains.
- XNU window base 0x140000000 derived from known RAM, not measured on device.
- `XNU_WDT_SECONDS=0` leaves TOPRGU off at handoff. The proven timeout encoding stops at 31 s, short of a full userspace boot; a hung run needs a power cycle or Penumbra BROM. Set 1..31 for short bringup runs that want the log-back path.

## iOS 27 readiness

Kernel `xnu-12377` is built; `ios27/` APFS images exist. This boot image uses the
in-kernel XNUP payload path (`chainboot` res3), so vendor_boot is untouched. Full
iOS still needs: correct X6886 ADT (GICD/GICR/timer/OVL0 framebuffer/DRAM verified
above), XNU drivers for MT6789 UFS + DSI video + FTS touch, and real iOS userland
from an IPSW. Bringup runs can set `XNU_WDT_SECONDS=1..31` for repeated attempts with a log reset.
