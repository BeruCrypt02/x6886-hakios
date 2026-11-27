# Touch controller analysis — Infinix X6886 (MT6789) stock dump

Analysis context: **read-only** inspection of `/home/berucrypt00/otadump` (stock dump) plus the
live-device FDT decompilation at `/home/berucrypt00/hakios-x6886/devices/x6886/evidence/fdt.dts`.
No phone access, no writes to the dump. Scratch copies/hashes were computed only in
`/tmp/hakios-x6886/touch-analysis/`.

Everything below is split into **[VERIFIED]** (directly observed in files) and
**[INFERENCE]** (interpretation, must be confirmed on hardware before acting on it).

---

## 1. Executive summary

- The shipped touch stack is a **FocalTech FT3683G SPI driver**, built as an out-of-tree
  kernel module named `focaltech_ft3683g.ko`. The board/ODM labeling in the device tree calls
  the part **ft3682g**, and the driver binary itself contains the literal string `ft3682g`,
  but every hard identifier (module name, ELF `name=`, OF alias, source tree, test-support
  file, `modules.load`, `modules.alias`) says **ft3683g**.
- The DT node `spi1@11010000/touch@0` has `compatible = "focaltech,fts\0chipsemi,chsc_cap_touch"`.
  `modules.alias` maps `focaltech,fts` → `focaltech_ft3683g`, so **FocalTech is the first/primary
  match**. The secondary compatible maps to the ChipSemi fallback driver.
- HW wiring from live FDT: **IRQ GPIO 0x0c, reset GPIO 0x74, iovdd GPIO 0x0f, avdd GPIO 0x96**,
  `vtouch-supply` regulator, SPI bus `spi1@11010000` @ **8 MHz** (`spi-max-frequency = 0x7a1200`),
  max 10 touch points, raw coords `0x437f` × `0x95ff`.
- Firmware `focaltech_ts_fw.bin` is a **237,568-byte (0x3A000) raw MCU image** with embedded
  project strings `x6886` / `Transsion` and **no ASCII chip ID**. The chip ID is read from the IC
  at runtime (driver format string `chip id read invalid, read:0x%02x%02x`), not from the FW file.
- All touch `.ko` files are **uncompressed raw ELF64 aarch64 relocatable objects** — there is
  nothing to decompress.
- Kernel ABI: `6.12.38-android16-5-gcc51d883045d-4k SMP preempt mod_unload modversions aarch64`
  (4 KB pages, `CONFIG_64BIT=y`).

---

## 2. Artifact inventory [VERIFIED]

| File | Size (bytes) | Type | SHA-256 |
|---|---:|---|---|
| `/home/berucrypt00/otadump/odm_dlkm/lib/modules/focaltech_ft3683g.ko` | 544,952 (`0x850B8`) | ELF64 LSB relocatable, ARM aarch64, BuildID `843e94848de79999ffd478dd7804d643bf4a7a56`, not stripped | `526d15efc1d84ee0bec90a1560cd93b475640ebbebd09709e1d4ff47cfc1328d` |
| `/home/berucrypt00/otadump/odm_dlkm/lib/modules/chipsemi_chsc5xxx_old.ko` | 425,176 (`0x67CD8`) | ELF64 aarch64, BuildID `48d23f2ac54efec6814ac9da37b2d800c770b1da`, not stripped | `64d78bc47027487f2140a98c460ef47a7d77cef9ead4660c4fd96c69334c6526` |
| `/home/berucrypt00/otadump/odm_dlkm/lib/modules/adaptive-ts.ko` | 300,792 (`0x496F8`) | ELF64 aarch64, BuildID `ec0e20b0617a904e20d251f10439ad572b77599e`, not stripped | `8f49890f5036e9f0bdec49131ae600cbb85e14ddf84e848bd5db24fc87292052` |
| `/home/berucrypt00/otadump/odm/firmware/focaltech_ts_fw.bin` | 237,568 (`0x3A000`) | raw data (MCU image) | `41486e2f6e47d16ce1e5d2ec5a8e35b33a510e0eb713c8e99513d9ababe133bd` |
| `/home/berucrypt00/otadump/odm/firmware/chipsemi_ts_fw.bin` | 142,508 (`0x22CAC`) | raw data, magic `CHSC` | `d364cc102d2cb54d3792dffb5e13b597eca0637d27761a95e6c70b4f1060cc32` |
| `/home/berucrypt00/otadump/vendor_dlkm/lib/modules/touch_boost.ko` | 69,312 (`0x10EC0`) | ELF64 aarch64 | `7bff538235108fc5f63a6781f2b605f97db7a68fae3a50ddfe01b961f5770803` |
| `/home/berucrypt00/otadump/vendor_dlkm/lib/modules/mtk_ioctl_touch_boost.ko` | 16,240 (`0x3F70`) | ELF64 aarch64 | `f275517e51bfaff2850dcf8a985d5913a176dfb3e7b92e3bfa3a78c3c4e14a76` |

Other touch-related files:

