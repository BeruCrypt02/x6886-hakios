# Firmware Blob Inventory — Infinix X6886 (MT6789) stock dump

Source: `/home/berucrypt00/otadump` (read-only). All analysis performed with `find`, `ls`, `stat`,
`file`, `strings`, `xxd`, `grep` on the dumped files. No phone access, no writes to the dump.

All sizes below are **exact byte counts** from `find -printf '%s'` / `stat`, not `ls -h` rounding.

---

## 0. Scope and key findings (verified)

1. There are exactly **two** directories whose name is `firmware`:
   - `/home/berucrypt00/otadump/vendor/firmware` — 77 regular files, 7,324,386 bytes + 1 dangling symlink
   - `/home/berucrypt00/otadump/odm/firmware` — 17 regular files, 5,840,343 bytes (incl. `ffu_bin/`)
2. Additional firmware blobs exist outside those directories:
   - `/home/berucrypt00/otadump/images/` — 26 partition images, 294,443,056 bytes (coprocessor/modem/boot firmware)
   - `/home/berucrypt00/otadump/vendor/etc/imgsensor/mt6881/` — 12 camera sensor blobs, 607,561 bytes
   - `/home/berucrypt00/otadump/odm/etc/vconfig/audio/pa_param/` — 2 Awinic DSP blobs, 58,224 bytes
   - `/home/berucrypt00/otadump/odm/etc/vconfig/audio/pa_param/` and `odm/firmware/` also contain `Conf_MultipleTest.ini` (37,773 bytes) which is Awinic calibration text, not a binary blob.
3. **No standalone GPU/Mali firmware blob exists** in `vendor/firmware` or `odm/firmware`. The GPU driver is present and does call `request_firmware` / `kbase_device_firmware_init_once`, and the vendor_boot ramdisk contains the string `sffw.bin`, but no matching file was found in the dump (see §3).
4. Wi-Fi/BT/FM/GPS (CONSYS) firmware is the largest group in `vendor/firmware` (3,684,620 bytes). Both MT6631 ("1d") and MT6635 ("1e") variants are shipped; build tags prove this.
5. `ueventd.rc` search path: `firmware_directories /etc/firmware/ /odm/firmware/ /vendor/firmware/ /firmware/image/` (both `system/system/etc/ueventd.rc:4` and `vendor_boot/ramdisk/system/etc/ueventd.rc:4`).
6. Coprocessor firmware (`gz`, `atf`/TEE, SCP, SSPM, MCUPM, SPMFW, DPM, TKV, modem) is loaded by preloader/LK from raw partition images in `images/`, **not** via `request_firmware`.

---

## 1. Complete firmware inventory, grouped by subsystem

### 1.1 GPU / Mali — NO dedicated firmware files found

Files in the dump (drivers/libraries, not firmware blobs):

| Path | Bytes | Notes |
|---|---|---|
| `vendor_dlkm/lib/modules/mali_kbase_mt6789_a16w_jm.ko` | (module) | GPU driver, contains `request_firmware`, `kbase_device_firmware_init_once`, CSFFW trace strings |
| `vendor_dlkm/lib/modules/mali_mgm_mt6789_a16w_jm.ko` | (module) | memory group manager |
| `vendor_dlkm/lib/modules/mali_prot_alloc_mt6789_a16w_jm.ko` | (module) | protected allocator |
| `vendor/lib64/egl/mt6789/libGLES_mali.so` | (library) | contains version string `1.5 Valhall-"r54p1-12eac0"` |
| `vendor/etc/mali_platform.config` | 87 | `PLATFORM_AGT_FREQUENCY_KHZ=13000`, `OSU_PROTECTED_MEMORY_HEAP_NAME=mtk_svp_region-aligned` |

No `*mali*.bin`, `*csffw*.bin`, `*gpu*.bin` file exists anywhere in the dump (verified by `find`). See §3 for details.

### 1.2 Wi-Fi / Bluetooth / FM / GPS — CONSYS "soc2_2" (3,684,620 bytes)

All in `/home/berucrypt00/otadump/vendor/firmware`:

| File | Bytes | Variant |
|---|---|---|
| `WIFI_RAM_CODE_soc2_2_1d_1.bin` | 712,676 | 1d (MT6631) |
| `WIFI_RAM_CODE_soc2_2_1e_1.bin` | 707,284 | 1e (MT6635) |
| `soc2_2_ram_wifi_1d_1_hdr.bin` | 241,680 | 1d, Wi-Fi patch/RAM |
| `soc2_2_ram_wifi_1e_1_hdr.bin` | 243,556 | 1e, Wi-Fi patch/RAM |
| `soc2_2_ram_mcu_1d_blr_1_hdr.bin` | 198,012 | 1d, MCU patch (BLR) |
| `soc2_2_ram_mcu_1e_1_hdr.bin` | 197,924 | 1e, MCU patch |
| `soc2_2_ram_bt_1d_blr_1_hdr.bin` | 691,052 | 1d, Bluetooth patch (BLR) |
| `soc2_2_ram_bt_1e_1_hdr.bin` | 692,436 | 1e, Bluetooth patch |

