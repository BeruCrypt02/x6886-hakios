# Optical Fingerprint (FOD) — Infinix X6886 stock dump analysis

Analysis date: 2026-11-23
Source: `/home/berucrypt00/otadump` (read-only). FDT cross-check: `/home/berucrypt00/hakios-x6886/devices/x6886/evidence/fdt.dts`.
Tools used: `file`, `modinfo`, `readelf`, `strings`, `xxd`, `grep`, `find`, `objdump` (symbols only).
No files in the dump were modified. No device interaction was performed.

Legend: **[V]** = verified directly from dump evidence. **[I]** = inference / interpretation.

---

## 1. Device-tree evidence (live FDT decompile)

### 1.1 SPI child node — the actual sensor endpoint

**[V]** `fdt.dts:4558-4591` — node `/soc/spi3@11013000/fingerprint@0`:

```
fingerprint@0 {
    compatible = "tran_fp_legacy";
    reg = <0x00>;                         /* CS 0 */
    spi-max-frequency = <0x19bfcc0>;      /* 27,000,000 Hz = 27 MHz */
    interrupt-parent = <0x49>;
    interrupts = <0x0d 0x01 0x0d 0x00>;
    debounce = <0x0d 0x00>;
    irq-gpio = <0x0d>;                    /* GPIO 13 */
    irq-gpio-std = <0x49 0x0d 0x00>;
    reset-gpio = <0x77>;                  /* GPIO 119 */
    reset-gpio-std = <0x49 0x77 0x00>;
    tran,gpio_pwr = <0x3b>;               /* GPIO 59 */
    tran,gpio_pwr-std = <0x49 0x3b 0x00>;
    fod_location_xy = "540,2210";
    tran_fp_sched;
    goodix;                               /* <-- vendor flag */
    pinctrl-names = "tran_fp_default\0tran_fp_pin_irq\0tran_fp_irq_pull_down\0"
                    "tran_fp_reset_high\0tran_fp_reset_low\0tran_fp_spi_default";
    pinctrl-0 = <0x205>; pinctrl-1 = <0x206>; pinctrl-2 = <0x207>;
    pinctrl-3 = <0x208>; pinctrl-4 = <0x209>; pinctrl-5 = <0x20a>;
    status = "okay";
};
```

Parent controller: `spi3@11013000`, `compatible = "mediatek,mt6765-spi"`, `reg = 0x11013000..0x11013100`, parent phandle `0x16b`.

**[V]** `0x019bfcc0` = 27,000,000 decimal — confirms the 27 MHz SPI clock reported from the live device.

### 1.2 Other fingerprint nodes

**[V]** `fdt.dts:4605-4608` — `/soc/fingerprint` (phandle `0x16e`), `compatible = "mediatek,goodix-fp"`, no properties. Likely a legacy Goodix placeholder node, not bound by either `tran_fp*.ko` (their OF tables only list `tran_fp_legacy` / `tran_fp`).