- `/home/berucrypt00/otadump/vendor/etc/init.insmod.touch.cfg`
- `/home/berucrypt00/otadump/vendor/etc/init/init.touch.rc`
- `/home/berucrypt00/otadump/vendor/etc/init/touch_boost.rc`
- `/home/berucrypt00/otadump/vendor/bin/init.insmod.sh`
- `/home/berucrypt00/otadump/odm_dlkm/lib/modules/modules.alias`, `modules.dep`, `modules.load`
- `/home/berucrypt00/otadump/kernel_config.txt`

A whole-dump filename search found **no other** `ft3xxx`/`focaltech`/`chct`/`chsc` touch files
(no FTS `.ini` test config in the dump — the driver references `FTS_INI_FILE_PATH` but the file
is not present in these partitions). One unrelated display module was found in `vendor_boot`:
`nt37703a_fhdp_dsi_vdo_dsc_tm_chct_120hz_cm5.ko` (a panel driver, not touch).

---

## 3. Driver modules — exact identity [VERIFIED]

### 3.1 `focaltech_ft3683g.ko` — `modinfo` output

```
filename:       .../odm_dlkm/lib/modules/focaltech_ft3683g.ko
author:         FocalTech Driver Team
description:    FocalTech Touchscreen Driver
license:        GPL v2
built_with:     DDK
name:           focaltech_ft3683g
depends:        transsion_tranlog,adaptive-ts
alias:          of:N*T*Cfocaltech,fts
alias:          of:N*T*Cfocaltech,ftsC*
vermagic:       6.12.38-android16-5-gcc51d883045d-4k SMP preempt mod_unload modversions aarch64
```

No `parm:` lines — the module exports **no `module_param` tunables**. All configuration comes
from device-tree properties (see §6).

Full `.modinfo` section (hex-verified at file offset `0x370c0`, size `0x134`) confirms the
same fields and contains no extra version tag.

### 3.2 Version / source strings embedded in the FTS module [VERIFIED]

```
FocalTech V4.1 20230424          <- driver version/date string (file offset 0x35859)
Driver Ver:%s                    <- sysfs format string
name=focaltech_ft3683g
focaltech_ft3683g
ft3683g_controller
ft3683g
ft3682g                          <- single standalone string, file offset 0x2B613
```

The version string `FocalTech V4.1 20230424` is the only driver-version evidence in the binary.

Embedded build source paths (verbatim strings) show the exact out-of-tree source tree:

```
../kernel_device_modules-6.12/drivers/input/touchscreen/transsion/vendor/focaltech/ft3683g_spi/focaltech_core.c
../kernel_device_modules-6.12/drivers/input/touchscreen/transsion/vendor/focaltech/ft3683g_spi/focaltech_spi_v2.c
../kernel_device_modules-6.12/drivers/input/touchscreen/transsion/vendor/focaltech/ft3683g_spi/focaltech_flash.c
../kernel_device_modules-6.12/drivers/input/touchscreen/transsion/vendor/focaltech/ft3683g_spi/focaltech_ex_fun.c
../kernel_device_modules-6.12/drivers/input/touchscreen/transsion/vendor/focaltech/ft3683g_spi/focaltech_test/focaltech_test.c
../kernel_device_modules-6.12/drivers/input/touchscreen/transsion/vendor/focaltech/ft3683g_spi/focaltech_test/focaltech_test_ini.c
../kernel_device_modules-6.12/drivers/input/touchscreen/transsion/vendor/focaltech/ft3683g_spi/focaltech_test/supported_ic/focaltech_test_ft3683g.c
```

Note the test-support file is literally named **`focaltech_test_ft3683g.c`**.

ELF tables (verified with `readelf -sW`):
- `fts_ts_id` (`.rodata+0x380`, 80 bytes) contains the SPI device ID string **`mtk-tpd`**.
- `__mod_of__fts_dt_match_device_table` (`.rodata+0x1f0`, 400 bytes) contains the OF compatible
  **`focaltech,fts`** at `.rodata+0x230`.
- Important symbols present: `fts_check_cid`, `fts_get_ic_information`, `fts_read_reg`,
  `fts_write_reg`, `fts_read`, `fts_write`, `fts_bus_transfer_direct`, `fts_ts_spi_init`,
  `fts_ts_probe`, `fts_ts_suspend`, `fts_ts_resume`, `fts_irq_enable/disable`,
  `fts_fwupg_*`, `fts_input_init`, `fts_point_report_check_init`.
- Imported kernel APIs of interest: `spi_sync`, `spi_setup`, `__spi_register_driver`,
  `request_threaded_irq`, `request_firmware`, `input_mt_init_slots`, `input_set_abs_params`,
  `of_property_read_variable_u32_array`, `tran_thp_device_init`.

### 3.3 Fallback driver — `chipsemi_chsc5xxx_old.ko` [VERIFIED]

