# Boot Chain & Partition Layout — Infinix X6886 (MT6789), stock OTA 155SP15

**Analysis target:** `/home/berucrypt00/otadump` (READ-ONLY; nothing in the dump was modified)
**Purpose:** evidence base for a non-destructive, slot-B-only XNU/HakiOS bring-up.
**Date:** 2026-11-23
**Tools used:** `cat`, `python3` (json/struct), `grep`, `find`, `strings`, `xxd`, `md5sum`, `nl`.

**Conventions used in this document**
- **VERIFIED** — value read directly from a file in the dump; the source path is given.
- **INFERENCE** — reasoning from verified facts, not a direct observation.
- All sizes are given as raw bytes plus MiB/GiB where useful. `MiB = 1024^2`, `GiB = 1024^3`.

---

## 0. Source files consulted

| File | Role |
|---|---|
| `/home/berucrypt00/otadump/super_partitions.json` | Logical (dynamic) partition inventory with sizes + SHA-256 |
| `/home/berucrypt00/otadump/super_merge_report.json` | Which source ("base" vs "open") was chosen per logical partition |
| `/home/berucrypt00/otadump/mtk/partition_info.json` | `super` size + group membership (`main_a` / `main_b`) per logical partition |
| `/home/berucrypt00/otadump/mtk/MT6789_Android_scatter.txt` and `.xml` | **Physical GPT / scatter** — authoritative partition table, offsets, sizes, regions |
| `/home/berucrypt00/otadump/mtk/version.csv`, `tool_custom.ini` | Flash-tool "Base"/"Open" variants, build prefix |
| `/home/berucrypt00/otadump/boot/header_info.json`, `boot/kernel` | boot.img header decode + extracted kernel |
| `/home/berucrypt00/otadump/images/*` | Raw boot-chain images (preloader, lk, boot, vendor_boot, dtbo, vbmeta*, …) |
| `/home/berucrypt00/otadump/vendor/etc/fstab.mt6789` and `vendor_boot/ramdisk/first_stage_ramdisk/fstab.mt6789` | Mount rules, `nofail`, AVB keys |
| `/home/berucrypt00/otadump/odm/etc/build.prop` | Anti-crack / ARB / AVB / RPMB properties |
| `/home/berucrypt00/otadump/system/system/etc/init/hw/init.rc` | Trigger that consumes the anti-crack property |
| `/home/berucrypt00/otadump/dump_state.txt`, `board-info.txt`, `README.md`, `all_files.txt` | Dump scope, board, device identity, inventory |

---

## 1. Full partition table

### 1.1 Important distinction (VERIFIED)

Two different "partition tables" exist in this dump and must not be confused:

1. **Physical GPT** — 72 entries `SYS0..SYS71` in the MTK scatter. Contains `preloader`, `lk_a/b`, `boot_a/b`, `super`, `userdata`, etc.
2. **Logical (dynamic) partitions inside `super`** — 32 entries (16 A + 16 B) in `super_partitions.json`. These are *not* GPT entries; they are LVM-like partitions in `super` and are listed with `_a`/`_b` suffixes.

`super_partitions.json` has **no `group` field**. Group membership comes from `mtk/partition_info.json`.

### 1.2 Physical GPT (VERIFIED)

Source: `/home/berucrypt00/otadump/mtk/MT6789_Android_scatter.txt` (and the identical `.xml`).
The scatter defines **two complete layouts with identical names and sizes**:

- `storage_type: EMMC` — first/primary layout. `preloader` in `EMMC_BOOT1`, `preloader_backup` in `EMMC_BOOT2`, everything else in `EMMC_USER`. `block_size: 0x20000`.
- A second `UFS_LU*` layout — same partition names/sizes, preloader in `UFS_LU0`/`UFS_LU1`, rest in `UFS_LU2`.

Project string: `project: x6886_h8921`, `platform: MT6789`.
**INFERENCE:** The eMMC layout is the primary one for this build (it is listed first, and the first-stage fstab uses `/devices/platform/soc/11240000.mmc*`). The dump contains both preloader variants but they are byte-identical (see §3.1), so the dump alone cannot definitively prove which storage the physical phone uses; both layouts must be treated as valid.