FM radio co-processor blobs (16,960 bytes total):

| File | Bytes |
|---|---|
| `mt6627_fm_v1_coeff.bin` | 1,092 |
| `mt6627_fm_v1_patch.bin` | 1,028 |
| `mt6630_fm_v1_coeff.bin` | 1,092 |
| `mt6630_fm_v1_patch.bin` | 1,028 |
| `mt6630_fm_v2_coeff.bin` | 1,092 |
| `mt6630_fm_v2_coeff_tx.bin` | 1,092 |
| `mt6630_fm_v2_patch.bin` | 1,028 |
| `mt6630_fm_v2_patch_tx.bin` | 1,028 |
| `mt6631_fm_v1_coeff.bin` | 1,092 |
| `mt6631_fm_v1_patch.bin` | 1,028 |
| `mt6632_fm_v1_coeff.bin` | 1,092 |
| `mt6632_fm_v1_patch.bin` | 1,028 |
| `mt6635_fm_v1_coeff.bin` | 1,092 |
| `mt6635_fm_v1_patch.bin` | 1,028 |
| `soc_fm_v1_coeff.bin` | 1,092 |
| `soc_fm_v1_patch.bin` | 1,028 |

CONSYS configuration text files:
- `vendor/firmware/WMT_SOC.cfg` — 195 bytes
- `odm/firmware/WMT_SOC.cfg` — 193 bytes
- `vendor/firmware/BT_FW.cfg` — 999 bytes (contains `# connac1 6631 setting`)
- `odm/firmware/BT_FW.cfg` — 456 bytes
- `vendor/firmware/fm_cust.cfg` — 304 bytes
- `odm/firmware/wifi.cfg` — 293 bytes
- `odm/firmware/txpowerctrl.cfg` — 324 bytes
- `vendor/firmware/connfem.cfg` — 0 bytes (empty)

### 1.3 Audio — Awinic smart-PA (odm) + MT6366 codec (no blob)

Awinic blobs (`/home/berucrypt00/otadump/odm/firmware`):

| File | Bytes |
|---|---|
| `awinic_params.bin` | 41,288 |
| `aw87xxx_acf.bin` | 5,988 |

Awinic DSP calibration (`/home/berucrypt00/otadump/odm/etc/vconfig/audio/pa_param/`):

| File | Bytes |
|---|---|
| `AW_DSP.bin` | 43,628 |
| `AW_SINWAVE_DSP.bin` | 14,596 |

Related text config: `odm/etc/vconfig/audio/dts/customer.cfg`, `odm/firmware/Conf_MultipleTest.ini` (37,773 bytes).

**MT6366:** codec driver modules present (`vendor_dlkm/lib/modules/mt6789-mt6366.ko`, `snd-soc-mt6366.ko`, both in `modules.load`). No MT6366-specific firmware blob found. `strings` on both modules shows no `.bin` / `request_firmware` name; MT6366 is register-programmed from the kernel driver and device tree. Audio amplifier path is `snd-soc-aw87xxx.ko` → `aw87xxx_acf.bin` (see §5).

### 1.4 Touch panel firmware (2,408,642 bytes)

Goodix GT9896S (vendor/firmware) — 20 firmware images + 20 config images:

| File | Bytes |
|---|---|
| `gt9896s_firmware_1080x2280.bin` | 88,320 |
| `gt9896s_firmware_1080x2400.bin` | 96,512 |
| `gt9896s_firmware_6781v01_1080x2280.bin` | 88,320 |
| `gt9896s_firmware_6781v01_1080x2300.bin` | 88,320 |
| `gt9896s_firmware_6789v01.bin` | 96,512 |
| `gt9896s_firmware_6833v01.bin` | 88,320 |
| `gt9896s_firmware_6853v01.bin` | 88,320 |
| `gt9896s_firmware_6855v01.bin` | 96,512 |
| `gt9896s_firmware_6873v00.bin` | 88,320 |
| `gt9896s_firmware_6877v01.bin` | 88,320 |
| `gt9896s_firmware_6877v02.bin` | 88,320 |
| `gt9896s_firmware_6878v01.bin` | 96,512 |
| `gt9896s_firmware_6879v01.bin` | 96,512 |
| `gt9896s_firmware_6893v01.bin` | 88,320 |
| `gt9896s_firmware_6893v02.bin` | 88,320 |
| `gt9896s_firmware_6893v03.bin` | 88,320 |
| `gt9896s_firmware_6893v04.bin` | 88,320 |
| `gt9896s_firmware_6893v05.bin` | 88,320 |
| `gt9896s_firmware_6895v01.bin` | 96,512 |
| `gt9896s_firmware_6983v01.bin` | 96,512 |