```
author:         Transsion TP Driver Team / wasim
description:    Transsion Touchscreen Driver
license:        GPL v2 / GPL
built_with:     DDK
name:           chipsemi_chsc5xxx_old
depends:        adaptive-ts
vermagic:       (same 6.12.38-android16-5-...-4k aarch64)
```

Embedded source paths:

```
../kernel_device_modules-6.12/drivers/input/touchscreen/transsion/vendor/chipsemi/chipsemi_chsc5xxx_old/semi_touch_device.c
../kernel_device_modules-6.12/drivers/input/touchscreen/transsion/vendor/chipsemi/chipsemi_chsc5xxx_old/semi_touch_custom.c
../kernel_device_modules-6.12/drivers/input/touchscreen/transsion/vendor/chipsemi/chipsemi_chsc5xxx_old/qual_core.c
../kernel_device_modules-6.12/drivers/input/touchscreen/transsion/vendor/chipsemi/chipsemi_chsc5xxx_old/semi_touch_test_5xxx.c
```

Chip/version strings: `chct5560`, `chct5562`, `chipsemi`, `chsc`, `v4.3.2`,
`driver version is %s`, `boot version is %04X`, `config version is %02X`.
Firmware/file strings: `chipsemi_ts_fw.bin`, `SetSpiPeed.bin`, `boot_FirmwareChecked.bin`,
`FirmwareChecked0.bin`, `FirmwareChecked9.bin`, `cfg_mercury_default.bin`,
`/sdcard/chsc_auto_update_packet.bin`.

### 3.4 Shared dependency — `adaptive-ts.ko` [VERIFIED]

```
description:    Transsion touch panel driver
author:         <Transsion_BSP@transsion.com>
name:           adaptive_ts
depends:        transsion_tranlog,mtk_disp_notify,thd,opsMediator
```

`modules.dep` lines:

```
/odm_dlkm/lib/modules/focaltech_ft3683g.ko: /odm_dlkm/lib/modules/adaptive-ts.ko
/odm_dlkm/lib/modules/chipsemi_chsc5xxx_old.ko: /odm_dlkm/lib/modules/adaptive-ts.ko
```

The FTS module calls `tran_thp_device_init` and exports many `tran_thp_*` / `thp_*` symbols —
i.e. a Transsion "THP" layer sits between the raw touch driver and Android input.

### 3.5 `modules.alias` / `modules.load` [VERIFIED]

```
alias of:N*T*Cfocaltech,fts focaltech_ft3683g
alias of:N*T*Cfocaltech,ftsC* focaltech_ft3683g
```

`modules.load` first three entries:

```
focaltech_ft3683g.ko
adaptive-ts.ko
chipsemi_chsc5xxx_old.ko
```

**Conclusion:** the OF compatible `focaltech,fts` on `touch@0` resolves to
`focaltech_ft3683g.ko`. [VERIFIED]

---

## 4. The 3682g vs 3683g discrepancy

### Evidence for "3683g" [VERIFIED]
1. Module filename: `focaltech_ft3683g.ko`.
2. `modinfo name` / `.modinfo`: `name=focaltech_ft3683g`.
3. `modules.alias`: `of:N*T*Cfocaltech,fts` → `focaltech_ft3683g`.
4. `modules.load` lists `focaltech_ft3683g.ko`.
5. Source tree and test file: `.../ft3683g_spi/...`, `focaltech_test_ft3683g.c`.
6. Other embedded strings: `ft3683g`, `ft3683g_controller`.

### Evidence for "3682g" [VERIFIED]
1. Live FDT `touch` supplier string:
   `supplier-0 = "nt37706a_fhdp_dsi_vdo_dsc_boe_boe_144hz_x6886, focaltech, ft3682g";`
2. One standalone string `ft3682g` in the FTS driver binary at file offset `0x2B613`, immediately
   followed by `fts_set_edg_level` in the string table (hexdump around file offset `0x2b613`:
   `66 74 33 36 38 32 67 00 66 74 73 ...` = `"ft3682g\0fts..."`).

### What the firmware header says [VERIFIED]
The 237,568-byte firmware contains **no ASCII `FT3682`, `FT3683`, or any chip-ID string**.
A full `strings` scan yields only `x6886` (offsets `0x140`, `0x3851C`) and `Transsion`
(offsets `0x160`, `0x3853E`) as meaningful ASCII. First 64 bytes:

```
00000000: 0000 2060 0003 8c7c 0003 8c7c 0003 8c7c  .. `...|...|...
00000010: 0003 8c7c 0003 8c7c 0003 8c7c 0003 8c7c  ...|...|...|...
00000020: 0003 8c7c 0003 8c7c 0003 8c7c 0003 8c7c  ...|...|...|...
00000030: 0003 8c7c 0003 8c7c 0003 8c7c 0003 8c7c  ...|...|...|...
```

### Determination [INFERENCE, high confidence]
The part actually driven is the **FT3683G-family SPI touch controller**, using the FocalTech
generic compatible `focaltech,fts`. "ft3682g" is an ODM board-level label (panel+TP supplier
string) and a leftover/alternate IC-name string inside the driver, not the identity of the
loaded driver. The runtime chip is identified by reading a 2-byte chip ID from the IC itself
(`[FTS_TS]%s:chip id read invalid, read:0x%02x%02x`, `read boot id:0x%02x%02x`), so the exact
silicon revision cannot be settled from the dump alone — it needs a live `getreg`/sysfs read
of the chip-ID registers. **Do not assume FT3682 silicon from the FDT supplier string alone.**

---

## 5. Firmware binaries

### 5.1 `focaltech_ts_fw.bin` [VERIFIED]
- Path: `/home/berucrypt00/otadump/odm/firmware/focaltech_ts_fw.bin`
- Size: 237,568 bytes (`0x3A000`), `file(1)` → `data`, no compression magic.
- Header bytes (first 0x40): see §4. After the repeated vector-like pattern, code begins at
  `0x60`: `e060 3c00 e1f0 cc02 e060 3c10 ea00 c805 ...` (8051-style opcodes: `0xE0` MOVX A,@DPTR,
  `0xF0` MOVX @DPTR,A, `0x02` LJMP, etc. [INFERENCE]).
- Embedded strings: `x6886` at `0x140`/`0x3851C`, `Transsion` at `0x160`/`0x3853E`, padded with
  `0xFE`/`0xFF`. These are project-ID fields, not a chip ID.
- No checksum/version ASCII field found. Driver-side log strings indicate version is read from
  the IC and from a version offset inside the FW:
  `fw version in tp:%x, host:%x, bv:%x`, `fw version in tp:%x`,
  `fw len(0x%0x) < fw ver offset(0x%x)`, `fw paramcfg version offset:%x`,
  `fw len(%x) < paramcfg offset(%x), no IDE`, `read boot id:0x%02x%02x`.
- [INFERENCE] The image is a raw MCU binary with an interrupt-vector header (big-endian
  addresses `0x00002060` reset vector and repeated `0x00038c7c` default handler, the latter
  inside the 0x3A000 image). It is flashed verbatim over SPI; the driver does the ECC/CRC and
  boot-mode handshake.

### 5.2 `chipsemi_ts_fw.bin` [VERIFIED]
- Path: `/home/berucrypt00/otadump/odm/firmware/chipsemi_ts_fw.bin`
- Size: 142,508 bytes (`0x22CAC`), first bytes `43 48 53 43 00 00 00 00 01 00 00 00 ...`
  → ASCII magic **`CHSC`**, then zeros, then `01 00 00 00 01 00 00 00 00 08 00 00 ...`.
- Same project coding as the alternate supplier; only used if ChipSemi is fitted.

---

## 6. DT wiring and config parameters

### 6.1 Init config files [VERIFIED]

`/home/berucrypt00/otadump/vendor/etc/init.insmod.touch.cfg` — a "kitchen-sink" list of
modprobe attempts (25 module names). The ones relevant here are at lines 12, 21, 24:

```
modprobe|ft3683g.ko
modprobe|focaltech_touch.ko
modprobe|fts_touch_spi.ko
```

Full list (verbatim): `tui-common.ko`, `ts_scp_common.ko`, `gt9895.ko`, `ft3518u.ko`,
`ft3683g.ko`, `st61y.ko`, `gt9916.ko`, `nt36672c.ko`, `gt9886.ko`, `synaptics_tcm.ko`,
`gt9896s.ko`, `gt1151.ko`, `td4320.ko`, `nt36xxx_no_flash_spi.ko`,
`hxchipset_hx83102p.ko`, `nt36672c_i2c.ko`, `nt36672c_144hz.ko`, `nt36672c_120hz.ko`,
`legacy_gt9896s.ko`, `ft3518.ko`, `nt36532.ko`, `focaltech_touch.ko`, `fts_touch_spi.ko`,
`omnivision_td4160.ko`. Only module names that exist are actually loadable; the OF alias
mechanism decides which binds.

**There are no key=value module parameters and no values in this cfg** — each line is just
`modprobe|<filename>`. The touch driver also exports no `module_param`. [VERIFIED]

`/home/berucrypt00/otadump/vendor/etc/init/init.touch.rc`:

```
service insmod_touch_sh /vendor/bin/init.insmod.sh /vendor/etc/init.insmod.touch.cfg
    class main
    user root
    group root system
    disabled
    oneshot

on init
    start insmod_touch_sh
```

`/home/berucrypt00/otadump/vendor/bin/init.insmod.sh` parses `action|arg` and for `modprobe`
executes `modprobe -a -d ${modules_dir} ${arg}` over `/system/lib/modules`, `/vendor/lib/modules`,
`/odm/lib/modules` (with a `16k` subdir when page size is 16384). No parameters are appended.

`/home/berucrypt00/otadump/vendor/etc/init/touch_boost.rc` (performance side-channel only):

```
service touch_boost /vendor/bin/touch_boost
    class main
    user system
    group system readproc wifi media media_rw camera audio

on boot
    start touch_boost