| SYS | Partition | Size (hex) | Bytes | MiB | Region (eMMC layout) |
|---|---|---|---|---|---|
| 0 | preloader | 0x100000 | 1,048,576 | 1.00 | EMMC_BOOT1 |
| 1 | preloader_backup | 0x100000 | 1,048,576 | 1.00 | EMMC_BOOT2 |
| 2 | pgpt | 0x8000 | 32,768 | 0.03 | EMMC_USER |
| 3 | boot_para | 0x100000 | 1,048,576 | 1.00 | EMMC_USER |
| 4 | proinfo | 0x300000 | 3,145,728 | 3.00 | EMMC_USER |
| 5 | misc | 0x80000 | 524,288 | 0.50 | EMMC_USER |
| 6 | para | 0x80000 | 524,288 | 0.50 | EMMC_USER |
| 7 | expdb | 0x8000000 | 134,217,728 | 128.00 | EMMC_USER |
| 8 | frp | 0x100000 | 1,048,576 | 1.00 | EMMC_USER |
| 9 | nvcfg | 0x2000000 | 33,554,432 | 32.00 | EMMC_USER |
| 10 | nvdata | 0x4000000 | 67,108,864 | 64.00 | EMMC_USER |
| 11 | vbmeta_a | 0x800000 | 8,388,608 | 8.00 | EMMC_USER |
| 12 | vbmeta_system_a | 0x800000 | 8,388,608 | 8.00 | EMMC_USER |
| 13 | vbmeta_vendor_a | 0x800000 | 8,388,608 | 8.00 | EMMC_USER |
| 14 | vbmeta_b | 0x800000 | 8,388,608 | 8.00 | EMMC_USER |
| 15 | vbmeta_system_b | 0x800000 | 8,388,608 | 8.00 | EMMC_USER |
| 16 | vbmeta_vendor_b | 0x800000 | 8,388,608 | 8.00 | EMMC_USER |
| 17 | metadata | 0x2000000 | 33,554,432 | 32.00 | EMMC_USER |
| 18 | persist | 0x31f8000 | 52,396,032 | 49.97 | EMMC_USER |
| 19 | protect1 | 0x800000 | 8,388,608 | 8.00 | EMMC_USER |
| 20 | protect2 | 0x800000 | 8,388,608 | 8.00 | EMMC_USER |
| 21 | tdfs | 0x3800000 | 58,720,256 | 56.00 | EMMC_USER |
| 22 | transec | 0x3800000 | 58,720,256 | 56.00 | EMMC_USER |
| 23 | seccfg | 0x800000 | 8,388,608 | 8.00 | EMMC_USER |
| 24 | otp | 0x3000000 | 50,331,648 | 48.00 | EMMC_USER |
| 25 | md1img_a | 0xc800000 | 209,715,200 | 200.00 | EMMC_USER |
| 26 | spmfw_a | 0x100000 | 1,048,576 | 1.00 | EMMC_USER |
| 27 | pi_img_a | 0x100000 | 1,048,576 | 1.00 | EMMC_USER |
| 28 | dpm_a | 0x400000 | 4,194,304 | 4.00 | EMMC_USER |
| 29 | scp_a | 0x600000 | 6,291,456 | 6.00 | EMMC_USER |
| 30 | sspm_a | 0x100000 | 1,048,576 | 1.00 | EMMC_USER |
| 31 | mcupm_a | 0x100000 | 1,048,576 | 1.00 | EMMC_USER |
| 32 | gz_a | 0x2000000 | 33,554,432 | 32.00 | EMMC_USER |
| 33 | lk_a | 0x400000 | 4,194,304 | 4.00 | EMMC_USER |
| 34 | logo_a | 0x1800000 | 25,165,824 | 24.00 | EMMC_USER |
| 35 | boot_a | 0x4000000 | 67,108,864 | 64.00 | EMMC_USER |
| 36 | vendor_boot_a | 0x4000000 | 67,108,864 | 64.00 | EMMC_USER |
| 37 | init_boot_a | 0x800000 | 8,388,608 | 8.00 | EMMC_USER |
| 38 | dtbo_a | 0x800000 | 8,388,608 | 8.00 | EMMC_USER |
| 39 | tee_a | 0x500000 | 5,242,880 | 5.00 | EMMC_USER |
| 40 | sec1 | 0x200000 | 2,097,152 | 2.00 | EMMC_USER |
| 41 | efuse | 0x80000 | 524,288 | 0.50 | EMMC_USER |
| 42 | nvram | 0x4000000 | 67,108,864 | 64.00 | EMMC_USER |
| 43 | dram_para | 0x1900000 | 26,214,400 | 25.00 | EMMC_USER |
| 44 | connsys_bt_a | 0x800000 | 8,388,608 | 8.00 | EMMC_USER |
| 45 | connsys_wifi_a | 0x800000 | 8,388,608 | 8.00 | EMMC_USER |
| 46 | connsys_gnss_a | 0xd80000 | 14,155,776 | 13.50 | EMMC_USER |
| 47 | md1img_b | 0xc800000 | 209,715,200 | 200.00 | EMMC_USER |
| 48 | spmfw_b | 0x100000 | 1,048,576 | 1.00 | EMMC_USER |
| 49 | pi_img_b | 0x100000 | 1,048,576 | 1.00 | EMMC_USER |
| 50 | dpm_b | 0x400000 | 4,194,304 | 4.00 | EMMC_USER |
| 51 | scp_b | 0x600000 | 6,291,456 | 6.00 | EMMC_USER |
| 52 | sspm_b | 0x100000 | 1,048,576 | 1.00 | EMMC_USER |
| 53 | mcupm_b | 0x100000 | 1,048,576 | 1.00 | EMMC_USER |
| 54 | gz_b | 0x2000000 | 33,554,432 | 32.00 | EMMC_USER |
| 55 | lk_b | 0x400000 | 4,194,304 | 4.00 | EMMC_USER |
| 56 | logo_b | 0x1800000 | 25,165,824 | 24.00 | EMMC_USER |
| 57 | boot_b | 0x4000000 | 67,108,864 | 64.00 | EMMC_USER |
| 58 | vendor_boot_b | 0x4000000 | 67,108,864 | 64.00 | EMMC_USER |
| 59 | init_boot_b | 0x800000 | 8,388,608 | 8.00 | EMMC_USER |
| 60 | connsys_bt_b | 0x800000 | 8,388,608 | 8.00 | EMMC_USER |
| 61 | connsys_wifi_b | 0x800000 | 8,388,608 | 8.00 | EMMC_USER |
| 62 | connsys_gnss_b | 0xd80000 | 14,155,776 | 13.50 | EMMC_USER |
| 63 | dtbo_b | 0x800000 | 8,388,608 | 8.00 | EMMC_USER |
| 64 | tee_b | 0x600000 | 6,291,456 | 6.00 | EMMC_USER |
| 65 | **super** | **0x30bd75000** | **13,083,561,984** | **12,477.46** | EMMC_USER |
| 66 | tkv_a | 0x100000 | 1,048,576 | 1.00 | EMMC_USER |
| 67 | tkv_b | 0x100000 | 1,048,576 | 1.00 | EMMC_USER |
| 68 | tranfs | 0x1308b000 | 319,336,448 | 304.54 | EMMC_USER |
| 69 | userdata | 0xc0000000 | 3,221,225,472 | 3,072.00 | EMMC_USER |
| 70 | flashinfo | 0x1000000 | 16,777,216 | 16.00 | EMMC_USER |
| 71 | sgpt | 0x8000 | 32,768 | 0.03 | EMMC_USER |