Config counterparts (`gt9896s_cfg_*.bin`), all 1,104–1,530 bytes each (20 files, 27,366 bytes total):
`1080x2280`, `1080x2400`, `6781v01_1080x2280`, `6781v01_1080x2300`, `6789v01`, `6833v01`, `6853v01`, `6855v01`, `6873v01`, `6877v01`, `6877v02`, `6878v01`, `6879v01`, `6893v01`…`6893v05`, `6895v01`, `6983v01`.

Goodix subtotal: **1,851,110 bytes**.

Novatek touch (4 × 139,264 = **557,056 bytes**):
- `vendor/firmware/novatek_ts_fw.bin` — 139,264
- `vendor/firmware/novatek_ts_fw_144.bin` — 139,264
- `vendor/firmware/novatek_ts_fw_jdi.bin` — 139,264
- `vendor/firmware/novatek_ts_fw_tm.bin` — 139,264

Other touch (odm/firmware):
- `focaltech_ts_fw.bin` — 237,568
- `chipsemi_ts_fw.bin` — 142,508

### 1.5 Fingerprint / NFC / other vendor/firmware blobs

| File | Bytes | Likely role |
|---|---|---|
| `vendor/firmware/firmware_slm_567.bin` | 134,144 | ARM Cortex-M firmware (vector table at 0x0); `file` reports `initial SP 0x20002030, reset 0x002000d4`. Purpose not confirmed — see §8 |
| `vendor/firmware/SEC_THN31_FW_VTP.txt.bin` | 448,570 | High-byte (non-UTF8) text config; not a classic binary blob |
| `vendor/firmware/SEC_THN31_FW_VTP_M1ENABLE.txt.bin` | 440,196 | Same class |
| `vendor/firmware/st21nfc_fw7.bin` | 190,200 | ST ST21NFC controller firmware (`/dev/st21nfc` in ueventd.rc) |
| `vendor/firmware/remoteproc_scp` | 32 | All-zero bytes — remoteproc SCP marker, not a real image |

### 1.6 DSP / coprocessor — raw images in `images/` (verified MTK header magic `88 16 88 58`)

All files begin with MTK image magic `88 16 88 58`; the 8-byte field at offset 0x08 names the image.

| File | Bytes | Embedded name (hex → ASCII) | Role |
|---|---|---|---|
| `images/gz.img` | 2,800,880 | `677a` → `gz` | GenieZone hypervisor / TrustZone |
| `images/tee.img` | 1,067,584 | `6174 6600` → `atf` | ARM Trusted Firmware / TEE (file name `tee.img`, header says `atf`) |
| `images/scp.img` | 546,272 | `tinysys-scp-RV55_A` | System Control Processor (RISC-V RV55) |
| `images/sspm.img` | 138,112 | `tinysys-sspm` | Subsystem power manager |
| `images/mcupm.img` | 129,440 | `tinysys-mcupm-RV33_A` | MCU power manager (RISC-V RV33) |
| `images/spmfw.img` | 14,752 | `spmfw` | System power manager firmware |
| `images/dpm.img` | 148,048 | `dpmpm` | DPM firmware |
| `images/tkv.img` | 528,624 | `tkv` | Trusted key / TKV firmware |
| `images/pi_img.img` | 16,416 | `pi_img` | PI firmware |
| `images/efuse.img` | 512 | — | eFuse data |
| `images/md1img.img` | 55,494,608 | `md1rom` | Modem (MD1) ROM/RAM firmware |
| `images/modem.img` | 54,434,080 | — | Modem image |
| `images/logo.img` | 442,688 | — | Boot logo |
| `images/preloader.img` / `_emmc` / `_ufs` | 387,056 each | — | Preloader (three variants) |
| `images/preloader_raw.img` | 382,960 | — | Preloader raw |
| `images/lk.img` | 3,148,992 | — | Little Kernel bootloader |
| `images/dtbo.img` | 8,388,608 | — | Device tree blob overlay |
| `images/boot.img` | 67,108,864 | — | Android boot image (kernel) |
| `images/vendor_boot.img` | 67,108,864 | — | Vendor boot ramdisk |
| `images/vendor_boot-debug.img` | 31,354,880 | — | Debug vendor boot |
| `images/vbmeta*.img`, `super_empty.img` | 4,096/12,288/6,224 | — | AVB metadata / empty super |