**[V]** `fdt.dts:8128-8141` — a second `/soc/fingerprint` node (phandle `0x240`), `compatible = "tran_fp_legacy"`, carrying only interrupt (`<0x0d 0x01 0x0d 0x00>`), debounce and the same six `tran_fp_*` pinctrl states; `status = "okay"`. This is a platform-device instance of the same legacy driver (used by the HAL for FOD coordinate / sysfs access rather than SPI transfer, based on the driver's `tran_fp_parse_dts_file` + `fod_location_xy` sysfs code).

**[V]** `fdt.dts:9188` — alias `goodix_fp = "/soc/fingerprint"`.

**[V]** Unrelated but nearby: `focaltech,fod-function-support` (`fdt.dts:4508`) and `chipsemi,fod-function-support` (`:4525`) belong to the **touch panel** FOD gesture, not the fingerprint sensor. `ifod-threshold = <0xc8>` at `:8414` and `:8482` are also touch-panel thresholds. Do not confuse these with the optical sensor.

---

## 2. Kernel driver modules

### 2.1 File identity (both are plain, uncompressed ELF)

**[V]**

| Property | `tran_fp.ko` | `tran_fp_v2.ko` |
|---|---|---|
| Full path | `vendor_boot/ramdisk/lib/modules/tran_fp.ko` | `vendor_boot/ramdisk/lib/modules/tran_fp_v2.ko` |
| Size | 194,952 B (190.4 KiB) | 105,848 B (103.4 KiB) |
| `file` | ELF 64-bit LSB relocatable, ARM aarch64, not stripped | same |
| BuildID (SHA1) | `bd212ea43055f6c7049687552da915dcc655f388` | `a776132ac317cd46c2a619e4024478d464692847` |
| `name` | `tran_fp` | `tran_fp_v2` |
| OF alias | `of:N*T*Ctran_fp_legacy` (+ `C*`) | `of:N*T*Ctran_fp` (+ `C*`) |
| `depends` | `transsion_tranlog,mtk_disp_notify,spi-mt65xx` | `mtk_disp_notify,mcDrvModule,spi-mt65xx` |
| `description` | User mode SPI device interface | User mode SPI device interface |
| `license` | GPL | GPL |
| `built_with` | DDK | DDK |
| `parm` | `bufsiz` — data bytes in biggest supported SPI message | same |
| `vermagic` | `6.12.38-android16-5-gcc51d883045d-4k SMP preempt mod_unload modversions aarch64` | identical |
| Toolchain (`.comment`) | `Android (12833971, +pgo, +bolt, +lto, +mlgo, based on r536225) clang version 19.0.1` | identical |

**[V]** Both start with ELF magic `7f 45 4c 46`; `modinfo`/`readelf` work directly. **They are NOT compressed** — no gzip/xz/Zstd header, no decompression step needed. (For reference, a compressed module would start `1f 8b` → `gzip -dc`, or `fd 37 7a 58 5a` → `xz -dc`; neither applies here.)

**[V]** Kernel config (`kernel_config.txt`): `CONFIG_SPI=y`, `CONFIG_SPI_MASTER=y`, `CONFIG_SPI_MEM=y`, `CONFIG_REGMAP_SPI=y`, `CONFIG_SPI_SLAVE=y`, `CONFIG_SPI_DYNAMIC=y`. There is **no** `CONFIG_FINGERPRINT*`, no `CONFIG_TOUCHSCREEN_GOODIX*` (all `not set`), and `CONFIG_TEE is not set`. The fingerprint driver is an out-of-tree vendor module; the TEE driver is supplied as the vendor module `mcDrvModule.ko`.

### 2.2 Which module is actually used

**[V]** `vendor_boot/ramdisk/lib/modules/modules.load` lines 165-166 load **both**:
```
tran_fp_v2.ko
tran_fp.ko
```
**[V]** `modules.alias`: `alias of:N*T*Ctran_fp tran_fp_v2` (line 566) and `alias of:N*T*Ctran_fp_legacy tran_fp` (line 2045).
**[V]** `modules.dep`:
```
/lib/modules/tran_fp_v2.ko: /lib/modules/spi-mt65xx.ko /lib/modules/mcDrvModule.ko /lib/modules/tnek.ko /lib/modules/irq-dbg.ko /lib/modules/mtk_disp_notify.ko /lib/modules/transsion_tranlog.ko
/lib/modules/tran_fp.ko:    /lib/modules/spi-mt65xx.ko /lib/modules/irq-dbg.ko /lib/modules/mtk_disp_notify.ko /lib/modules/transsion_tranlog.ko
```

**[I]** The live FDT contains `compatible = "tran_fp_legacy"` and **no** `compatible = "tran_fp"` anywhere (grep of `fdt.dts` for `tran_fp` returns only `tran_fp_legacy`, `tran_fp_sched` and `tran_fp_*` pinctrl state names). Therefore `tran_fp.ko` is the module that probes and owns the sensor on this device. `tran_fp_v2.ko` is loaded but has no matching device node — it is a second-generation implementation shipped in the common BSP for other SKUs. This is consistent with the runtime `/dev/goodix_fp` node created by the init rc (§4.1), which belongs to the legacy driver's Goodix branch.

### 2.3 `tran_fp.ko` — legacy / universal driver (ACTIVE)

**[V]** Key symbols (from `readelf -sW`):
- `tran_fp_probe`, `tran_fp_remove`, `tran_fp_ioctl` (11,056 bytes — by far the largest function), `tran_fp_compat_ioctl`, `tran_fp_open/release/poll/fasync`, `tran_fp_fops`, `tran_fp_pm_ops`.
- `tran_fp_parse_dts_file`, `tran_fp_get_power_val`, `tran_fp_resource_init`, `tran_fp_power_init`, `tran_fp_power_onoff`, `tran_fp_open_clock/close_clock`, `tran_fp_set_clock`, `tran_fp_reset_sensor`, `tran_fp_shutdown_sensor`, `tran_fp_enable_irq/disable_irq`, `tran_fp_irq_init`, `tran_fp_input_init`, `tran_fp_report_key_nav_event`.
- `tran_get_fp_sensor_chip_id`, `tran_read_fp_chip_id`, `tran_get_fp_sensor_chip_id`, `chip_info`, `vendor_name`, `vendor id 0x%x`.
- `fod_location_xy_show` / `fod_location_xy_store` / `fod_location_xy_attr`, `tee_version`, `vendor_name_show/store`.
- SPI: `tran_fp_spi_transfer`, `tran_spi_transfer_p_arr`, `spi_sync`, `mt_spi_enable_master_clk`, `mt_spi_disable_master_clk`, `tran_spi_speed`.
- Multi-vendor per-sensor ops: `gf_spi_transfer` / `gf_fp_irq_init` / `gf_spi_read_bytes_ree` / `gf_fb_notifier_callback`; plus `egf_*`, `cf_*`, `cdf_*`, `sf_*`, `sif_*`, `ff_*`, `of_*`, `ftf_*`, `ancf_*` variants.
- Netlink: `tran_netlink_init/recv/send/destroy`; proc: `tran_proc_init/deinit`, `silfp_proc_*`.
- Display/HBM: `mtk_disp_notifier_register`, `tran_fasync_for_lcm_full_HBM`, `TRAN_FULL_HBM_EVENT=FULL_HBM_SET`, `SCREEN_STATUS=%s`, `SPI_STATE=finger`.

**[V]** Source path embedded in the module:
```
../kernel_device_modules-6.12/drivers/input/fingerprint/tran_common/tran_fp_main.c
```

**[V]** Chip/vendor names present in the binary (all supported by this one universal driver):
```
GOODIX_GF3956   GOODIX_GF3976   GOODIX_GF3986
GOODIX_GW9578   GOODIX_GW9598
GF3956  GF3976  GF3986
CHIPONE_ICNF7312BL / ICNF7316EL / ICNF7319AL / ICNF7332AL, CHIPONE
```
plus TEE identifiers: `goodix_tee`, `goodix_opt_tee`, `chipone_tee`, `silead_tee`, `silead_opt_tee`, `focaltech_tee`, `cdfinger_tee`, `fortsense_tee`, `anc_opt_tee`.
Also silead-style ioctl names: `SIFP_IOC_PROC_NODE`, `SIFP_IOC_PWDN`, `SIFP_IOC_SET_FEATURE`, `SIFP_IOC_WAKELOCK`, `IOC_SET_KMAP`, `IOC_GET_VER`; and `FF_IOC_RESET_DEVICE_HL`.

**[V]** Log/probe strings include `HW_FP Check ID Pass In Init`, `HW_FP Check ID Fail In Init`, `read sensor id success,rty = %d!`, `vendor id 0x%x`, `chip   : %s %s`, `id     : 0x0 lib:%s`, `vendor : fw:%s`, `firmware:%s`.

**[V]** `goodix` is a real boolean DT property in the active node, and the driver has a symbol `g_fp_vendor_id` plus `g_fp_name_flg` — consistent with selecting the `gf_*` handler set and creating the `goodix_fp` misc device.

### 2.4 `tran_fp_v2.ko` — second-generation driver (loaded, apparently unbound)

**[V]** Key symbols: `tran_fp_probe` (1,968 B), `tran_fp_ioctl` (3,400 B), `tran_fp_parse_dts_file` (1,456 B), `tran_fp_power_init`, `tran_fp_power_onoff`, `tran_fp_reset_gpio_low/high`, `tran_fp_report_key_nav_event`, `tran_fp_input_init`.
**[V]** Undefined symbols unique to v2: `set_tee_worker_threads_schedule` (called with `POLICY_LOW` / `POLICY_HIGH` per its log strings), `set_tee_worker_threads_on_big_core` semantics, plus `mtk_disp_notifier_unregister`.
**[V]** Debug strings enumerate a clean ioctl API:
```
TRAN_IOC_INIT, TRAN_IOC_EXIT, TRAN_IOC_CHIP_INFO, TRAN_IOC_GET_FW_INFO,
TRAN_IOC_ENABLE_IRQ / DISABLE_IRQ, TRAN_IOC_ENABLE_POWER / DISABLE_POWER,
TRAN_IOC_ENABLE_SPI_CLK / DISABLE_SPI_CLK, TRAN_IOC_ENTER_SLEEP_MODE,
TRAN_IOC_INPUT_KEY_EVENT, TRAN_IOC_NAV_EVENT, TRAN_IOC_REMOVE
```
**[V]** Other strings: `common_goodix`, `GF3656`, `GF3976`, `GF3986`, `tran_tee_afinity`, `TRAN_FP_TEE_AFINITY`, `firmware info  0x%x`, `nlh->nlmsg_pid`, `[tran] set spi default state success!`.
**[V]** No `gf_spi_transfer`/`gf_fp_irq_init` family — v2 is a smaller, single-driver design that relies on the `mcDrvModule` TEE path directly.

### 2.5 tran_fp vs tran_fp_v2 — summary of differences

**[V]/[I]**

| Aspect | `tran_fp.ko` (active) | `tran_fp_v2.ko` |
|---|---|---|
| OF compatible | `tran_fp_legacy` ✅ matches FDT | `tran_fp` ❌ absent from FDT |
| Size | 194,952 B | 105,848 B |
| Architecture | One universal driver with ~10 vendor-specific handler families (gf/egf/cf/sf/ff/anc/sif/...) | Single streamlined driver |
| TEE dependency | none in `depends` | `mcDrvModule` (MobiCore) + `set_tee_worker_threads_schedule` |
| Device node | `/dev/goodix_fp` (Goodix branch), also `/dev/spidev0.0` string present | `/dev/tran_fp` |
| ioctl API | multi-vendor numeric ioctls (`FF_IOC_*`, `SIFP_IOC_*`, etc.) | uniform `TRAN_IOC_*` names |
| HBM integration | `tran_fasync_for_lcm_full_HBM`, disp notifier | disp notifier + TEE affinity knobs |
| Sysfs | `/sys/kernel/tran_fp/{vendor_name,fod_location_xy,tee_version}` | same `fod_location_xy` attribute |

---

## 3. Chip identity / Goodix part number

**[V]** The dump contains **three** Goodix optical-FOD TA images whose UUIDs encode part numbers (see §4), and the drivers/HALs list these Goodix parts:
```
GF3956, GF3976, GF3986, GW9578, GW9598
```
**[V]** Strongest single-SKU hint: `vendor/lib64/libtran_hal.so` contains the literal strings
```
[%s] T_GF3976
GF3976   Goodix
Goodix   GF3976   Goodix
GF3656   Goodix
GF3986   Goodix
```
together with Focaltech `FT9391`, Silead `GSL6195`, FPC `FPC1561` — i.e. a compiled-in sensor-type table where GF3976 is repeated as the primary Goodix type.
**[V]** The FDT itself gives no part number — only the generic `goodix;` flag. The kernel reads the silicon ID at runtime (`tran_get_fp_sensor_chip_id` / `tran_read_fp_chip_id`, HAL `getVendorChipId`, log `getVendorChipId success!` / `chip_id invalid, sensor is broken.`).

**[I]** Best candidate for the fitted sensor is **Goodix GF3976**; GF3956 and GF3986 TAs are also shipped for multi-SKU/board compatibility, and GW9578/GW9598 names in the universal driver cover other possible Transsion boards. This cannot be raised above "probable" from static dump evidence alone — a live `getprop`/`dmesg` or a TA-open log would settle it.

**[V]** The spec PDF calling it `tran_keys` is a Transsion internal name; the actual OS-facing identity is Goodix (`goodix;` in DT, `/dev/goodix_fp`, `gf_fingerprint.default.so`).

---

## 4. Firmware: TEE trustlets, not request_firmware blobs

### 4.1 No conventional fingerprint `.bin` in firmware dirs

**[V]** Recursive search of `odm/firmware/` and `vendor/firmware/` found **no** Goodix/fingerprint firmware file (only touch-controller firmware `focaltech_ts_fw.bin`, `chipsemi_ts_fw.bin`, `gt9896s_*`, audio/WiFi/audio-codec blobs). The `firmware:%s` / `FIRMWARE=` / `firmware info 0x%x` strings in the driver/HAL are version log messages, not `request_firmware()` paths.

### 4.2 MobiCore trustlets (`.tabin`)

**[V]** Fingerprint "firmware" lives as secure-world **MobiCore TAs** (GlobalPlatform-style TEE applets) in two mcRegistry directories. All relevant files begin with the **MCLF** container magic:

```
offset 0x00: 4d 43 4c 46   "MCLF"
offset 0x04: 05 00         format/version fields (0x0005)
offset 0x06: 02 00         (0x0002)
offset 0x08: 38 00 / 3c 00 header size (0x38=56 or 0x3c=60)
offset 0x0c: 02 00
offset 0x10: 03 00
offset 0x14: 01 00
offset 0x18: 16-byte TA UUID  <-- matches the filename
offset 0x28: 00 10 00 00 (0x00001000 = 4096)
```

**[V] `/odm/app/mcRegistry/` — the path explicitly referenced by the Goodix HAL** (`libgf_hal.so` string: `[%s] add open-session ta path /odm/app/mcRegistry`, and `TEEC_TT_SetLocalRegistryPath` import):

| File | Size (B) | UUID bytes at 0x18 | Header size |
|---|---|---|---|
| `05060000000000000000000000000000.tabin` | 12,142,443 | `05 06 00 00 00...00` | 0x38 |
| `05070000000000000000000000000000.drbin` | 5,667 | `05 07 00 00...` | 0x3b |

Hexdump of the ODM generic TA:
```
00000000: 4d43 4c46 0500 0200 3800 0000 0200 0000  MCLF....8.......
00000010: 0300 0000 0100 0000 0506 0000 0000 0000  ................
00000020: 0000 0000 0000 0000 0000 0000 0100 0000  ................
00000030: 0010 0000 1088 7500 00b0 7500 50bd 4300  ......u...U...P.C
```

**[V] `/vendor/app/mcRegistry/` — chip-specific Goodix FOD TAs:**

| File (UUID = `NNNN…`) | Encoded part | Size (B) | Header | UUID bytes |
|---|---|---|---|---|
| `05063956000000000000000000000000.tabin` | GF3956 | 5,740,443 | 0x3c | `05 06 39 56` |
| `05063976000000000000000000000000.tabin` | GF3976 | 6,628,299 | 0x3c | `05 06 39 76` |
| `05063986000000000000000000000000.tabin` | GF3986 | 6,720,835 | 0x3c | `05 06 39 86` |
| `05063656000000000000000000000000.tabin` | GF3656 | 5,954,123 | 0x3c | `05 06 36 56` |
| `05060000000000000000000000000000.tabin` | generic | 12,090,819 | 0x3c | `05 06 00 00` |

Exact 128-byte dump of the GF3976 TA:
```
00000000: 4d43 4c46 0500 0200 3c00 0000 0200 0000  MCLF....<.......
00000010: 0300 0000 0100 0000 0506 3976 0000 0000  ..........9v....
00000020: 0000 0000 0000 0000 0000 0000 0100 0000  ................
00000030: 0010 0000 e043 2300 0070 2300 e0dd 4100  .....C#..p#...A.
00000040: a860 0700 d010 0000 0000 0000 ffff ffff  .`..............
00000050: ffff ffff 0000 0000 0000 0000 0000 0000  ................
00000060: 0100 0000 0000 0000 0000 0000 0000 0000  ................
00000070: 0000 0000 0000 0000 0000 0000 0000 0000  ................
```
GF3956 and GF3986 have the same header layout with UUID bytes `05 06 39 56` / `05 06 39 86` respectively.

**[V]** `strings` on the TA payload yields only apparent ARM machine code/garbage, not plaintext version strings. The version is obtained at runtime: `libgf_hal.so` has `GdxGetWBVersionNum`, `[%s] Version = %08x`, `[%s] hal_version: %s`; `libtran_hal.so` has `[%s] TA_VERSION %s`, `getFirmwareInfo` and the kernel ioctl `TRAN_IOC_GET_FW_INFO`.

**[I]** The TA payloads are encrypted/obfuscated secure-world binaries (MCLF container). Matching algorithm, templates and image-processing code are inside them, not in the HAL or kernel.

---

## 5. Init rc, VINTF, HAL and framework

### 5.1 `odm/etc/init/vendor.goodix.rc` (483 B) — full content

```
on post-fs-data
    chmod 0660 /dev/goodix_fp
    chown system root /dev/goodix_fp

    mkdir /data/vendor/goodix
    chmod 0777 /data/vendor/goodix
    chown system system /data/vendor/goodix

    mkdir /data/vendor/goodix/gf_data
    chmod 0777 /data/vendor/goodix
    chown system system /data/vendor/goodix/gf_data

    chmod 0777 /proc/ppm/enabled
    chmod 0777 /proc/ppm/policy/ut_fix_core_num
    chmod 0777 /proc/ppm/policy/ut_fix_freq_idx
```

**[V]** Device node: `/dev/goodix_fp`, mode 0660, owner `system:root` — fingerprint matches the active `tran_fp.ko` Goodix branch. `/data/vendor/goodix/gf_data` is Goodix template/calibration storage. The `/proc/ppm/*` 0777 grants the HAL fingerprint-boost access to MediaTek performance manager.

### 5.2 `odm/etc/init/fingerprint-default.rc` (379 B) — full content

```
service vendor.fingerprint-default /vendor/bin/hw/android.hardware.biometrics.fingerprint-service default
    class late_start
    user system
    group system input
    interface aidl android.hardware.biometrics.fingerprint.IFingerprint/default

on post-fs-data
    mkdir /data/vendor/fingerprint/ 0771 system system
    mkdir /mnt/vendor/persist/fingerprint 0771 system system
```

**[V]** AIDL HAL, `late_start`, runs as `system` with `input` group (needed for nav-key input events from the sensor). Persistent template dir `/mnt/vendor/persist/fingerprint`.

### 5.3 `odm/etc/vintf/manifest/fingerprint-default.xml` (222 B) — full content

```xml
<manifest version="1.0" type="device">
    <hal format="aidl">
        <name>android.hardware.biometrics.fingerprint</name>
        <version>3</version>
        <fqname>IFingerprint/default</fqname>
    </hal>
</manifest>
```

**[V]** Fingerprint HAL is **AIDL v3**, `IFingerprint/default` (Android 16 / SDK 36). No HIDL fingerprint manifest present.

**[V]** Also present: `odm/etc/permissions/android.hardware.fingerprint.xml` (feature declaration).

### 5.4 `vendor/bin/hw/android.hardware.biometrics.fingerprint-service` (103,032 B)

**[V]** Stripped ARM64 PIE, BuildID (md5/uuid) `68bfd1eba9b35ab347e286a6ebf84122`, interpreter `/system/bin/linker64`.
NEEDED: `android.hardware.biometrics.fingerprint-V3-ndk.so`, `android.hardware.biometrics.common-V3-ndk.so`, `vendor.hardware.biometrics.extension.fingerprint-V1-ndk.so`, `android.hardware.biometrics.common.thread.so`, `libtranlog.so`, `libhardware.so`, `libbinder_ndk.so`, `libcutils.so`, `libutils.so`, `libbase.so`, `liblog.so`, `libc++`, `libc`, `libm`, `libdl`.
**[V]** Source paths embedded: `hardware/interfaces/biometrics/fingerprint/aidl/transsion/{main.cpp,Fingerprint.cpp,Session.cpp,ExSessionV2.cpp}` — a **Transsion-customised AIDL fingerprint service**.
**[V]** It calls `hw_get_module` and contains both module IDs plus variant selection:
```
tran_fingerprint
gf_fingerprint
fingerprint open hal the varient_key is: %s
/default
```
plus props `vendor.fp.transsion.lcmname`, `vendor.tran.fp.center.X.offset`, `vendor.tran.fp.center.Y.offset`, `vendor.tran.fp.status`, `vendor.fingerprint.init.error`, and data files `/data/vendor/tran_fp/fp_history.txt`, `/data/vendor/tran_fp/fp_uid.txt`, `/sys/kernel/tran_display/lcm_name`.

### 5.5 Actual HAL modules (the service loads one by variant key)

**[V]**

| File | Size | NEEDED core | Device / ioctl family | Registry path strings |
|---|---|---|---|---|
| `odm/lib64/hw/gf_fingerprint.default.so` | 216,816 B | `libgf_hal.so`, `libvendor.transsion.hardware.biometrics.fingerprint.so` | `/dev/goodix_fp`, `GF_IOC_INIT/EXIT/REMOVE/ENABLE_POWER/DISABLE_POWER/ENABLE_IRQ/DISABLE_IRQ` | `/odm/app/mcRegistry` |
| `vendor/lib64/hw/tran_fingerprint.default.so` | 52,024 B | `libtran_hal.so`, `libion.so` | `/dev/tran_fp`, `TRAN_IOC_*` | `/odm/app/mcRegistry`, `/odm/etc/vconfig/tran_fp/` |

**[V] `odm/lib64/libgf_hal.so` (580,296 B)** — Goodix HAL logic. Plain strings include:
```
TEEC_InitializeContext / TEEC_OpenSession / TEEC_InvokeCommand /
TEEC_AllocateSharedMemory / TEEC_ReleaseSharedMemory /
TEEC_CloseSession / TEEC_FinalizeContext / TEEC_TT_SetLocalRegistryPath
[%s] add open-session ta path /odm/app/mcRegistry
GdxGetWBVersionNum
GF_CMD_SENSOR_INIT / CAPTURE_IMG / DETECT_SENSOR / PRE_DETECT / DATA / GET_IRQ_TYPE / SLEEP / WAKEUP
GF_CMD_AUTH_* , GF_CMD_ALGO_* , GF_CMD_SENSOR_*
GF_ERROR_SPI_RAW_DATA_CRC_FAILED , GF_ERROR_SPI_FW_DOWNLOAD_FAILED , GF_ERROR_SPI_CFG_DOWNLOAD_FAILED
/sys/kernel/tran_display/lcm_hbm_state , /sys/kernel/oplus_display/hbm
TRAN_FULL_HBM_EVENT= , FULL_HBM_SET , setHbmMode/openHbmMode
persist.vendor.fp.sensorUid , persist.vendor.sys.fp.dump_data , vendor.fp.QRCode
/mnt/vendor/persist/fpdata/ , /data/vendor/fpdump
GoodixFingerprintAIDLDaemon (binder service registration)
```
Source paths: `/home/fangshasha/project/fss_tran_licon/transsion_optical_project/customized/goodix/lincoln_public_standard/hal/{CoreCreator,ExtModuleCreator,CustomizedFingerprintCore}.cpp`.

**[V] `vendor/lib64/libtran_hal.so` (434,088 B)** — generic Transsion HAL logic, plain strings include:
```
TEEC_* (same seven calls as above)
/dev/tran_fp , ioctl , TRAN_IOC_* (see §2.4)
TRAN_CMD_INIT / CAPTURE_IMAGE / ENROLL / AUTH / ENUMERATE / REMOVE /
TRAN_CMD_SENSOR_BROKEN_CHECK / TRAN_CMD_SENSOR_GET_IRQ_TYPE / TRAN_CMD_SET_HMAC_KEY / ...
[%s] T_GF3976 , GF3976 , Goodix , GF3656 , GF3986 , FT9391 , GSL6195 , FPC1561
Using old TA UUID for icname=%s , tran_fp_use_old_uuid
ro.odm.tr_fingerprint.vendor.name , vendor.fp.type
/odm/etc/vconfig/tran_fp/ , %stran_fp_ext_config.xml , %stran_fp_%s_%s_%s_%s.json ,
%stran_fp_%s_%s.json , %stran_fp_project_info.json
spi_speed , spi_actual_high_speed , spi_high_speed , spi_transfer_align_packet_size ,
is_spi_miso_delay , spi_miso_delay , is_spi_miso_falling_edge_output ,
spi_miso_drive_strength_ma , system_spi_voltage , support_read_max_dma_size
vendor.tran.fp.center.X.offset , vendor.tran.fp.center.Y.offset ,
tran_fp_offset_x1..x3 / y1..y3 , sensor_center_x , sensor_center_y
/mnt/vendor/persist/fpdata/ , /data/vendor/tran_fp/ , /data/vendor/fpdump
set_tee_worker_threads_schedue , set_tee_worker_threads_on_big_core
```
**[V]** Build compiler stamps: gf/gf-fingerprint HAL uses clang 11.0.5 (r399163b) for the wrapper and clang 8.0.2 for libgf_hal; libtran_hal uses a newer LLVM (contains `TRAN_FP_*` AIDL v3 strings).

**[I] Which HAL is active:** the FDT `tran_fp_legacy` + `goodix;` flag, the `/dev/goodix_fp` node with 0660 permissions, and the presence of the large ODM Goodix TA strongly indicate the **Goodix stack is the live one**: `tran_fp.ko → /dev/goodix_fp → gf_fingerprint.default.so + libgf_hal.so → ODM Goodix TA`. The `tran_fingerprint.default.so` / `libtran_hal.so` path belongs to the `tran_fp_v2` stack and is likely selected by a vendor variant property on other SKUs. The service binary can load either, so this remains a strong inference, not absolute proof — a live `getprop | grep fp` would confirm.

---

## 6. TEE / where matching happens

**[V] Evidence that match/enroll is inside the TEE, not in the HAL or kernel:**

1. Both `libgf_hal.so` and `libtran_hal.so` import the full TEE Client API set: `TEEC_InitializeContext`, `TEEC_OpenSession`, `TEEC_InvokeCommand`, `TEEC_AllocateSharedMemory`, `TEEC_ReleaseSharedMemory`, `TEEC_CloseSession`, `TEEC_FinalizeContext`, `TEEC_TT_SetLocalRegistryPath`.
2. Goodix HAL: `[%s] TEEC_OpenSession failed result = 0x%x`, `[%s] TEEC_InitializeContext failed result = 0x%x`, and the TA registry path `/odm/app/mcRegistry`.
3. Transsion HAL: `[%s] TEEC_OpenSession success, result = 0x%x, retry = %d`, ` %s: TEEC_InvokeCommand failed (%08x), %d`, `[%s] TA tran_spi_init failed err=%d`, `[%s] TA_VERSION %s`, `tran_fp_load_ta_from_odm_path`.
4. TEE-side command enums are in the HAL (`GF_CMD_AUTH_*`, `GF_CMD_ALGO_*`, `TRAN_CMD_ENROLL/AUTH/...`) — i.e. the HAL sends high-level commands and gets results back; it never contains a matcher.
5. The kernel driver is only a transport: `tran_fp_spi_transfer`/`gf_spi_transfer` → `spi_sync`; its ioctls are power/clock/IRQ/FW-info/raw-image-move, not biometric matching.
6. MobiCore kernel driver `mcDrvModule.ko` is in `modules.load` and is a declared dependency of `tran_fp_v2.ko`. `kernel_config.txt` has `CONFIG_TEE is not set`, so TEE support is entirely vendor-supplied (MobiCore / `tenek.ko` / `ffa_v11.ko`, all in `modules.load`).
7. Template/persistent data lives in `/mnt/vendor/persist/fingerprint`, `/mnt/vendor/persist/fpdata/`, `/data/vendor/goodix/gf_data`, `/data/vendor/tran_fp/` — consistent with encrypted templates produced by a TA.

**[V]** `libgf_hal.so` also registers a `GoodixFingerprintAIDLDaemon` binder service for vendor extensions.

**[I]** Practical consequence for XNU: without a working MobiCore/TrustZone client and the TA, you cannot authenticate — the matching algorithm and enrolled templates are **not** recoverable from the Linux userspace blobs. The dump gives you transport + UI glue only.

---

## 7. SPI protocol / register / image-size observations

**[V] Available os:**
- Physical bus: `spi3@11013000` (MediaTek MT6765 SPI controller), CS 0, 27 MHz.
- GPIOs: IRQ 0x0d (13), reset 0x77 (119), power 0x3b (59).
- Kernel-side transfer: `spi_sync()` via `tran_fp_spi_transfer()` / `gf_spi_transfer()`; clock gating via `mt_spi_enable_master_clk` / `mt_spi_disable_master_clk`; speed variable `tran_spi_speed`, buffer parameter `bufsiz` ("data bytes in biggest supported SPI message").
- Configurable SPI tuning in `libtran_hal.so`: `spi_speed`, `spi_actual_high_speed`, `spi_high_speed`, `spi_transfer_align_packet_size`, `is_spi_miso_delay`, `spi_miso_delay`, `is_spi_miso_falling_edge_output`, `spi_miso_drive_strength_ma`, `system_spi_voltage`, `support_read_max_dma_size`.
- ioctl APIs: `TRAN_IOC_*` (v2), `GF_IOC_*` (Goodix HAL), `FF_IOC_RESET_DEVICE_HL`, `SIFP_IOC_*` (legacy generic driver).
- FOD coordinates: DT `fod_location_xy = "540,2210"`, exported through `/sys/kernel/tran_fp/fod_location_xy`; HAL offsets `vendor.tran.fp.center.X.offset`, `vendor.tran.fp.center.Y.offset`, `tran_fp_offset_x1..3`, `tran_fp_offset_y1..3`, `sensor_center_x/y`.

**[I]** 540 is exactly half of 1080, and 2210 is 190 px above a 2400-pixel bottom — consistent with an FOD circle centred horizontally at the bottom of a **1080×2400** panel. (The dump also contains `gt9896s_*_1080x2400.bin` touch firmware, supporting that panel geometry.)

**[I]** No Goodix command opcodes or sensor register addresses appear in the kernel module strings or exported symbols: this driver is a transparent SPI pipe. Capture/init/algorithm command bytes are constructed **inside the TA** and tunnelled via TEEC shared memory, so they are not statically visible in this dump. Do not expect to recover e.g. a `0x80xx` Goodix register map from these files. Extracting it would require decrypting the MCLF TA or running a live SPI logic analyser.

**[V]** HBM (high-brightness mode, required for optical capture): `mtk_disp_notifier_register`, `tran_fasync_for_lcm_full_HBM`, `TRAN_FULL_HBM_EVENT=FULL_HBM_SET`, `setHbmMode`/`closeHbmMode`, `/sys/kernel/tran_display/lcm_hbm_state`. Nav-key/touch events: `tran_fp_report_key_nav_event`, `silfp_keyevent`, `TRAN_IOC_INPUT_KEY_EVENT`, `TRAN_IOC_NAV_EVENT`.

---

## 8. Notes for a future XNU / HakiOS port

**[I] Reordering the required pieces:**

1. **Hardware bring-up:** enable SPI3 at MMIO `0x11013000`, 27 MHz, CS0; configure IRQ GPIO 13, reset GPIO 119, power GPIO 59; reproduce the six pinctrl states (`tran_fp_default`, `tran_fp_pin_irq`, `tran_fp_irq_pull_down`, `tran_fp_reset_high`, `tran_fp_reset_low`, `tran_fp_spi_default`). Power sequencing lives in `tran_fp_power_init` / `tran_fp_power_onoff` — reverse those from the `.ko` if needed.
2. **Userspace transport:** provide a character device equivalent to `/dev/goodix_fp` supporting the Goodix ioctl set (init/exit/power/IRQ/spi-clock/fw-info/raw transfer) and the sysfs contract `/sys/kernel/tran_fp/{vendor_name,fod_location_xy,tee_version}`. `fod_location_xy` must return `"540,2210"` for the UI icon.
3. **Display integration:** implement the HBM notification path (`TRAN_FULL_HBM_EVENT=FULL_HBM_SET` ↔ `lcm_hbm_state`) and a display notifier so optical capture happens under a bright screen. Without this, captures are unreliable even if SPI works.
4. **Sensor detection:** port the chip-ID read sequence (`tran_read_fp_chip_id` / `getVendorChipId`) so GF3956/GF3976/GF3986 are recognised; otherwise the HAL reports `sensor is broken` / `chip_id invalid`.
5. **Matching — the hard blocker:** the matcher, template format and image preprocessing are inside the MobiCore TA (`/odm/app/mcRegistry/05060000….tabin` for the Goodix stack; chip-specific `/vendor/app/mcRegistry/050639xx….tabin` for the v2 stack). On XNU there is no MobiCore client or TrustZone apps. Options:
   - reuse the TA requires a secure-world/OS shim that does not exist in XNU — realistically unavailable;
   - port an open-source Goodix optical-FOD stack — **not present in this dump**;
   - keep the sensor non-functional (UI only) until the TA can be obtained/decrypted or an equivalent algorithm is written.
6. **Template storage** must be migrated: `/mnt/vendor/persist/fingerprint`, `/mnt/vendor/persist/fpdata/`, `/data/vendor/goodix/gf_data`. Existing enrolled fingerprints cannot be reused across OSes without the TA's encryption keys.
7. **Anti-crack / verified boot:** `ro.vendor.tran.anti.crack.p7.support=1` in `odm/etc/build.prop` and `ro.vendor.tran.ta.crashdebug.support=1`; TA integrity is tied to the vendor trust chain. The target build disables vbmeta, which does not help with TA loading — TAs are validated inside the TEE, not by Android Verified Boot.
8. **Properties expected by the stack** (set or emulate): `persist.vendor.fp.sensorUid`, `vendor.fp.currentUid`, `vendor.fp.type`, `vendor.fp.transsion.lcmname`, `vendor.tran.fp.center.X/Y.offset`, `persist.vendor.sys.fp.goodix.spmt.flag`, `vendor.fp.cfg.checksum`.

---

## 9. Open questions / limits of this analysis

1. **Exact part number:** static evidence points to GF3976 but cannot prove it; GF3956/GF3986/GW9578/GW9598 are compiled in and TAs exist. Needs live `dmesg`/TA-open log.
2. **Active HAL:** Goodix stack is strongly indicated (§5.5), but the AIDL service supports both `gf_fingerprint` and `tran_fingerprint` via a variant key whose runtime value was not found in any dumped prop/rc file.
3. **ioctl numeric values and struct layouts:** only symbolic names were extracted. Numeric `_IOC` codes and buffer structures require disassembly of `tran_fp_ioctl` / `tran_fp_v2` ioctl handlers (both modules are unstripped, so `objdump -d --disassemble=tran_fp_ioctl` is feasible).
4. **TA contents:** encrypted; no version string or opcode table recoverable with `strings`.
5. **Image dimensions:** 1080×2400 inferred from FOD coordinate + touch firmware names, not from an explicit fingerprint property.

---

## Appendix — evidence index (exact paths)

```
/home/berucrypt00/otadump/vendor_boot/ramdisk/lib/modules/tran_fp.ko
/home/berucrypt00/otadump/vendor_boot/ramdisk/lib/modules/tran_fp_v2.ko
/home/berucrypt00/otadump/vendor_boot/ramdisk/lib/modules/modules.load
/home/berucrypt00/otadump/vendor_boot/ramdisk/lib/modules/modules.alias
/home/berucrypt00/otadump/vendor_boot/ramdisk/lib/modules/modules.dep
/home/berucrypt00/otadump/kernel_config.txt
/home/berucrypt00/otadump/odm/etc/init/vendor.goodix.rc
/home/berucrypt00/otadump/odm/etc/init/fingerprint-default.rc
/home/berucrypt00/otadump/odm/etc/vintf/manifest/fingerprint-default.xml
/home/berucrypt00/otadump/odm/etc/permissions/android.hardware.fingerprint.xml
/home/berucrypt00/otadump/vendor/bin/hw/android.hardware.biometrics.fingerprint-service
/home/berucrypt00/otadump/odm/lib64/hw/gf_fingerprint.default.so
/home/berucrypt00/otadump/odm/lib64/libgf_hal.so
/home/berucrypt00/otadump/odm/lib64/libvendor.transsion.hardware.biometrics.fingerprint.so
/home/berucrypt00/otadump/vendor/lib64/hw/tran_fingerprint.default.so
/home/berucrypt00/otadump/vendor/lib64/libtran_hal.so
/home/berucrypt00/otadump/odm/app/mcRegistry/05060000000000000000000000000000.tabin
/home/berucrypt00/otadump/odm/app/mcRegistry/05070000000000000000000000000000.drbin
/home/berucrypt00/otadump/vendor/app/mcRegistry/05063656000000000000000000000000.tabin
/home/berucrypt00/otadump/vendor/app/mcRegistry/05063956000000000000000000000000.tabin
/home/berucrypt00/otadump/vendor/app/mcRegistry/05063976000000000000000000000000.tabin
/home/berucrypt00/otadump/vendor/app/mcRegistry/05063986000000000000000000000000.tabin
/home/berucrypt00/hakios-x6886/devices/x6886/evidence/fdt.dts  (lines 4508,4525,4558-4591,4605-4608,8128-8141,8414,8482,9188)
```