**VERIFIED observations**
- There is **no `recovery` partition** in the GPT. Recovery lives in the vendor boot ramdisk (see §3.4).
- There is **no `bootctrl` partition**; A/B slot metadata is in the small `misc` (512 KiB) partition (BCB). LK contains `bootctrl_init`.
- `tkv_a`/`tkv_b` (1 MiB each) are Transsion "trusted key/value" partitions — LK uses them for PayJoy flags and AVB persist state (§4.3).
- `tranfs` (~304.5 MiB), `transec`/`tdfs` (56 MiB each), `seccfg`, `protect1/2` are Transsion/secure partitions.

### 1.3 Logical (dynamic) partitions inside `super` (VERIFIED)

Source: `super_partitions.json` + `mtk/partition_info.json`.
All 32 entries are in one of two groups: `main_a` (slot A) or `main_b` (slot B).

| Logical partition | Group | Bytes | MiB | Source in merge (`super_merge_report.json`) |
|---|---|---|---|---|
| odm_a | main_a | 843,235,328 | 804.17 | base |
| odm_b | main_b | 843,235,328 | 804.17 | — |
| odm_dlkm_a | main_a | 4,460,544 | 4.25 | base |
| odm_dlkm_b | main_b | 4,460,544 | 4.25 | — |
| product_a | main_a | 451,981,312 | 431.04 | base |
| product_b | main_b | 451,981,312 | 431.04 | — |
| system_a | main_a | 967,806,976 | 922.97 | base |
| system_b | main_b | 967,806,976 | 922.97 | — |
| system_dlkm_a | main_a | 8,024,064 | 7.65 | base |
| system_dlkm_b | main_b | 8,024,064 | 7.65 | — |
| system_ext_a | main_a | 1,863,352,320 | 1777.03 | base |
| system_ext_b | main_b | 1,863,352,320 | 1777.03 | — |
| tr_carrier_a | main_a | 348,160 | 0.33 | **open** |
| tr_carrier_b | main_b | 348,160 | 0.33 | base |
| tr_company_a | main_a | 348,160 | 0.33 | **open** |
| tr_company_b | main_b | 348,160 | 0.33 | base |
| tr_manifest_a | main_a | 348,160 | 0.33 | **open** |
| tr_manifest_b | main_b | 348,160 | 0.33 | base |
| tr_misc_a | main_a | 348,160 | 0.33 | **open** |
| tr_misc_b | main_b | 348,160 | 0.33 | base |
| tr_overlayfs_a | main_a | 348,160 | 0.33 | **open** |
| tr_overlayfs_b | main_b | 348,160 | 0.33 | base |
| tr_preload_a | main_a | 348,160 | 0.33 | **open** |
| tr_preload_b | main_b | 348,160 | 0.33 | base |
| tr_product_a | main_a | 3,241,615,360 | 3091.45 | **open** |
| tr_product_b | main_b | 761,217,024 | 725.95 | base |
| tr_region_a | main_a | 3,253,149,696 | 3102.45 | **open** |
| tr_region_b | main_b | 348,160 | 0.33 | base |
| vendor_a | main_a | 664,006,656 | 633.25 | base |
| vendor_b | main_b | 664,006,656 | 633.25 | — |
| vendor_dlkm_a | main_a | 14,241,792 | 13.58 | base |
| vendor_dlkm_b | main_b | 14,241,792 | 13.58 | — |

`super_merge_report.json`: `"prefer": "open"`. "open" wins for all `tr_*_a` (the large XOS app partitions); "base" wins for everything else and for all `tr_*_b`.

---

## 2. Exact `super` size and layout

**VERIFIED — physical size**
- `mtk/partition_info.json` → `"super": { "size": "13083561984" }`
- MTK scatter `SYS65 super` → `partition_size: 0x30bd75000`
- **13,083,561,984 bytes = 12,477.46 MiB = 12.185 GiB**
- This matches the constraint in the project master note (A/B, 0x30bd75000).

**VERIFIED — metadata fields (note the disagreement; do not treat them as the physical size)**
- `super_partitions.json → logical_bytes = 16,916,238,336` (15.754 GiB). This is **larger than the physical super** — it is a bookkeeping total across both slot images, not a partition size.
- `super_merge_report.json → logical_bytes = 12,093,837,312` (11.263 GiB) — the merged "prefer open" set.
- Computed sums from `super_partitions.json`:
  - Sum of all **A** entries = **11,313,963,008 B (10.537 GiB)**
  - Sum of all **B** entries = **5,580,763,136 B (5.197 GiB)**
  - Sum A+B = 16,894,726,144 B (15.734 GiB)
- Group topology (from `partition_info.json`): `main_a`, `main_b`. Each logical partition has `size: 0` in that file (dynamic — actual bytes are in `super_partitions.json`).

**VERIFIED — slot asymmetry**
- Slot A carries the big XOS partitions: `tr_product_a` = 3,241,615,360 B (3.02 GiB) and `tr_region_a` = 3,253,149,696 B (3.03 GiB) → ~6.05 GiB together.
- Slot B equivalents are tiny in this dump: `tr_product_b` = 761,217,024 B (725.95 MiB), `tr_region_b` = 348,160 B (0.33 MiB).
- **INFERENCE:** the shipped slot B is a much smaller "base" image; slot A holds the full "open"/XOS build. This is why the fstab mounts `tr_product`/`tr_region` with `nofail` (§6) — the device must boot even if those logical partitions cannot be allocated/mounted.
- `images/super_empty.img` exists (6,224 B) — an empty super metadata image (VERIFIED by name/size; not decoded).

---

## 3. Boot chain

### 3.1 Chain order and where each stage lives (VERIFIED from scatter + image inventory)