Kernel-side DSP/SCP modules (all in `vendor_dlkm/lib/modules/modules.load`):
`adsp.ko`, `scp.ko`, `ccci_fsm_scp.ko`, `mtk-scp-audiocommon.ko`, `mtk-scp-ultra.ko`,
`snd-soc-mtk-scp-ultra.ko`, `ts_scp_common.ko` (touch), `mtk-scpsys*.ko` (in vendor_boot ramdisk).
These load the above raw images from fixed partitions, not from `/vendor/firmware`.

### 1.7 UFS storage firmware — `odm/firmware/ffu_bin/` (5,373,952 bytes)

Eight Samsung FFU (Firmware File Update) images, each **671,744 bytes**, header magic at 0x00 = `53 4d 49 20` → `SMI `, model `SM2754BB` at 0x04, date string `15042021` at 0x28:

| File | Bytes |
|---|---|
| `FEUDNN128G_C2G072+Jbl+C2G072+0x3030313300000000+0x3030313800000000.bin` | 671,744 |
| `FEUDNN128G_C2G072+Jbl+C2G072+0x3030313400000000+0x3030313800000000.bin` | 671,744 |
| `FEUDNN128G_C2G072+Jbl+C2G072+0x3030313600000000+0x3030313800000000.bin` | 671,744 |
| `FEUDNN128G_C2G072+Jbl+C2G072+0x3030313700000000+0x3030313800000000.bin` | 671,744 |
| `FEUDNN256G_C2G074+Jbl+C2G074+0x3030313300000000+0x3030313800000000.bin` | 671,744 |
| `FEUDNN256G_C2G074+Jbl+C2G074+0x3030313400000000+0x3030313800000000.bin` | 671,744 |
| `FEUDNN256G_C2G074+Jbl+C2G074+0x3030313600000000+0x3030313800000000.bin` | 671,744 |
| `FEUDNN256G_C2G074+Jbl+C2G074+0x3030313700000000+0x3030313800000000.bin` | 671,744 |

These are **UFS device firmware update packages** (128 GB / 256 GB Samsung UFS), not SoC firmware. They are not needed to boot.

### 1.8 Camera / ISP firmware (607,561 bytes)

`/home/berucrypt00/otadump/vendor/etc/imgsensor/mt6881/` — 12 raw sensor tuning/firmware blobs:

| File | Bytes |
|---|---|
| `imx06c_mipi_raw.bin` | 152,016 |
| `imx989_mipi_raw.bin` | 132,770 |
| `s5khp9sp_mipi_raw.bin` | 63,077 |
| `imx06clite_mipi_raw.bin` | 57,820 |
| `ov48b_mipi_raw.bin` | 37,599 |
| `ov64b_mipi_raw.bin` | 36,135 |
| `s5kjn1_mipi_raw.bin` | 32,885 |
| `s5khp9splite_mipi_raw.bin` | 31,702 |
| `s5k3p9sp_mipi_raw.bin` | 17,091 |
| `imx758_mipi_raw.bin` | 17,029 |
| `imx598_mipi_raw.bin` | 16,122 |
| `s5kjn1lite_mipi_raw.bin` | 13,315 |

Note: directory is named `mt6881` even though the platform is `mt6789`. Also
`vendor/etc/default_pd_calibration.bin` (phase-detection calibration) and
`vendor/firmware/sensor -> /vendor/etc/imgsensor/mt6789` — **the symlink is dangling**
(the target `mt6789` does not exist in the dump; only `mt6881` exists).

---

## 2. GPU deep dive

**Verified facts:**

- GPU driver sources embedded in the module point to
  `vendor/mediatek/kernel_modules/gpu/gpu_mali/mali_avalon/a16w_jm/...` — architecture family "Avalon", variant `a16w_jm`, platform `mt6789` (e.g. `.../platform/mt6789/mali_kbase_config_mt6789.c`).
- Version string from `strings vendor/lib64/egl/mt6789/libGLES_mali.so`:
  - `1.5 Valhall-"r54p1-12eac0"` (this is the driver/compiler build string for this library)
  - Also present: a generic GPU support table containing `Mali-G31`, `Mali-G310`, `Mali-G51`, `Mali-G510`, `Mali-G52`, `Immortalis-G715`; these are library capabilities, **not** proof of the on-die GPU model.
- `strings mali_kbase_mt6789_a16w_jm.ko` contains:
  - `request_firmware`, `release_firmware` (twice), `kbase_device_firmware_init_once`
  - `KBASE_TL_KBASE_CSFFW_FW_RELOADING`, `..._FW_ENABLING`, `..._FW_REQUEST_SLEEP`, `..._FW_REQUEST_WAKEUP`, `..._FW_REQUEST_HALT`, `..._FW_DISABLING`, `..._FW_OFF`, `KBASE_TL_KBASE_CSFFW_TLSTREAM_OVERFLOW`
  - Human strings: `CSF FW is being reloaded`, `CSF FW is being enabled`, `CSF FW sleep is requested`, `CSF FW wake up is requested`, `CSF FW halt is requested`, `CSF FW is being disabled`, `CSF FW is off`