on post-fs-data
    chown system system /proc/perfmgr_touch_boost
    chown system system /proc/perfmgr_touch_boost/ioctl_touch_boost
```

`touch_boost.ko` is `MediaTek TOUCH_BOOST`, `mtk_ioctl_touch_boost.ko` is its perf-ioctl
helper. They are not the touch driver — they expose `/proc/perfmgr_touch_boost` and
`/proc/perfmgr_touch_boost/ioctl_touch_boost`.

### 6.2 Live FDT: bus and node [VERIFIED]

File: `/home/berucrypt00/hakios-x6886/devices/x6886/evidence/fdt.dts`

`touch` top-level node (lines ~1603–1623):

```
touch {
    compatible = "mediatek,touch";
    supplier-num = <0x02>;
    supplier-vendor-0 = "BOE";
    supplier-0 = "nt37706a_fhdp_dsi_vdo_dsc_boe_boe_144hz_x6886, focaltech, ft3682g";
    supplier-vendor-fwname-0 = "focaltech_ts_fw.bin";
    supplier-resolution-0 = <0x10>;
    supplier-vendor-1 = "TM";
    supplier-1 = "vtdr6126a_fhdp_dsi_vdo_dsc_tm_tm_144hz_x6886, chipsemi, chct5560";
    supplier-vendor-fwname-1 = "chipsemi_ts_fw.bin";
    supplier-resolution-1 = <0x10>;
    touchscreen,fingerprint-support;
    touchscreen,dynamic-report-rate-support;
    touchscreen,pocket-mode-amt-support;
    touchscreen,esports-engine = <0x02>;
    interrupt-parent = <0x49>;
    interrupts = <0x09 0x02>;
    status = "okay";
};
```

SPI controller and touch child (lines ~4486–4532):

```
spi1@11010000 {
    compatible = "mediatek,mt6765-spi";
    mediatek,pad-select = <0x00>;
    reg = <0x00 0x11010000 0x00 0x100>;
    interrupts = <0x00 0xc0 0x04 0x00>;
    clocks = <0x2b 0x7f 0x2b 0x12 0x2c 0x26>;
    clock-names = "parent-clk\0sel-clk\0spi-clk";

    touch@0 {
        compatible = "focaltech,fts\0chipsemi,chsc_cap_touch";
        reg = <0x00>;
        status = "ok";
        spi-max-frequency = <0x7a1200>;            /* 8,000,000 Hz */
        interrupt-parent = <0x49>;
        vtouch-supply = <0x143>;
        focaltech,iovdd-gpio = <0x49 0x0f 0x00>;
        focaltech,irq-gpio   = <0x49 0x0c 0x00>;
        focaltech,reset-gpio = <0x49 0x74 0x00>;
        focaltech,avdd-gpio  = <0x49 0x96 0x00>;
        focaltech,max-touch-number = <0x0a>;
        focaltech,display-coords = <0x00 0x00 0x437f 0x95ff>;
        focaltech,fod-function-support;
        focaltech,custom-mistouch-support;
        focaltech,pocket-function-support;
        focaltech,switch-report-support;
        pinctrl-names = "default\0state_spi_mode";
        pinctrl-0 = <0x1ef>;
        pinctrl-1 = <0x1f0>;
        chipsemi,int-gpio = <0x49 0x0c 0x00>;
        chipsemi,rst-gpio = <0x49 0x74 0x00>;
        chipsemi,vdd-gpio = <0x49 0x96 0x00>;
        chipsemi,vddio-gpio = <0x49 0x0f 0x00>;
        chipsemi,x-coords = <0x437f>;
        chipsemi,y-coords = <0x95ff>;
        chipsemi,multiple-solution = <0x10>;
        chipsemi,fp_x_center = <0x21c>;
        chipsemi,fp_y_center = <0x87d>;
        chipsemi,fp_radius = <0x3f>;
        chipsemi,rawdata_limits_row = <0x28>;      /* 40 */
        chipsemi,rawdata_limits_col = <0x12>;      /* 18 */
        chipsemi,short_limits = <0x2bc>;           /* 700 */
        invalid_row_col = <0xff ... (8 bytes)>;
        rawdata_lo_limits = <...>;                 /* long calibration array */
        rawdata_hi_limits = <...>;                 /* long calibration array */
    };
};
```

Pinctrl states (lines ~2825–2833):

```
ctp_default {                 phandle = <0x1ef>; };          /* empty */
ctp_spimode_default {         phandle = <0x1f0>;
    pins_cmd_dat {
        pinmux = <0x2501 0x2301 0x2401 0x2201>;
        drive-strength = <0x02>;
    };
};
```

Decoded values [VERIFIED arithmetic]:
- `spi-max-frequency` `0x7a1200` = **8,000,000 Hz (8 MHz)**.
- GPIOs: `irq = 0x0c (12)`, `reset = 0x74 (116)`, `iovdd = 0x0f (15)`, `avdd = 0x96 (150)`.
  `0x49` is the GPIO-controller phandle, not part of the GPIO number.
- `max-touch-number = 0x0a` → 10 slots.
- `display-coords` x = `0x437f` = 17,279; y = `0x95ff` = 38,399.
- ChipSemi matrix: 40 rows × 18 cols; `multiple-solution = 0x10` (16); FOD center
  `(0x21c, 0x87d)` = (540, 2173), radius `0x3f` = 63.
- SPI controller IRQ `0xc0` (192), MMIO base `0x11010000` size `0x100`.

### 6.3 Kernel config [VERIFIED]

`/home/berucrypt00/otadump/kernel_config.txt`:

```
CONFIG_64BIT=y
CONFIG_PAGE_SIZE_4KB=y
CONFIG_INPUT=y
CONFIG_INPUT_EVDEV=y
CONFIG_INPUT_TOUCHSCREEN=y
CONFIG_SPI=y
CONFIG_SPI_MASTER=y
CONFIG_SPI_DYNAMIC=y
CONFIG_SPI_MEM=y
CONFIG_REGMAP_SPI=y
```

No `CONFIG_TOUCHSCREEN_FOCALTECH` / FTS option is present — the driver is out-of-tree, loaded
from `odm_dlkm`. All upstream in-tree touchscreen options are `# ... is not set`.