```
Power-on ROM (PBL, SoC-internal — not in dump)
  └─> preloader            [EMMC_BOOT1 0x100000; backup EMMC_BOOT2 0x100000]
        └─> LK              [lk_a / lk_b, 0x400000 each]  ← bootloader, NO U-Boot
              └─> boot_a / boot_b            [0x4000000 each]  GKI kernel
                  vendor_boot_a/_b           [0x4000000 each]  vendor + recovery ramdisk
                  init_boot_a/_b             [0x800000 each]   generic ramdisk (no image dumped)
                  dtbo_a / dtbo_b            [0x800000 each]
                  vbmeta{a,system,vendor}_{a,b} [0x800000 each]
              └─> kernel -> init (PID 1)
```

| Stage | GPT partition(s) | GPT size | Dumped image | Dumped image bytes | Notes |
|---|---|---|---|---|---|
| Preloader | `preloader`, `preloader_backup` | 1 MiB each | `images/preloader.img`, `preloader_emmc.img`, `preloader_ufs.img`, `preloader_raw.img` | 387,056 (×3), 382,960 (raw) | `preloader.img` == `preloader_emmc.img` == `preloader_ufs.img` (md5 `ca71bbf6cc0c3e7c49844799d5aa8c6e`); `preloader_raw.img` differs |
| LK bootloader | `lk_a`, `lk_b` | 4 MiB each | `images/lk.img` | 3,149,312 | MediaTek LK; app paths `app/mt_boot/...` in strings |
| boot | `boot_a`, `boot_b` | 64 MiB each | `images/boot.img` | 67,108,864 | header v4, page 4096, `cmdline` empty |
| vendor_boot | `vendor_boot_a`, `vendor_boot_b` | 64 MiB each | `images/vendor_boot.img` (and `vendor_boot-debug.img` = 31,354,880) | 67,108,864 | contains `ramdisk/` + `recovery_ramdisk/` |
| init_boot | `init_boot_a`, `init_boot_b` | 8 MiB each | **not dumped** | — | `mtk/installed-files-ramdisk.txt` lists only `3589928 /ramdisk/init` |
| dtbo | `dtbo_a`, `dtbo_b` | 8 MiB each | `images/dtbo.img` | 8,388,608 | — |
| vbmeta (top) | `vbmeta_a/b` | 8 MiB each | `images/vbmeta.img` | 12,288 | AVB0 |
| vbmeta_system | `vbmeta_system_a/b` | 8 MiB each | `images/vbmeta_system.img` | 4,096 | AVB0 |
| vbmeta_vendor | `vbmeta_vendor_a/b` | 8 MiB each | `images/vbmeta_vendor.img` | 4,096 | AVB0 |
| TEE | `tee_a` 5 MiB / `tee_b` 6 MiB | — | `images/tee.img` | 1,067,584 | — |
| "gz" (MediaTek secure/hypervisor fw) | `gz_a/b` | 32 MiB each | `images/gz.img` | 2,800,880 | — |
| Modem | `md1img_a/b` | 200 MiB each | `images/md1img.img` (also `modem.img` 54,434,080) | 55,494,608 | — |
| Other co-processor fw | `scp_a/b` 6 MiB, `sspm_a/b` 1 MiB, `mcupm_a/b` 1 MiB, `dpm_a/b` 4 MiB, `pi_img_a/b` 1 MiB, `spmfw_a/b` 1 MiB, `connsys_{bt,wifi,gnss}_a/b` | — | corresponding `images/*.img` | see `/home/berucrypt00/otadump/images/` | — |

### 3.2 boot.img header (VERIFIED)

Source: `/home/berucrypt00/otadump/boot/header_info.json`

```json
{
  "bytes": 67108864,
  "cmdline": "",
  "format": "boot",
  "header_version": 4,
  "image": "boot.img",
  "kernel": { "file": "kernel", "format": "lz4_legacy", "size": 18885902 },
  "page_size": 4096,
  "ramdisk": { "note": "empty (normal for GKI boot)", "size": 0 }
}
```

- Boot header version **4**, **empty generic ramdisk** — GKI layout. The kernel is LZ4-compressed, 18,885,902 bytes, extracted to `boot/kernel`.
- **Kernel version (VERIFIED):** `kernel_version.txt` / `README.md` → Linux **6.12.38**.
- `board-info.txt` → `require board=mt6789`.

### 3.3 Device identity (VERIFIED)

From `/home/berucrypt00/otadump/README.md`:
- Model: Infinix Hot 60 Pro Plus (`X6886`), codename `x6886`, brand/manufacturer `infinix`, platform `mt6789`
- Release `16`, SDK `36`, ID `BP2A.250605.031.A3`, firmware `155SP15`
- Fingerprint: `Infinix/X6886-OP/Infinix-X6886:16/BP2A.250605.031.A3/301400007:user/release-keys`
- A/B device: true; Treble device: true

### 3.4 Recovery (VERIFIED)

- No `recovery` GPT partition (§1.2).
- `vendor_boot/ramdisk/` and `vendor_boot/recovery_ramdisk/` both exist and contain `init -> /system/bin/init`, first-stage fstabs (`fstab.mt6789`, `fstab.emmc`), SELinux contexts, and `/avb/*.avbpubkey` handling.
- **INFERENCE:** recovery is the Android 12+ "recovery-in-vendor_boot-ramdisk" model. A bring-up that replaces `vendor_boot_b` also replaces recovery — losing stock recovery/fastboot UI assets located at `odm*/etc/asset/VENDOR_BOOT/RAMDISK/res/images/` (logout/fastbootd/installing screens).

### 3.5 Storage controller (VERIFIED + INFERENCE)