- `images/vendor_boot.img` contains the byte sequence `sffw.bin` at file offsets **29,682,356** and **32,891,762**, with bytes `73 66 66 77 2e 62 69 6e` (`sffw.bin`). No `.bin` file with this name exists in `vendor/firmware`, `odm/firmware`, or anywhere else in the dump.
- No file matching `*mali*.bin`, `*csf*fw*`, `*gpu*.bin` exists (verified with `find`).
- `vendor/etc/ueventd.rc:176-177` creates `/dev/mali0` with mode 0666.
- `vendor/etc/init/hw/init.mt6789.rc:331-334` has commented-out manual insmod lines; the driver is actually loaded via `modules.load` (`mali_kbase...`, `mali_prot_alloc...`, `mali_mgm...` all present).

**Inference (not verified):** the `sffw.bin` string is most likely the tail of a CSF firmware filename such as `csffw.bin` or `mali_csffw.bin`, requested at runtime from a firmware search path. Because the file is absent from this dump, either (a) the firmware is embedded elsewhere in the vendor_boot ramdisk as a compressed payload, (b) it is supplied from a path not included in the dump, or (c) this driver revision falls back to a no-firmware path. This must be resolved on-device (e.g. `strace`/`grep` of `/sys/firmware` or `dmesg` for `request_firmware` failures) before the XNU port can assume a GPU blob is required.

No `RO_ROM` / `mali_rom` / `valhall` binary header bytes were found because no separate GPU binary exists to dump. The only GPU-related version bytes obtained are the ASCII strings above.

---

## 3. Wi-Fi / CONSYS deep dive

**Verified facts:**

- Chip family is "soc2_2" (MediaTek connectivity combo), firmware in two variants:
  - **1d = MT6631** — build tag in `soc2_2_ram_bt_1d_blr_1_hdr.bin`:
    `BABEFACEBABEFACE_NEPTUNE_BORA_CI_BUILD_TAG_INFO_ = t-neptune-main-soc2_2-2118-MOET_SOC2_2_E1_MT6631_ASIC_MINIROM_BLR-20260108192854`
  - **1e = MT6635** — build tag in `soc2_2_ram_bt_1e_1_hdr.bin`:
    `BABEFACEBABEFACE_NEPTUNE_BORA_CI_BUILD_TAG_INFO_ = t-neptune-main-soc2_2-2118-MOET_SOC2_2_E1_MT6635_ASIC_MINIROM-20260206152854`
- Header bytes of the `_hdr.bin` files (first 32 bytes, verified by `xxd`):

| File | First 16 bytes (ASCII) | Magic @0x10 |
|---|---|---|
| `soc2_2_ram_wifi_1d_1_hdr.bin` | `32 36 30 31 30 39 31 31 32 39 34 39 30 30 30 30` → `2601091129490000` | `414c 5053` → `ALPS` |
| `soc2_2_ram_wifi_1e_1_hdr.bin` | `2602061528540000` | `ALPS` |
| `soc2_2_ram_mcu_1d_blr_1_hdr.bin` | `2601081928540000` | `ALPS` |
| `soc2_2_ram_mcu_1e_1_hdr.bin` | `2602061528540000` | `ALPS` |
| `soc2_2_ram_bt_1d_blr_1_hdr.bin` | `3230 3236 3031 3038 3139 3334 3138 0000` → `20260108193418` | `ALPS` |
| `soc2_2_ram_bt_1e_1_hdr.bin` | (build tag carries `20260206152854`) | `ALPS` |

  Decoded build timestamps: WiFi 1d = 2026-01-09 11:29:49; WiFi 1e / MCU 1e = 2026-02-06 15:28:54; MCU 1d = 2026-01-08 19:28:54; BT 1d = 2026-01-08 19:34:18.
- `WIFI_RAM_CODE_soc2_2_1d_1.bin` and `_1e_1.bin` both start with
  `d0 79 cf 53 4c d3 7d 40 74 51 2e e5 d8 31 b4 a7` — high-entropy data (encrypted/compressed RAM code), no ASCII header. `file` reports plain `data`.
- Strings in Wi-Fi patch files include MediaTek source paths:
  `../wifi/open_corebase/base/talos/hal/hal_wmt_wf_cfg.c`,
  `../wifi/base/talos/emi/hal_cal_flow_rom.c`,
  `../wifi/base/talos/emi/hal_cal_manu_rom.c`,
  `../wifi/base/talos/emi/hal_phy_rom.c`, and
  `CAL: 6635 Rom_WFPath(0x%x)...` (in the 1e file, confirming MT6635).