---

## 7. SPI protocol details from strings [VERIFIED unless marked]

### 7.1 Command opcodes / protocol verbs
The module contains these command identifiers (verbatim strings):

```
PROC_READ_REGISTER
PROC_WRITE_REGISTER
PROC_READ_DATA
PROC_WRITE_DATA
PROC_READ_DATA_DIRECT
PROC_WRITE_DATA_DIRECT
PROC_SET_BOOT_MODE
PROC_SET_TEST_FLAG
PROC_ENTER_TEST_ENVIRONMENT
```

Additional numeric command/protocol evidence from log strings:

```
"pram/bootloader read 03(SPI) command fail"     -> SPI read opcode 0x03
"pram/bootloader read 03(SPI_V2) command fail"
"pram/bootloader write 03 command fail"
"upgrade mode(09) cmd write fail"               -> 0x09 = enter upgrade/boot mode
"write 90 cmd fail"                             -> 0x90 used in flash flow
"write 55 cmd fail"
"write FC=0xAA fail" / "write FC=0x55 fail"     -> 0xAA then 0x55 written to reg 0xFC
"send 0xAA and 0x55 to FW, reset to boot environment"
"pram/rom/bootloader reset cmd write fail"
"cmd(PROC_READ_DATA) length(%d) fail"
"cmd(PROC_READ_DATA_DIRECT) length(%d) fail"
"hid2std cmd read fail" / "hid2std cmd write fail"
"erase cmd fail" / "ecc calc cmd write fail" / "ecc init cmd write fail"
```

Functions `fts_read`, `fts_write`, `fts_read_reg`, `fts_write_reg`, `fts_bus_transfer_direct`
exist; the bus layer is `focaltech_spi_v2.c` and the probe log says
`"Touch Screen(SPI-2 BUS) driver prboe..."`. [VERIFIED]

### 7.2 Registers observed in strings [VERIFIED]

| Register | Evidence string | Purpose (inference) |
|---|---|---|
| `0x01` | `write 0x01 fail,ret=%d` / `restore 0x0A` / `write 0x1c` etc. | command / mode reg |
| `0x06` | `read 0x06 fail,ret=%d` | FW version or status |
| `0x0A` | `read 0x0A fail,ret=%d`, `restore 0x0A` | key number / chip info |
| `0x1c`, `0x1d` | `write 0x1c fail`, `write 0x1d fail` | test / address high-low |
| `0x54` | `read 0x54 register fail`, `restore 0xBD` | gesture header area |
| `0x5B` | `read 0x5B fail,ret=%d` | factory/test data |
| `0x8B` | `Charger Reg(0x8B):%d` | charger-mode flag |
| `0x91` | `read reg0x91 fail`, `reg0x91,val:%x,last:%x`, `reg0x91 keep a value for 5 times, need execute TP reset` | ESD / watchdog status |
| `0xBD` | `restore 0xBD fail,ret=%d`, `read regBD fail` | factory-mode save/restore |
| `0xC0` | `Glove Reg(0xC0):%d` | glove mode |
| `0xC1` | `Cover Reg(0xC1):%d` | cover mode |
| `0xD0` | `Reg(0xD0)=%d` | status |
| `0xFC` | `write FC=0xAA`, `write FC=0x55` | boot/reset trigger register |

Chip/boot ID is **two bytes**, from format strings `chip id read invalid, read:0x%02x%02x`
and `read boot id:0x%02x%02x`. [VERIFIED format; values unknown without live read]

### 7.3 Report format / coordinate handling [VERIFIED strings]