- Kernel config: `CONFIG_MMC=y`, `CONFIG_MMC_BLOCK=y`, `CONFIG_MMC_CRYPTO=y`, `CONFIG_SCSI_UFSHCD=y`, `CONFIG_SCSI_UFS_CRYPTO=y`, `CONFIG_SCSI_UFSHCD_PLATFORM=y` — **both** eMMC and UFS drivers are compiled in.
- fstab references `/devices/platform/soc/11240000.mmc*` (MSDC/eMMC) and `/devices/platform/soc/soc:odm/11230000.msdc*` (SD).
- LK strings include `androidboot.boot_devices=bootdevice,soc/%08lx.ufshci,%08lx.ufshci` and `disable mmc0 when ufs boot`.
- **INFERENCE:** the ROM package is shared across eMMC and UFS SKUs; the scatter carries both layouts. Confirm actual storage on-device before selecting flash targets.

---

## 4. vbmeta / AVB status, anti-rollback, PayJoy/P7

### 4.1 vbmeta images (VERIFIED)

Decoded directly from the image headers (`strings` + `python3 struct`):

| Image | Size | Magic | `required_libavb` | `algorithm_type` | `hash_type` | Builder string |
|---|---|---|---|---|---|---|
| `images/vbmeta.img` | 12,288 | `AVB0` | 1.0 | 1 (= SHA256_RSA2048) | 0 (= SHA256) | `avbtool 1.3.0` |
| `images/vbmeta_system.img` | 4,096 | `AVB0` | 1.0 | 1 | 0 | `avbtool 1.3.0` |
| `images/vbmeta_vendor.img` | 4,096 | `AVB0` | 1.0 | 1 | 0 | `avbtool 1.3.0` |

**VERIFIED — descriptor contents (property keys found in the vbmeta payloads):**
- `images/vbmeta.img` contains chained `vbmeta_system`, `vbmeta_vendor` and property descriptors
  `com.android.build.<partition>.fingerprint` / `.os_version` / `.security_patch` for:
  `dtbo`, `vendor_boot`, `product`, `system_dlkm`, `system_ext`, `odm`, `odm_dlkm`, `tr_carrier`, `tr_company`, `tr_overlayfs`, `tr_preload`, `tr_product`, `tr_region`, `vendor_dlkm`, plus hash/hashtree descriptors for these partitions.
- Fingerprints inside vbmeta (VERIFIED strings): `com.android.build.dtbo.fingerprint` = `Infinix/X6886-OP/Infinix-X6886:16/BP2A.250605.031.A3/301550006:user/release-keys`, while most others are `transsion/tssi/tssi:16/BP2A.250605.031.A3/301550078:user/release-keys` or `Transsion/tssi/mssi_64_64only_cn_armv82:16/.../301550078:...`.
  - **Observation:** these vbmeta build IDs (`301550006`, `301550078`) differ from the README/full-OTA fingerprint (`301400007`). This is a per-component build-ID difference, not a contradiction.
- `images/vbmeta_system.img` → `com.android.build.system.{os_version,fingerprint,security_patch}`.
- `images/vbmeta_vendor.img` → `com.android.build.vendor.{os_version,fingerprint,security_patch}`.

**INFERENCE / LIMITATION:** the raw `flags` word and exact rollback_index values need `avbtool info_image` to state definitively; `avbtool` is not installed in this environment and no copy was found in the dump. Do not claim "verification disabled" from the data above alone. The verified facts are: AVB0 images exist, are per-slot A/B, are chained (top → system + vendor), carry descriptor/fingerprint data, and are consumed by fstab and LK.

### 4.2 AVB in fstab (VERIFIED)

From `vendor/etc/fstab.mt6789` and `vendor_boot/ramdisk/first_stage_ramdisk/fstab.mt6789`:
- `system`, `system_ext` mount with `avb=vbmeta_system` **and** `avb_keys=/avb/q-gsi.avbpubkey:/avb/r-gsi.avbpubkey:/avb/s-gsi.avbpubkey` (GSI public keys — direct evidence that the firmware is designed to accept a Generic System Image on those two partitions).
- `vendor`, `product`, `odm`, `vendor_dlkm`, `odm_dlkm`, `system_dlkm` mount with plain `avb` (verify against top-level `vbmeta`).
- `tr_*` logical partitions use a Transsion key: `avb_keys=/odm/etc/vconfig/tran_avb.pubkey` **plus `nofail`** (§6).
- `vbmeta_system` raw partition line carries `avb=vbmeta` (chained verification).

### 4.3 Anti-rollback / secure state (VERIFIED)

**LK strings** (`images/lk.img`):
- `Error getting rollback index from AVB_GROUP`
- `Error getting rollback index from RECOVERY_GROUP`
- `[ANTI_ROLLBACK] magic == 0`, `[ANTI_ROLLBACK] magic mismatch, tran_devctrl.magic = 0x%x`
- `[ANTI_ROLLBACK] data_len != 0`, `[ANTI_ROLLBACK] tran_anti_rollback_data error, code = %d`
- `[ANTI_ROLLBACK] opt update error`, `[ANTI_ROLLBACK] sbc is not enable`, `[ANTI_ROLLBACK] read %s partition err =%d`, `[ANTI_ROLLBACK] write %s partition err =%d`
- `anti_rollback_status=%x`, `anti_rollback_ver`
- `[AVB] Fail To Get Verification Disabled Bit`, `Skip to verification key Temporarily`, `: Public key used to sign data rejected.`
- `androidboot.verifiedbootstate=green|yellow|orange`, `Orange State`, `OS Not Being Verified Or Custom OS. Dismiss After 5 Seconds.`
- `RPMB don't support erase operation` — RPMB-backed state is used.