- MCU strings: `driver/rom/gpt.c`, `system/rom/cos/cos_api.c`, `GPSL5 MCUB handler`, `fm/rom/fm_task.c`, `wmt/coex/core/sys_patch_coex_pta5.c`, `@@project/soc2_2/system/ROM/common.c`.
- Platform properties (`vendor/build.prop`):
  - `ro.board.platform=mt6789`
  - `ro.vendor.connsys.dedicated.log.port=bt,wifi,gps,mcu`
  - `persist.vendor.connsys.chipid=-1` (auto-detect at runtime)
  - `persist.vendor.connsys.patch.version=-1`
  - `persist.vendor.connsys.fm_chipid=mt6631_6635`
  - `ro.vendor.fm.platform=mt6631_6635`
  - `ro.vendor.wlan.gen=gen4m_6789`
- `vendor/bin/wmt_launcher` expects patch names `connsys_mt6835_mt6631.bin` and `connsys_mt6835_mt6635.bin` — **neither file exists in the dump**. It is launched with `-p /vendor/firmware/`.
- `vendor_dlkm/lib/modules/wlan_drv_gen4m_6789.ko` contains the literal string `WIFI_RAM_CODE_soc2_2` (verified by grep) and filename format strings `%s.bin`, `%s_%s_%u.bin`, `%s_%u%s_1.bin`, `mtsoc1_0_patch_e%x_hdr.bin`, `mt%x_patch_e%x_hdr.bin` — i.e. firmware names are partly constructed at runtime.

**Inference:** the device auto-detects MT6631 vs MT6635 (`chipid=-1`) and ships both firmware sets; the X6886 most likely uses the **1d / MT6631** set because `vendor/firmware/BT_FW.cfg` says `# connac1 6631 setting` and 1d carries the `BLR` (BT linear repeater?) tag. Confirm on-device with `getprop persist.vendor.connsys.chipid` or `dmesg | grep -i connsys`.

---

## 4. Audio deep dive

**Verified facts — Awinic (`aw87xxx` smart PA):**

- `odm/firmware/aw87xxx_acf.bin` (5,988 bytes), first 96 bytes include:
  - `08 f9 15 0a` magic at 0x00
  - `X6886_voice15dB` at 0x08
  - `Transmission` at 0x1c
  - `aw87391` at 0x58
  - Strings list profiles: `Bot_Right`, `Bot_Right_Bypass`, `Low_pwr`, `MMI_ALL`, `MMI_Bypass`, `Music`, `Receiver` — all tagged `aw87391`. Header magic `08f9150a` is shared by all four Awinic blobs.
- `odm/firmware/awinic_params.bin` (41,288 bytes): magic `08 f9 15 0a`, tag `CL6`, `Transmission`, `awinic`, profiles `Music`, `Ring`, `Voip`.
- `odm/etc/vconfig/audio/pa_param/AW_DSP.bin` (43,628 bytes): magic `08 f9 15 0a`, tag `X6886`, `awinic`, profiles `Music`, `Voip`, `Ring`.
- `odm/etc/vconfig/audio/pa_param/AW_SINWAVE_DSP.bin` (14,596 bytes): magic `08 f9 15 0a`, tag `SKTune`, `awinic`, profile `default`.
- `vendor_dlkm/lib/modules/snd-soc-aw87xxx.ko` explicitly references `aw87xxx_acf.bin`, `awinic_monitor.bin`, and chip id `aw87360`; strings include `[Awinic] [%s]%s: hdr_cersion[0x%x] parse failed`, `aw87xxx_algo_auth_misc_open`.
- `awinic_monitor.bin` is referenced by the driver but **not present** in the dump.

**Verified facts — MT6366:**
- Modules `mt6789-mt6366.ko` and `snd-soc-mt6366.ko` load at boot (`modules.load`), but no `.bin` firmware name appears in either. MT6366 needs no external firmware.

**Inference:** the Awinic blobs are device-specific calibration/PA profiles keyed by project name `X6886` and amp model `aw87391`. They are required only for speaker/earpiece amplification; audio without them may still enumerate but will mis-configure the PA.

---

## 5. What actually gets loaded at boot (cross-reference)

**Verified references from init rc / cfg:**