```
"THP parameters from DTS: thp_frame_len=%d, thp_crc_len=%u, tx=%u, rx=%u,
 reserves_big_buf_size=%d, touch_event_offset=%d, palm_support=%d"
"THP parameters initialized successfully: frame_len=%u(+%zu)=%u, crc_len=%u,
 tx=%u, rx=%u, queues=%u"
"read packet_number:%d, remainder:%d"
"data read(addr:%x) crc abnormal,retry:%d"
"touch buff is 0xff, need recovery state"
"no touch point information(%02x)"
"touch point ID(%d) beyond max_touch_number(%d)"
"touch_num:%d > max_touch_num:%d, invalid touch number"
"[THP][res:%d] P%d(%d, %d)[p:%d,tm:%d] DOWN!"   /   "... UP!"
"VK Number:%d, key:(%d,%d,%d), coords:(%d,%d),(%d,%d),(%d,%d)"
```

DT properties the driver reads (all `focaltech,*`):

```
focaltech,display-coords
focaltech,max-touch-number
focaltech,irq-gpio
focaltech,reset-gpio
focaltech,iovdd-gpio
focaltech,avdd-gpio
focaltech,key-number
focaltech,have-key
focaltech,keys
focaltech,key-x-coords
focaltech,key-y-coords
focaltech,fod-function-support
focaltech,custom-mistouch-support
focaltech,pocket-function-support
focaltech,switch-report-support
focaltech,tran-rain-thp-support
focaltech,thp-palm-support
focaltech,thp_data_head_size
focaltech,thp_crc_offset
focaltech,thp_report_buf_size
focaltech,thp_debug_buf_size
focaltech,thp_reserves_big_buf_size
```

Important: the live `touch@0` node **does not contain any `focaltech,thp_*` properties**. The
driver has fallbacks: `Unable to get "focaltech,thp_report_buf_size", using default: %d`,
`...thp_crc_offset... using default`, `...thp_data_head_size... using default`,
`...thp_debug_buf_size... using default`, `...thp_reserves_big_buf_size... using default`,
`use default, thp_tx_num:%d`, `use default, thp_rx_num:%d`. [VERIFIED]

=> On this device the frame layout (head size, CRC offset/length, tx/rx, queue sizes) is
**driver-default**, not DT-specified. The defaults are compiled constants and cannot be
recovered from strings alone. [INFERENCE]

Coordinate scaling [VERIFIED values, INFERENCE on interpretation]: raw range is
`x ∈ [0, 0x437f]` (17,279), `y ∈ [0, 0x95ff]` (38,399). The panel logical resolution is
not present in this DT node. Dividing by 16 gives ≈1080 × 2400, which does **not** equal the
known 1220 × 2712 panel of X6886, so scaling is likely done in the Transsion THP/userspace
layer (`tran_thp_input_report_touch`, `input_set_abs_params`, etc.) rather than a simple /16
in the kernel. This must be verified on-device. [INFERENCE]

### 7.4 FW upgrade flow (string-derived sequence) [INFERENCE from verified strings]

1. `request_firmware("focaltech_ts_fw.bin")` — name supplied from `supplier-vendor-fwname-0`.
2. Read boot ID / chip ID (2 bytes); check IC type.
3. Enter boot/upgrade mode: write `0x09` command; on some paths write `0xAA`/`0x55` to reg `0xFC`.
4. Optional PRAM path: `fts_pram_write_init`, `fts_pram_ecc_cal`, `fts_pram_write_buf`,
   `fts_pram_write_remap` (logs: `pram supported, write pramboot and init`,
   `pramboot ecc check fail`, `write pramboot ecc cal cmd fail`).
5. Erase, write image in chunks, ECC check, then reset to normal boot
   (`upgrade success, reset to normal boot`, `success upgrade to fw version %02x`,
   `success upgrade to fw param version %02x`).
6. Runtime ESD watchdog: reg `0x91` polled, 5 identical values → TP reset.

---

## 8. Compression status [VERIFIED]

Magic-byte scan of every relevant file:

```
focaltech_ft3683g.ko          7f 45 4c 46 02 01 ...  -> ELF64 aarch64 (NOT compressed)
chipsemi_chsc5xxx_old.ko      7f 45 4c 46 02 01 ...  -> ELF64 aarch64 (NOT compressed)
adaptive-ts.ko                7f 45 4c 46 02 01 ...  -> ELF64 aarch64 (NOT compressed)
touch_boost.ko                7f 45 4c 46 02 01 ...  -> ELF64 aarch64 (NOT compressed)
mtk_ioctl_touch_boost.ko      7f 45 4c 46 02 01 ...  -> ELF64 aarch64 (NOT compressed)
focaltech_ts_fw.bin           00 00 20 60 00 03 ...  -> raw MCU image
chipsemi_ts_fw.bin            43 48 53 43 00 00 ...  -> raw "CHSC" image
```

No `*.ko.gz`, `*.ko.xz`, or `*.ko.zst` files exist anywhere in the dump. **No decompression
is required.** For reference only (not applicable here): if a future dump contains gzip
(`1f 8b`) or xz (`fd 37 7a 58 5a 00`) modules, decompress with `gzip -dc` / `xz -dc` into a
scratch file, never over the dump.