**odm properties** (`odm/etc/build.prop`, lines 72, 73, 86, 91, 92, 185–189):
```
ro.vendor.tran.rpmb.support=1
ro.vendor.tran.rpmb.factory.enable.support=1
ro.vendor.tran.avb_key=release_v1
ro.vendor.tran.keystoredb.protect.support=1
ro.odm.tr_keyfilevfy.support=0
ro.vendor.tran.arb_group_0_version=6
ro.vendor.tran.arb_group_1_version=6
ro.vendor.tran.arb_pl_version=6
ro.vendor.tran.arb_avb_img_version=6
```
**INFERENCE:** ARB version `6` across four groups; LK maintains Transsion ARB in `tkv`/secure storage plus AVB rollback indexes. Flashing lower-version images to a slot could trip ARB.

### 4.4 PayJoy / anti-theft — LK has an explicit fastboot kill switch (VERIFIED)

Strings in `images/lk.img`:
```
INFO: fastboot disabled by payjoy!!!
check_payjoy_flag
[tran] tran_tkv_get_payjoy_flag is failed
[tran] tran_tkv_get_ext is NULL
[tran] tran_get_tkv_addr_va_init is false
[AVB] create a new persist item...
[FASTBOOT] oem p2u on!
[FASTBOOT] oem p2u off!
oem p2u
oem tran_unlock
transsion oem unlock failed
flashing get_unlock_ability
flashing unlock / flashing lock
[UNLOCK_VFY] auth ok, unlock is allow!
[UNLOCK_VFY] auth fail, unlock is forbidden!
[UNLOCK_VFY] rsa_oaep pass
private partition '%s' forbidden to fastboot
command parameter is not allowed!
REJECT
```
- No `payjoy` literal appears in any filename in `all_files.txt`, nor in any `.rc`/`.prop` file — it exists **inside the LK binary**, checked via `tkv_a`/`tkv_b`.
- `androidboot.verifiedbootstate=orange` and the unlock warning UI are present, so unlock state is handled normally when PayJoy is not active.

**INFERENCE:** on a unit where the PayJoy flag is set in `tkv`, LK will disable fastboot entirely ("fastboot disabled by payjoy!!!"). This is the single biggest bring-up risk: a DSU/fastboot-based flow may be impossible until the flag is cleared through a Transsion-authorized path (`oem p2u` / `oem tran_unlock`). Do not assume fastboot is available.

---

## 5. The anti-crack prop — exact location and neighbours

**VERIFIED — definition**

File: `/home/berucrypt00/otadump/odm/etc/build.prop` (note: `odm`, not `vendor` or `system`).
Line **189**:
```
ro.vendor.tran.anti.crack.p7.support=1
```

Immediate surrounding block (lines 185–189, `nl -ba`):
```
185  ro.vendor.tran.arb_group_0_version=6
186  ro.vendor.tran.arb_group_1_version=6
187  ro.vendor.tran.arb_pl_version=6
188  ro.vendor.tran.arb_avb_img_version=6
189  ro.vendor.tran.anti.crack.p7.support=1
```
Other nearby related props in the same file: line 72–73 `ro.vendor.tran.rpmb.support=1` / `ro.vendor.tran.rpmb.factory.enable.support=1`; line 86 `ro.vendor.tran.avb_key=release_v1`; line 91 `ro.vendor.tran.keystoredb.protect.support=1`; line 92 `ro.odm.tr_keyfilevfy.support=0`; line 172 `ro.vendor.tran.trans_pay_tee=1`.

**VERIFIED — consumer at runtime**

File: `/home/berucrypt00/otadump/system/system/etc/init/hw/init.rc`, lines 1245–1248:
```
1245 #USD:add tran verify para by shishan.zheng 20250331 start
1246 on property:sys.boot_completed=1 && property:ro.boot.mode=normal && property:ro.vendor.tran.anti.crack.p7.support=1
1247     vfy_boot Trigger_P7 /vendor/bin/verify_para
1248 #USD:add tran verify para by shishan.zheng 20250331 end
```
Binary: `/home/berucrypt00/otadump/vendor/bin/verify_para` (present).

Two sibling triggers in the same block (VERIFIED):
- line 1236–1237: `on property:sys.boot_completed=1 && ... && property:ro.vendor.tran.rpmb.support=1` → `vfy_boot Trigger_Rpmb /vendor/bin/verify_rpmb`
- line 1251–1252: gated on `ro.odm.tr_keyfilevfy.support=1` → `vfy_boot Trigger_keyfilesvfy /system_ext/bin/verify_key_files` — **disabled in this build** because that prop is `0`.

**INFERENCE:** "P7" is a Transsion anti-crack/anti-clone verification routine run after boot through a custom `vfy_boot` init builtin. A GSI/donor `system`/`vendor` pair that does not carry `/vendor/bin/verify_para` (or the `vfy_boot` builtin) would fail to start this trigger; conversely, if a donor `odm` retains the prop but the binary is absent, init will attempt a missing path. Either way, the prop must be handled deliberately when composing the slot-B image set (the project master note already calls for an explicit allow-list).

---

## 6. fstab entries — `nofail`, super, vendor, odm, tr_*

Source (VERIFIED): `/home/berucrypt00/otadump/vendor/etc/fstab.mt6789`; identical dynamic lines in `/home/berucrypt00/otadump/vendor_boot/ramdisk/first_stage_ramdisk/fstab.mt6789`; recovery variant at `vendor_boot/recovery_ramdisk/first_stage_ramdisk/fstab.emmc`.

### 6.1 Dynamic logical partitions (inside `super`) — all `slotselect,logical,first_stage_mount`