| Source | Reference | Meaning |
|---|---|---|
| `system/system/etc/ueventd.rc:4` and `vendor_boot/ramdisk/system/etc/ueventd.rc:4` | `firmware_directories /etc/firmware/ /odm/firmware/ /vendor/firmware/ /firmware/image/` | Kernel firmware loader search path |
| `vendor/etc/init/hw/init_connectivity.rc:16` | `service wmt_launcher /vendor/bin/wmt_launcher -p /vendor/firmware/ -o 1` | CONSYS Wi-Fi/BT/FM/GPS firmware loaded from `vendor/firmware` at `class early_hal` |
| `vendor/etc/init/hw/factory_init.connectivity.rc:15` and `meta_init.connectivity.rc:17` | same launcher, `-p /vendor/firmware/` | Factory/meta variants |
| `vendor/etc/init/hw/init_connectivity.rc:12`, factory/meta | `service wmt_loader /vendor/bin/wmt_loader` | CONSYS loader daemon |
| `vendor/etc/init/hw/meta_init.connectivity.rc` | `insmod wmt_drv.ko`; on `vendor.connsys.driver.ready=yes`: `insmod wmt_chrdev_wifi.ko`, `insmod wlan_drv_${ro.vendor.wlan.gen}.ko` | = `wlan_drv_gen4m_6789.ko` |
| `vendor/etc/init.insmod.mt6789.cfg` | `modprobe|*` then `setprop|vendor.all.modules.ready` | **all** modules in the module dir are loaded at init |
| `vendor/etc/init.insmod.touch.cfg` | explicit `modprobe|gt9896s.ko`, `ft3518u.ko`, `nt36672c.ko`, `ts_scp_common.ko`, etc. | touch drivers; firmware fetched by the drivers |
| `vendor/etc/init/hw/init.mt6789.rc:300-320` | SCP/ADSP device nodes and logs (`/dev/scp`, `/dev/adsp`, `/dev/adsp_0`, `/dev/adsp_1`, `/dev/audio_scp`) | SCP/ADSP runtime present |
| `vendor/etc/init/hw/init.mt6789.rc:331-334` | `#insmod /vendor/lib/modules/mali.ko` (commented out) | legacy line; real load via `modules.load` |
| `vendor/etc/init/hw/init.mt6789.rc:474,487-491` | display / `lcm_node` sysfs nodes | panel driver in kernel, not a firmware blob |
| `vendor/etc/init/trustonic.rc:32` | `-r /apex/com.mediatek.oemcrypto/firmware/tee/40188311faf343488db888ad39496f9a.drbin` | TEE/OEMCrypto trusted app (in apex, not in dump) |
| `vendor/etc/init/nfc-service-st.rc:5-10` | ST21NFC driver noted for X6851-W | matches `st21nfc_fw7.bin` |
| `odm/etc/init/vendor.goodix.rc` | `/dev/goodix_fp` | Goodix fingerprint sensor |

**Verified firmware-name strings inside kernel modules (`request_firmware` targets):**
`aw87xxx_acf.bin`, `awinic_monitor.bin`, `chipsemi_ts_fw.bin`, `gt9896s_fw_update_ctrl.fw`, `goodix_fw_update_thread.fw`, `tran_thp_get_fw_ver.fw`, plus generic `rtl_nic/rtl*.fw` and `FirmwareChecked{0,1,8,9}.bin` markers.

**Conclusion for XNU:** Linux boots with (a) raw coprocessor images from `images/` loaded by preloader/LK, and (b) `wmt_launcher` + driver-driven `request_firmware` calls hitting `/vendor/firmware` and `/odm/firmware`. The XNU port must either replicate these load paths or pre-load the blobs into the corresponding controllers before handing off.

---

## 6. Totals and largest files

| Group | Files | Bytes |
|---|---:|---:|
| `vendor/firmware` | 77 | 7,324,386 |
| `odm/firmware` (incl. ffu_bin) | 17 | 5,840,343 |
| `images/` | 26 | 294,443,056 |
| `vendor/etc/imgsensor/mt6881` | 12 | 607,561 |
| `odm/etc/vconfig/audio/pa_param` | 2 | 58,224 |
| **Grand total** | **134** | **308,273,570** (~293.9 MiB) |

Firmware dirs only (excluding `images/`): 108 files, **13,830,514 bytes** (~13.19 MiB).

`vendor/firmware` breakdown:
- Wi-Fi/CONSYS (8 files): 3,684,620
- Goodix touch (40 files): 1,851,110
- Novatek touch (4): 557,056
- SEC_THN31 (2): 888,766
- firmware_slm_567 (1): 134,144
- ST21NFC (1): 190,200
- FM radio (16): 16,960
- remoteproc_scp (1): 32
- cfg text (4): 1,498
- (sum = 7,324,386)

`odm/firmware` breakdown:
- ffu_bin (8): 5,373,952
- focaltech + chipsemi touch (2): 380,076
- awinic (2): 47,276
- Conf_MultipleTest.ini: 37,773
- cfg text (4): 1,266
- (sum = 5,840,343)

**Largest files overall:**
1. `images/vendor_boot.img` — 67,108,864
2. `images/boot.img` — 67,108,864
3. `images/md1img.img` — 55,494,608
4. `images/modem.img` — 54,434,080
5. `images/vendor_boot-debug.img` — 31,354,880
6. `images/dtbo.img` — 8,388,608
7. `images/lk.img` — 3,148,992
8. `images/gz.img` — 2,800,880
9. `images/tee.img` — 1,067,584
10. `vendor/firmware/WIFI_RAM_CODE_soc2_2_1d_1.bin` — 712,676 (largest non-`images/` blob)