---

## 9. Notes for porting an FTS SPI touch driver into XNU

All items below are analysis-derived guidance, not verified XNU behavior.

1. **Bus**: implement/attach an SPI master for `spi1@11010000` (MMIO `0x11010000..0x110100ff`,
   controller IRQ 192). Clock names `parent-clk`, `sel-clk`, `spi-clk`; bus speed 8 MHz.
   No `spi-cpol`/`spi-cpha` properties are present on `touch@0` → [INFERENCE] default SPI mode 0;
   confirm with a logic analyzer or by reading the MTK SPI setup on a live kernel.
2. **Device matching**: bind on compatible `focaltech,fts`; this is what `modules.alias` uses.
   Ignore the `mtk-tpd` SPI ID unless the board uses SPI ID matching.
3. **GPIO/power sequence** [INFERENCE from driver strings]: request `avdd` (0x96) and `iovdd`
   (0x0f) GPIOs, drive them via the `vtouch-supply` regulator hooks, then reset (0x74) with
   `hw reset executed` / `FTS_TS/I ... hw reset` timing; IRQ 0x0c is threaded
   (`request_threaded_irq`). Logs `[GPIO]avdd gpio request failed`,
   `[GPIO]set_direction for avdd gpio failed`, `[GPIO]iovdd gpio request failed`,
   `fail to enable power(regulator)`.
4. **Read path**: IRQ → `spi_sync` with the FTS 0x03 read command and a multi-byte frame
   header/dummy phase → validate CRC (`data read(addr:%x) crc abnormal,retry:%d`,
   `thp_crc_offset`) → extract up to 10 points. Because the DT lacks `thp_*` properties,
   the frame head size / CRC offset / tx-rx geometry come from compiled defaults that must be
   recovered either from the live device (sysfs `/proc` nodes listed below) or by decoding
   the driver's default constants with an aarch64 disassembler.
5. **Coordinate output**: raw 0..0x437f / 0..0x95ff; reproduce whatever the Transsion THP layer
   does for logical-panel mapping, edge-reject, palm, FOD, pocket, glove, cover, charger mode.
6. **Userspace interfaces observed** (useful for live cross-check before porting): sysfs
   attributes `fts_fw_version`, `fts_rw_reg`, `fts_dump_reg`, `fts_upgrade_bin`,
   `fts_force_upgrade`, `fts_driver_info`, `fts_hw_reset`, `fts_irq`, `fts_boot_mode`,
   `fts_touch_point`, `fts_log_level`, `fts_pen`, `fts_touch_size`, `fts_ta_mode`,
   `fts_edge_mode`, `fts_glove_mode`, `fts_cover_mode`, `fts_charger_mode`, `fts_gesture_mode`,
   `fts_gesture_buf`, `fts_gesture_bm`, `fts_prc`, `fts_test`, plus proc nodes
   `/proc/report_rate_switch`, `/proc/fingerprint_status`, and a `/dev/` misc node for the THP
   channel. `fts_driver_info` is where `FocalTech V4.1 20230424` is exposed.
7. **Firmware delivery**: Linux uses `request_firmware()` with the name from
   `supplier-vendor-fwname-0` (`focaltech_ts_fw.bin`). An XNU port needs an equivalent
   user-space loader or an embedded copy of the 0x3A000-byte image; preserve the bare binary
   (no header rewrite — the first bytes are vector entries).
8. **Do not reuse Linux module dependencies verbatim**: `adaptive-ts` / `transsion_tranlog` /
   `mtk_disp_notify` / `thd` / `opsMediator` are Android/MediaTek constructs. Decide explicitly
   which THP features to drop (gesture, glove, cover, pocket, FOD, edge reject) for a first
   minimal XNU bring-up.
9. **Descriptive checks to run on the live device later** (not performed here, per task):
   read chip ID / boot ID registers, dump `fts_driver_info` and `fts_fw_version`, and read
   the `thp_*` values from the THP proc/sysfs nodes to resolve the default frame geometry.

---

## 10. Open questions / limits of this analysis

- Exact silicon (FT3682G vs FT3683G) cannot be settled from static files; the FW has no ASCII
  chip ID and the driver reads a 2-byte ID at runtime. [LIMIT]
- Numeric values of `PROC_*` opcodes and the exact SPI transfer framing (header length,
  dummy-byte count, address width) are compiled immediates. No aarch64 disassembler was
  available in this environment (`objdump` here is x86-only; capstone/LLVM/交叉 tools absent),
  so these were not decoded from `.text`. Only the hex opcodes appearing in log strings are
  reported. [LIMIT]
- THP default frame parameters (head size, CRC offset, tx/rx, buffer sizes) are compiled
  constants; the live DT does not provide them. [LIMIT]
- Whether the `chct5560` ChipSemi part is actually fitted on this specific unit cannot be
  determined from the dump; the task statement says FocalTech is active, and the OF alias
  order supports FocalTech being primary. [LIMIT]