```
system /system erofs ro wait,slotselect,avb=vbmeta_system,logical,first_stage_mount,avb_keys=/avb/q-gsi.avbpubkey:/avb/r-gsi.avbpubkey:/avb/s-gsi.avbpubkey
system /system ext4 ro ... (same flags)
system_ext /system_ext erofs|ext4 ro ... avb=vbmeta_system ... (same avb_keys)
vendor /vendor erofs|ext4 ro wait,slotselect,avb,logical,first_stage_mount
product /product erofs|ext4 ro wait,slotselect,avb,logical,first_stage_mount
odm /odm erofs|ext4 ro wait,slotselect,avb,logical,first_stage_mount
vendor_dlkm /vendor_dlkm erofs|ext4 ro wait,slotselect,avb,logical,first_stage_mount
odm_dlkm /odm_dlkm erofs|ext4 ro wait,slotselect,avb,logical,first_stage_mount
system_dlkm /system_dlkm erofs|ext4 ro wait,slotselect,avb,logical,first_stage_mount
```
- `erofs` is first choice, `ext4` fallback for every dynamic partition.
- **None of these core dynamic partitions has `nofail`.** `super` itself is not listed as a mount; it is consumed by the logical-partition driver via `first_stage_mount`.

### 6.2 Transsion `tr_*` logical partitions — all `nofail`

```
tr_region /tr_region erofs|ext4 ro wait,slotselect,logical,first_stage_mount,avb_keys=/odm/etc/vconfig/tran_avb.pubkey,nofail
tr_company /tr_company ... (same, nofail)
tr_carrier /tr_carrier ... (same, nofail)
tr_product /tr_product ... (same, nofail)
tr_preload /tr_preload ... (same, nofail)
tr_overlayfs /tr_overlayfs ... (same, nofail)
tr_misc /tr_misc ... (same, nofail)
tr_manifest /mnt/vendor/tr_manifest erofs|ext4 ro wait,slotselect,logical,first_stage_mount,avb_keys=/odm/etc/vconfig/tran_avb.pubkey
/mnt/vendor/tr_manifest /tr_manifest none ro,bind wait,nofail
```
- **VERIFIED:** `tr_manifest` is the only `tr_*` logical partition **without** `nofail` on its own line; its `nofail` is on the subsequent bind mount to `/tr_manifest`.
- **VERIFIED:** `tr_product`, `tr_region` (the ~6 GiB XOS app partitions) are explicitly `nofail` — the device boots without them. This directly supports the project note that they can be omitted if they do not fit in `super`.

### 6.3 Physical / metadata partitions

```
/dev/block/by-name/metadata /metadata ext4 noatime,nosuid,nodev,discard wait,check,formattable,first_stage_mount      (NOT nofail)
/dev/block/by-name/userdata  /data     f2fs ... wait,check,formattable,quota,latemount,resize,...,fileencryption=aes-256-xts:aes-256-cts:v2+inlinecrypt_optimized,...   (NOT nofail)
/dev/block/by-name/userdata  /data     ext4 ... (fallback, NOT nofail)
/dev/block/by-name/tranfs    /tranfs   ext4 noatime,nosuid,nodev,noauto_da_alloc,discard wait,check,formattable,nofail,first_stage_mount
```
Raw `emmc defaults` mounts include: `frp`, `nvram`, `proinfo`, `lk`, `lk2`/`bootloader2`, `misc`, `para`, `vendor_boot`, `init_boot`, `logo`, `expdb`, `seccfg`, `tee1/2`, `scp1/2`, `sspm1/2`, `dpm1/2`, `mcupm1/2`, `efuse`, `md1img`, `modem`, `gz1/2`, `spmfw`, `pi_img`, `boot_para`, `odmdtbo`, `dtbo`, `otp`, `vbmeta`, `tkv`.

**VERIFIED `nofail` boot-critical lines:**
```
/dev/block/by-name/boot           /boot            emmc defaults first_stage_mount,nofail,slotselect
/dev/block/by-name/vbmeta_vendor  /vbmeta_vendor   emmc defaults first_stage_mount,nofail,slotselect
/dev/block/by-name/vbmeta_system  /vbmeta_system   emmc defaults first_stage_mount,nofail,slotselect,avb=vbmeta
```

**INFERENCE:** several by-name aliases in the fstab (`lk2`, `tee1`, `gz1`, `md1dsp`, `ccu`, `vcp`, `gpueb`, `apusys*`, `mcf_ota`, `mvpu_algo*`) do not exist in the MTK scatter. They are generic AOSP/GSI fstab entries; on this device the real labels are `lk_a/b`, `tee_a/b`, `gz_a/b`, etc. Missing aliases are harmless because those lines are plain `defaults` (not `nofail` but also not first-stage-critical) — however, a donor fstab carried into the port should be reconciled against the real GPT.

---

## 7. Safe vs forbidden writes for a slot-B bring-up

The boundaries below are stated as a **risk assessment**, not as flash commands (per task constraints — no commands are provided). Slot A, GPT, and non-replaceable calibration/security storage are excluded.

### 7.1 FORBIDDEN — never write (VERIFIED identities + policy from the task)

| Target | Why |
|---|---|
| `pgpt`, `sgpt` (32 KiB each) | Primary/secondary GPT — repartitioning bricks the device |
| `preloader`, `preloader_backup` (1 MiB each, EMMC_BOOT1/BOOT2 or UFS LU0/LU1) | Boot ROM stage; corruption is unrecoverable without SoC-level rescue |
| `userdata` (3 GiB) | Contains user's encrypted data; out of scope |
| `metadata` (32 MiB) | Holds FBE metadata / vold keys; corruption makes userdata unreadable |
| `super` (12.185 GiB) as a whole | Shared by **both** slots — writing it endangers slot A. Logical partitions inside super must be handled via a slot-scoped dynamic mechanism (DSU), not a whole-super rewrite |
| All slot **A** partitions (`*_a`, `vbmeta_a`, `vbmeta_system_a`, `vbmeta_vendor_a`, `lk_a`, `boot_a`, `vendor_boot_a`, `dtbo_a`, `logo_a`, `tee_a`, …) | Slot A is the known-good fallback |
| `frp`, `nvram`, `persist`, `proinfo`, `efuse` (0.5 MiB), `otp` (48 MiB), `seccfg` (8 MiB), `protect1/2` (8 MiB each), `tdfs`/`transec` (56 MiB each) | Calibration, RF, secure/anti-rollback and FRP state |
| `tkv_a`, `tkv_b` (1 MiB each) | Verified PayJoy flag + AVB persist storage in LK |
| `flashinfo` (16 MiB) | Fastboot flash history used by LK (`Please check the flashinfo partition for details.`) |