---

## 7. Minimal firmware set needed to reach a display (inference)

**Verified constraints:** the display panel config lives in `odm/etc/vconfig/display/` (nt37706a / vtdr6126a, 144 Hz) and is **not** a firmware blob; the panel/LCM driver is compiled into the kernel/vendor modules. The boot logo is `images/logo.img`.

Minimal boot-to-display set (separating "must have" from "not needed"):

| Priority | Item | Bytes | Why |
|---|---|---:|---|
| Must | `images/preloader.img` (or emmc/ufs variant) | 387,056 | DRAM init + loads LK |
| Must | `images/lk.img` | 3,148,992 | bootloader, loads kernel/DT, starts TrustZone |
| Must | `images/gz.img` | 2,800,880 | GenieZone — required before Android/Linux kernel boots |
| Must | `images/tee.img` (header `atf`) | 1,067,584 | ATF/TEE — secure boot chain |
| Must | `images/scp.img` | 546,272 | SCP controls clocks/power; display clock/power requests go through it |
| Must | `images/spmfw.img`, `images/sspm.img`, `images/mcupm.img` | 14,752 / 138,112 / 129,440 | power management coprocessors |
| Must | `images/dtbo.img` | 8,388,608 | panel + display hardware description |
| Must | `images/boot.img` / `images/vendor_boot.img` | 67,108,864 each | kernel + ramdisk |
| Optional for logo | `images/logo.img` | 442,688 | boot logo |
| GPU | **no separate GPU blob found** | 0 | driver loads from `.ko` + unknown runtime firmware (§2) |
| Not needed | `images/md1img.img`, `images/modem.img`, `images/dpm.img`, `images/tkv.img`, `images/pi_img.img` | >110 MB | modem/trust apps, unrelated to display |
| Not needed | all Wi-Fi/BT/FM CONSYS blobs (3.68 MB) | — | connectivity, not display |
| Not needed | Awinic audio blobs, touch blobs, imgsensor blobs, FFU | — | audio/input/camera/storage |

So the **firmware-specific payload needed for display is roughly 4.7 MB** (gz + atf + scp + spmfw + sspm + mcupm) on top of the boot/DT images. Touch firmware is only needed if "boot to display" includes a usable input device; the Goodix `gt9896s_firmware_6789v01.bin` + `gt9896s_cfg_6789v01.bin` pair (96,512 + 1,104 bytes) is the likely candidate for this SoC, but that is an inference — the driver supports many panel variants and auto-detects.

---

## 8. Unresolved / needs on-device verification

1. **GPU CSF firmware file name and presence** — `sffw.bin` string in vendor_boot but no matching file. Determine actual `request_firmware` name from `dmesg` or by instrumenting the loader.
2. **CONSYS variant actually used** — 1d/MT6631 vs 1e/MT6635. `persist.vendor.connsys.chipid=-1` means runtime detection. `connsys_mt6835_mt6631.bin` / `_mt6635.bin` patch names expected by `wmt_launcher` are absent from the dump; their relationship to the shipped `soc2_2_ram_*` / `WIFI_RAM_CODE_*` files needs confirming.
3. **`firmware_slm_567.bin`** — ARM Cortex-M binary, purpose unknown. The only `slm` reference found is an SELinux property context comment for a different Infinix model (X6887O1AEE-888). Could be a sensor-hub/algorithm core; do not assume.
4. **`SEC_THN31_FW_VTP*.txt.bin`** — high-byte text (~440 KB each), likely a vendor configuration table (possibly fingerprint/touch related given `/dev/goodix_fp`), but no code reference was found in rc/cfg. Treat as unknown.
5. **`awinic_monitor.bin`** — referenced by `snd-soc-aw87xxx.ko`, not present in dump.
6. **`vendor/firmware/sensor` symlink** targets `/vendor/etc/imgsensor/mt6789`, which does not exist in the dump (only `mt6881`). Camera firmware path is broken in this snapshot.
7. **`images/tee.img` header says `atf`** — verify whether this partition holds ATF BL31, a TEE OS, or both before assuming its role in an XNU boot chain.

---

### Verification notes

- All file sizes, hex dumps and strings quoted above were read directly from the dump during this analysis.
- `file(1)` misidentifies some blobs (e.g. Goodix firmware as "raw G3 FAX", Novatek as "aria2 control file", Awinic/SEC_THN31 as text); those classifications were **not** trusted — only raw bytes and cross-referenced strings are reported as fact.
- No files were modified, created or deleted under `/home/berucrypt00/otadump`. This report is the only file written.