### 7.2 CONDITIONALLY SAFE — slot B static images only, subject to LK/PayJoy checks

`boot_b` (64 MiB), `vendor_boot_b` (64 MiB), `dtbo_b` (8 MiB), `init_boot_b` (8 MiB), `vbmeta_b` / `vbmeta_system_b` / `vbmeta_vendor_b` (8 MiB each), `lk_b` (4 MiB), `logo_b` (24 MiB), and B-slot coprocessor firmware (`md1img_b`, `scp_b`, `sspm_b`, `mcupm_b`, `dpm_b`, `pi_img_b`, `spmfw_b`, `connsys_*_b`, `tee_b`, `gz_b`).

**VERIFIED constraints that can block even these:**
- `INFO: fastboot disabled by payjoy!!!` — fastboot may be unavailable entirely.
- `private partition '%s' forbidden to fastboot` — LK refuses some partitions via fastboot.
- `command parameter is not allowed!` / `REJECT` — command-level rejection.
- `Boot image location and length mismatch.` / `invalid boot image header` / `Image is signed for special device, please download common image!` / `: Public key used to sign data rejected.` — image-header and signature validation.

### 7.3 Logical partitions on slot B (INFERENCE)

`system_b`, `product_b`, `system_ext_b`, `odm_b`, `vendor_b`, `*_dlkm_b` and the `tr_*_b` partitions are **inside `super`**. Because `super` is forbidden as a whole, a non-destructive bring-up must present these via a slot-scoped dynamic mechanism (the project's DSU Sideloader path), not by rewriting the on-device super image. Slot B has ~5.197 GiB of logical partitions in the dump, so there is nominal free space inside the 12.185 GiB super — but only a dynamic/overlay mechanism can use it without touching slot A.

---

## 8. LK rejecting `fastboot boot` / U-Boot presence

### 8.1 U-Boot — ABSENT (VERIFIED)

- No `U-Boot`, `U_BOOT`, or `Das U-Boot` string in `lk.img`, any `preloader*.img`, `boot.img`, `vendor_boot.img`, `tee.img`, or `gz.img`.
- The bootloader is **MediaTek LK** (evidence: `images/lk.img`, GPT `lk_a`/`lk_b`, LK strings `app/mt_boot/mt_boot.c`, `fastboot_init`, `platform/mediatek/mt6789/sboot/...`).
- The `UFS_LU_BOOT1/BOOT2 write protect` strings in `preloader*.img` refer to UFS boot logical units, **not** U-Boot.

### 8.2 Evidence about fastboot boot rejection (VERIFIED strings vs INFERENCE)

**VERIFIED — LK contains restrictive logic:**
- `INFO: fastboot disabled by payjoy!!!`, `check_payjoy_flag`, `tran_tkv_get_payjoy_flag`
- `private partition '%s' forbidden to fastboot`
- `command parameter is not allowed!`
- `REJECT`
- `Boot image location and length mismatch.`
- `[%s:%d: invalid boot image header`
- `[AVB] invalid boot image type`
- `Image is signed for special device, please download common image!`
- `: Public key used to sign data rejected.`
- `[UNLOCK_VFY] auth fail, unlock is forbidden!`
- `Device will continue to bootup in %ds`, `continue`, `boot`, `reboot-bootloader` (standard fastboot verbs are present)

**VERIFIED — no literal string `fastboot boot` was found** in `lk.img` (the verb is generically handled by fastboot's `boot`/`continue` path).

**INFERENCE:**
- There is **no evidence of a categorical, unconditional rejection of `fastboot boot`.** The restrictions are **conditional**: PayJoy flag, unlock/auth state, private-partition list, command-parameter validation, and boot-image header/signature checks.
- A stock, unlocked, PayJoy-free device should accept normal fastboot boot flow; a locked or PayJoy-flagged device may reject it with one of the exact messages above.
- `fastboot oem p2u` and `fastboot oem tran_unlock` are Transsion-specific commands present in LK — likely the authorized route for the PayJoy/unlock state, but their preconditions/signatures are not documented in the dump.

---

## 9. Bottom line for the bring-up plan (short)

1. Physical super: **13,083,561,984 B (12.185 GiB)**, A/B groups `main_a`/`main_b`. Slot A ≈10.54 GiB logical, slot B ≈5.20 GiB logical.
2. Boot chain: **preloader → LK → boot + vendor_boot + init_boot + dtbo + vbmeta***; **no U-Boot**; **no recovery partition** (recovery is in `vendor_boot`'s ramdisk).
3. Kernel: Linux **6.12.38**, boot header v4, GKI (empty generic ramdisk).
4. AVB: three chained `AVB0` images (top/system/vendor), GSI public keys in fstab for `system`/`system_ext`, Transsion key for `tr_*`; ARB version 6 in four groups, RPMB used.
5. **PayJoy flag in LK can disable fastboot outright** — check before planning any fastboot/DSU step.
6. Anti-crack prop lives at **`odm/etc/build.prop:189`** and is consumed at **`system/system/etc/init/hw/init.rc:1246-1247`** → `/vendor/bin/verify_para`; it must be handled explicitly.
7. All `tr_*` logical partitions are `nofail`; core `system/vendor/product/odm/...` are **not** `nofail`.
8. Write boundary: **slot B static images only**; never GPT, preloader, userdata, metadata, whole super, slot A, or the secure/calibration/TKV partitions. Logical B partitions must be delivered through a dynamic (DSU) mechanism, not a super rewrite.

*End of report. No device was accessed; no flash commands were issued; the dump was not modified.*
