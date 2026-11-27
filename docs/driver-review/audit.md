# X6886 hardware to XNU driver audit

Offline audit, 24 Nov 2026. Every claim below points at a file in `otadump`,
`x6886-hakios/device-dump`, or `x6886-hakios/refs`. No adb, no fastboot, no
writes to loader, kernel trees, or boot images. Sibling document:
`docs/driver-matrix.md` tracks bring-up phases; this file tracks hardware
identity and driver gaps.

## Evidence sources

| Path | What it gives | Caveat |
| --- | --- | --- |
| `devices/x6886/evidence/fdt.dts` | Live `/sys/firmware/fdt` decompile. Node names, compatibles, GPIOs, regulators, register bases | Device tree is stock Linux. It does not give register programming sequences |
| `devices/x6886/evidence/iomem.txt` | Linux's view of occupied physical ranges | Kernel-owned ranges only; LK's framebuffer is absent |
| `devices/x6886/evidence/modules.txt` | Modules loaded on the running stock kernel | Shows what Linux uses, not what XNU has |
| `otadump/all_files.txt` | Firmware blobs, HAL libraries, vendor `.ko` files | Filename list; no binary analysis done here |
| `otadump/kernel_version.txt:1` | `6.12.38-android16-5-gb575a0b6e647-ab14355190-4k`, built 29 Oct 2025 | Device-dump `version.txt` reports a different build stamp, `6.12.38-android16-Nyanmaru` (12 Nov 2026). Both are kernel 6.12.38 |
| `devices/x6886/evidence/HARDWARE-IDENTITY.md` | Board survey from the live device and the spec PDF | Some IC names there come from the PDF, not the DTB. DTB wins where they disagree |
| `refs/hakios-mtk/xnu/files/bsd/dev/` | XNU BSD device drivers from the HakiOS MediaTek line | Written for POCO X5 Pro (redwood, MT6768) and Infinix Hot 20 (X6826B, MT6768). No MT6789 driver in this directory |
| `refs/oosexgang-xnu27/docs/drivers.md` | Boot-arg and feature table for those drivers | States which parts are phone-independent and which need a port |
| `refs/unixnu-mt6789/README.md:1-16` | MT6789 scaffold for UniXNU | Explicitly "not a complete SoC driver implementation" |
| `refs/xnu-12377-hakios/README.md:1-13` | Base XNU 12377 tree with Mach, BSD, IOKit | No MT6789 peripheral driver in the base tree |

## Master map

| Component | Sensor / IC from stock | Stock evidence | XNU driver in refs | Readiness | Missing driver / register mismatch / known bug | Face ID relevance |
| --- | --- | --- | --- | --- | --- | --- |
| Panel | BOE `nt37706a` active; TM `vtdr6126a` spare | `fdt.dts:69`, `:6538-6574`; `modules.txt:391-392`; `all_files.txt:11315,11422` | None for DSI init; `refs/oosexgang-xnu27/docs/drivers.md:21-23` only flips LK's OVL0 layer | Not ported | Missing: no DSI/DSC init sequence in any ref. Mismatch: LK framebuffer address disputed. Bug: `atag,videolfb` still names a 60 Hz JDI panel | High: animation surface |
| Touch | FocalTech FTS on `spi1.0`; DTB string `ft3682g`, loaded module `focaltech_ft3683g` | `fdt.dts:1608-1609,4486-4520`; `modules.txt:70-71`; `all_files.txt:1067` | `board/fts.c:1-11` FT3658U over GENI; `fts_protocol.h:1-14`; X676C MT6789 I2C driver at `Input-and-HID.md:26,39,43`; `redwood_goodix.c:2-20` FTS 5.x over GENI | Not ported | Missing: no MT6789 **SPI** FTS driver. Mismatch: all refs use GENI or MT6789 I2C, this board uses `spi1`; 116448-byte redwood FW vs stock blob. Bug: FT3682G/FT3683G naming conflict | Medium: fallback input |
| Rear camera | Sony `imx882`, camera 0, 50 MP | `fdt.dts:1912,4690-4697`; `X6886-specs.txt:121-147`; `all_files.txt:755-760,1265+` | `redwood_mtkcam.c:2-24` (S5K JN1); `redwood_mtkcam_skel.c:3-14` platform table | Skeleton only | Missing: sensor register tables, SENINF/CAMSV values. Mismatch: different sensor, different PMIC rails. Bug: none observed | None |
| Front camera | GalaxyCore `gc13a0ff`, camera 1, 13.1 MP | `fdt.dts:1913,4848-4856`; `X6886-specs.txt:148-174`; `all_files.txt:1174-1247` | Same skeleton, no gc13a0 table | Skeleton only | Missing: front sensor table, reset/MCLK pins, front CSI lane map. Bug: spec PDF `Supported` line names gc32e1, which is not the fitted sensor | Critical: image source |
| Optical fingerprint | Goodix under-display sensor, `tran_fp_legacy` on `spi3.0`; TEE via `mcDrvModule` | `fdt.dts:4558-4582,4605-4607`; `modules.txt:316-317,351`; `all_files.txt:9734,6394` | None. `redwood_goodix.c` is a touch driver, not a fingerprint driver | Not ported | Missing: FOD SPI driver, HBM spot control, trustlet interface. Mismatch: Goodix touch vs Goodix optical fp. Bug: none observed | None: separate biometric |
| Audio / mics | MT6366 codec + Awinic `aw87xxx_pa` amps, 2 devices | `fdt.dts:1777,3228-3229,5172-5180,5582,7627`; `modules.txt:147-150` | `redwood_sound.c:2-8`, `redwood_mic.c:2-8` target Qualcomm ADSP | Not ported | Missing: MT6366 AFE path. Mismatch: Qualcomm DSP services vs MediaTek AFE. Bug: none observed | None |
| Wi-Fi / BT / GPS | CONNSYS MT6789, `wlan_drv_gen4m_6789`, `bt_drv_connac1x`, `wmt_drv` | `fdt.dts:6955-6970,7571-7581,4477-4483`; `modules.txt:1-4,112,166-167` | `redwood_connsys.c:2-22`, `redwood_mtkwifi.c:2-16`, `redwood_mtkbt_skel.c:2-20` for MT6768 + MT6631 | Skeleton for BT only | Missing: MT6789 CONNSYS and Wi-Fi platform tables. Mismatch: different `reg` block list and firmware. Bug: none observed | None |
| GPU | Mali-G57 MC2, Valhall v9 Job Manager, `GPU_ID 0x90930010` from the X676C reference | `modules.txt:199-201`; `all_files.txt:11816`; `refs/heliodarwin/wiki/MT6789-GPU-Power-Sequence.md:3,19` | `MaliValhallHw.h:1-12` register map cross-checked against `mali_kbase_mt6789.ko`; JM transport patch `0189-...` compiled, unvalidated; `redwood_mali.c:2-17` is Bifrost | Not ported into this tree | Missing: no integrated X6886 JM driver. Mismatch: Bifrost vs Valhall; JM has no CSF firmware. Bug: patch 0189 says hardware execution remains unvalidated | Low: animation compositing |
| Battery / charging | MT6366 PMIC, `sc8548` master charger, `sc8950` secondary | `fdt.dts:3228-3229,5125-5133,5245-5255`; `modules.txt:43,47,52,147` | `redwood_mtkbatt.c:2-25` for MT6358 gauge | Not ported | Missing: MT6366 gauge and both charger drivers. Mismatch: different PMIC register map. Bug: reference has no charger chip driver at all | Low: power state |
| Buttons | `kp@10010000`, map `0x73`/`0x72`; `mt6366keys` | `fdt.dts:4002-4012,3356-3357`; `modules.txt:337-338,368`; `input.txt:1-5` | `redwood_mtkkeys.c:2-12`, same keypad base and map | Closest to reuse | Missing: X6886-specific enable and PMIC key bit. Mismatch: reference reads MT6358; board has MT6366. Bug: none observed | Low: passcode fallback |
| Vibrator | `regulator-vibrator`, 3.0-3.5 V, `vib-supply` | `fdt.dts:2333-2340`; `modules.txt:336` | `redwood_haptic.c:2-20` for AW86927 at I2C 0x5a | Not ported | Missing: PWM vibrator driver. Mismatch: no AW86927 on this board; `aw87xxx_pa` at 0x58/0x59 are audio amps. Bug: none observed | Low: haptic feedback |

## Display and panel

Stock identity:

| Item | Value | File:line |
| --- | --- | --- |
| Active panel string | `nt37706a_fhdp_dsi_vdo_dsc_boe_boe_144hz_x6886` | `fdt.dts:69`, `:1608` |
| Active panel node | `panel1@0`, compatible `nt37706a,...`, `reg = <0>` | `fdt.dts:6538-6555` |
| Spare panel node | `panel2@0`, `vtdr6126a,...`, `reg = <1>` | `fdt.dts:6557-6574` |
| DSI host | `dsi@14013000`, `mediatek,mt6789-dsi`, IRQ 0x12d | `fdt.dts:6525-6534`; `iomem.txt:123` |
| Overlay engines | OVL0 `0x14005000`, OVL0_2L `0x14006000` | `fdt.dts:6374-6388`; `iomem.txt:118-122` |
| DSC wrapper | `disp_dsc_wrap0@14012000` | `fdt.dts:6507-6508` |
| PWM backlight | `disp_pwm0@1100e000` | `fdt.dts:6314-6315` |
| Kernel modules | `mediatek_drm_v1`, `nt37706a_..._x6886`, `vtdr6126a_..._x6886` | `modules.txt:376,391-392` |

Flags:

- MISSING DRIVER. No XNU code programs the DSI host, the DSC wrapper, or the
  nt37706a init table. `refs/oosexgang-xnu27/docs/drivers.md:21-23` describes
  `MiniIOMobileFramebuffer.cpp` flipping LK's OVL0 layer between two pictures.
  That works because LK left scanout running. It does not initialize a panel.
- REGISTER MISMATCH. The task-supplied active layer is `0xfeab6000`, stride
  4320. `refs/heliodarwin/wiki/Display-Pipeline.md:14` gives
  `0xfeabe000`, stride 4320, for OVL0 layer 1 on the X676C. Stock
  `fdt.dts:93` gives `atag,videolfb-fb_base_l = 0xfe0c0000`. No local file
  supports `0xfeab6000`. Treat that address as unverified until a loader log
  or SWEET FB TRACE proves it.
- KNOWN BUG. `fdt.dts:97-99` reports 60 Hz and names
  `nt36672e_fhdp_dsi_vdo_60hz_jdi_dphy_drv`, while `:69` and `:6539` select
  the 144 Hz BOE panel. The bootloader's framebuffer tag is stale. Do not use
  `atag,videolfb-lcmname` for panel selection. Use `touch,lcm_name` or the
  panel compatible list.
- Gaps to close before code: vendor panel init sequence, DSC parameters,
  backlight register set, exact active layer address. The init sequence lives
  in `nt37706a_fhdp_dsi_vdo_dsc_boe_boe_144hz_x6886.ko`
  (`all_files.txt:11315`), not in the DTB.

## Touch

| Item | Value | File:line |
| --- | --- | --- |
| SPI controller | `spi1@11010000`, `mediatek,mt6765-spi`, IRQ 0xc0 | `fdt.dts:4486-4493`; `iomem.txt:96` |
| Child node | `touch@0`, `focaltech,fts` then `chipsemi,chsc_cap_touch` | `fdt.dts:4495-4496` |
| Clock / power | 8 MHz (`0x7a1200`), `vtouch-supply = <0x143>` | `fdt.dts:4499,4501` |
| GPIOs | IRQ 12, reset 116, IOVDD 15, AVDD 150, all on pinctrl `0x49` | `fdt.dts:4502-4505` |
| Geometry | `display-coords = <0 0 0x437f 0x95ff>`, max 10 touches | `fdt.dts:4506-4507` |
| Supplier strings | `ft3682g` in DTB, `focaltech_ft3683g` as loaded module | `fdt.dts:1608`; `modules.txt:70` |
| Firmware | `focaltech_ts_fw.bin` | `all_files.txt:1067` |

Flags:

- MISSING DRIVER. No XNU driver talks to `spi1` on MT6789. Four references
  exist, none of them on this bus:
  - `refs/xnu-12377-hakios/bsd/dev/board/fts.c:1-11` handles an FT3658U, chip
    ID `0x5652`, over Qualcomm GENI. `fts_protocol.h:1-14` verifies the report
    starts at register `0x01`, uses a 7-byte read prefix and a CRC.
  - `refs/hakios-mtk/xnu/files/bsd/dev/redwood_goodix.c:2-20` reads an FTS
    report at register `0x01` with `0xC1` read, `0xC0` write, CRC-16/Kermit over
    GENI (`redwood_goodix.c:272,352-392`).
  - `refs/heliodarwin/wiki/Input-and-HID.md:26,39,43` documents a working
    MT6789 driver for an FT5452J at I2C `0-0038`, module `ft3519_common`,
    report at register `1`, six-byte contact records, XNU patch
    `0192-x676c-real-touch-and-keys.patch`.
  - `refs/klios716/xnu/patches/0112-kl7-fts3519-touch.patch:1-20` adds an FT3519
    driver for another MT6789 board.

  The report decoding is reusable. The I2C and GENI transports are not: the
  X6886 controller sits behind `mediatek,mt6765-spi` at `0x11010000`
  (`fdt.dts:4486-4493`).
- REGISTER MISMATCH. `redwood_focaltech_fw.h:1,4,6` embeds 116448 bytes from
  `focaltech_ts_fw_m20.bin`. The stock X6886 blob is a different file. Compare
  sizes and headers before reusing any FW path.
- KNOWN BUG. The DTB supplier name `ft3682g` and the loaded module
  `focaltech_ft3683g` disagree. The compatible `focaltech,fts` does not name
  either. Fingerprint a chip ID over SPI before choosing a command set.
- The `focaltech,fod-function-support` flag at `fdt.dts:4508` marks touch
  report support for under-display fingerprint. It does not make the touch
  controller the fingerprint sensor.

## Cameras

| Item | Rear | Front | File:line |
| --- | --- | --- | --- |
| Sensor | `imx882_mipi_raw` | `gc13a0ff_mipi_raw` | `fdt.dts:1912-1913` |
| I2C child | `camera_main@2a`, `imx882_17201983` | `camera_sub@1a`, `gc13a0_17201982` | `fdt.dts:4690-4697,4848-4856` |
| Bus | I2C at `0x11eb0000`, index 2 | I2C at `0x11eb1000`, index 4 | `bus.txt`; `iomem.txt:39-41` |
| EEPROM | `camera_eeprom0@50` | `camera_eeprom1@51` | `fdt.dts:4682-4688,4841-4846` |
| AF | `camera_main_af@0c` | none | `fdt.dts:4700-4705` |
| Rails | `vcama`, `vcamd`, `vcamio`, `vcamaf` for cam0; `vcama1`, `vcamd`, `vcamio` for cam1 | same | `fdt.dts:1888-1911,3507-3517,3642-3643,3703-3704` |
| CSI | `mipi_tx_config@11f60000`, `mediatek,mt6789-mipi-tx` | same block | `fdt.dts:6515-6520` |
| Firmware / HAL data | `libCamera_imx882mipiraw_*.so`, HDR tables | `libCamera_gc13a0ffmipiraw_*.so`, HDR tables | `all_files.txt:752-766,1174-1247,1265+` |
| Optical format | 50 MP, f/1.7, RGGB | 13.1 MP, f/2.0, GRBG | `X6886-specs.txt:121-147,148-174` |

Flags:

- MISSING DRIVER. `redwood_mtkcam_skel.c:3-14` makes `/dev/redwood-camera`
  and waits for a `struct mtkcam_platform` table (`:63,92,98`). It holds no
  registers. `docs/camera-mtk-porting.md:8-21` states it has never run on a
  phone. `docs/camera-mtk-porting.md:23-45` gives the order: power, sensor ID
  and register table, SENINF/CAMSV, frame layout, exposure.
- REGISTER MISMATCH. `redwood_mtkcam.c:2-24` is an S5K JN1 on MT6768 with
  MT6358 rails. The X6886 uses imx882 and gc13a0ff with MT6366 rails. No
  value transfers. Camera 2 in the DTB is `fake_serial_yuv`
  (`fdt.dts:1914`), so a two-sensor port is enough.
- KNOWN BUG. `X6886-specs.txt:118-119` lists gc32e1x6850 and gc32e1x6853 as
  supported sensors. Those are other board SKUs. The fitted front sensor is
  gc13a0ff per the DTB.
- Face ID depends on cam1. Liveness needs the front stream at 30 fps or better
  (`X6886-specs.txt:172`).

## Under-display optical fingerprint

| Item | Value | File:line |
| --- | --- | --- |
| Node | `spi3@11013000`, IRQ 0xc2 | `fdt.dts:4549-4553`; `iomem.txt:98` |
| Child | `fingerprint@0`, `tran_fp_legacy`, CS0 | `fdt.dts:4558-4561` |
| Clock / IRQ | 27 MHz (`0x19bfcc0`), `interrupts = <0x0d 0x01 ...>`, `irq-gpio = 0x0d` | `fdt.dts:4561-4566` |
| Reset / power | `reset-gpio = 0x77`, `tran,gpio_pwr = 0x3b` | `fdt.dts:4567-4570` |
| FOD position | `fod_location_xy = "540,2210"` | `fdt.dts:4571` |
| Vendor flags | `tran_fp_sched`, `goodix` | `fdt.dts:4572-4573` |
| Alias | `goodix_fp = "/soc/fingerprint"`, compatible `mediatek,goodix-fp` | `fdt.dts:4605-4607,9188` |
| Kernel modules | `tran_fp`, `tran_fp_v2`, `spi_mt65xx`, `mcDrvModule` | `modules.txt:316-317,351,433` |
| User space | `tran_fingerprint.default.so`, `gf_fingerprint.default.so`, `tranhbm.jar` | `all_files.txt:9734,1071,6394` |

Flags:

- MISSING DRIVER. No XNU SPI fingerprint driver, no HBM spot control, no TEE
  client in refs. `mcDrvModule` (`modules.txt:351`) is Trustonic's kernel
  client; the match runs in a trustlet, not in the kernel.
- REGISTER MISMATCH. `redwood_goodix.c:2-3` is a Goodix GT9916 **touch**
  controller. The X6886 sensor is a Goodix **optical fingerprint** device.
  The shared vendor name does not make the driver reusable.
- KNOWN BUG. `all_files.txt:1071` ships `gf_fingerprint.default.so` beside
  `tran_fingerprint.default.so`. Which HAL binds depends on the fingerprint
  service configuration. Pick one before designing the XNU interface.
- HBM is a display-side dependency. `tranhbm.jar` (`all_files.txt:6394`)
  carries Transsion's HBM control on the Android side. Any XNU FOD work needs
  the display driver to dim the rest of the panel.

## Audio and microphones

| Item | Value | File:line |
| --- | --- | --- |
| Codec | `mt6789-mt6366`, `mediatek,mt6789-mt6366-sound` | `fdt.dts:1777,7627` |
| AFE PCM | `mt6789-afe-pcm@11210000` | `fdt.dts:5582-5592`; `iomem.txt:105` |
| Amplifiers | `aw87xxx_pa@58`, `aw87xxx_pa@59` | `fdt.dts:5172-5180` |
| Jack / keys | `mt6366` accdet and `mt6366keys` | `fdt.dts:3268-3271,3356-3357` |
| Input devices | `mt6789-mt6366 Headset Jack`, `mtk-tpd` | `input.txt:3-5` |
| Modules | `mt6789_mt6366`, `snd_soc_mt6366`, `snd_soc_mt6789_afe`, `snd_soc_aw87xxx`, `tran_audio_control` | `modules.txt:147-152` |

Flags:

- MISSING DRIVER. No XNU codec or AFE driver for MT6366.
- REGISTER MISMATCH. `redwood_sound.c:2-8` and `redwood_mic.c:2-8` drive
  Qualcomm ADSP services ASM, ADM, and AFE with SoundWire codecs. MT6789 uses
  AFE registers at `0x11210000` and the MT6366 PMIC codec. None of the DSP
  service numbers transfer.
- KNOWN BUG. `redwood_haptic.c` uses an AW86927 at I2C 0x5a. The X6886
  `aw87xxx_pa` devices at 0x58/0x59 are speaker amplifiers. Do not route
  haptic calls to those addresses.

## Wi-Fi, Bluetooth, GPS

| Item | Value | File:line |
| --- | --- | --- |
| CONNSYS | `consys@18002000`, `mediatek,mt6789-consys`, 11 register windows | `fdt.dts:6955-6970` |
| Wi-Fi | `wifi@18000000`, 1 MiB, IRQ 0x199, EMI 6 MiB | `fdt.dts:7571-7581` |
| BTIF | `btif@1100c000` | `fdt.dts:4477-4483` |
| Modules | `wlan_drv_gen4m_6789`, `bt_drv_connac1x`, `gps_drv_stp`, `wmt_drv`, `connadp`, `btif_drv` | `modules.txt:1-4,112,166-167` |

Flags:

- MISSING DRIVER. No MT6789 CONNSYS or Wi-Fi XNU driver.
- REGISTER MISMATCH. `redwood_connsys.c:2-22` uses the MT6768 register set
  around `0x18002000`, SPM `0x10006000`, and MCU `0x180B1000`. The X6886 DTB
  lists a different set of eleven windows at `:6960`. `redwood_mtkwifi.c:2-16`
  targets an MT6631 gen4m chip. Confirm the radio generation before loading
  any firmware.
- KNOWN BUG. The reference calls `redwood_connsys_init()` from
  `redwood_input.c` (`redwood_mtkbt_skel.c:2-20`). Porting it changes the boot
  order; keep each radio behind its own boot arg.

## GPU

| Item | Value | File:line |
| --- | --- | --- |
| Kernel module | `mali_kbase_mt6789_a16w_jm` (991232 bytes), plus `mali_mgm_...` and `mali_prot_alloc_...` | `modules.txt:199-201`; `all_files.txt:11816-11818` |
| Architecture | Mali-G57 MC2, Valhall v9, Job Manager | `refs/heliodarwin/wiki/MT6789-GPU-Power-Sequence.md:3` |
| Observed ID | `GPU_ID = 0x90930010`, `AS_PRESENT = 0xff`, `JS_PRESENT = 0x7` on X676C | `refs/heliodarwin/wiki/MT6789-GPU-Power-Sequence.md:19` |
| CSF | No CSF firmware in the dump; JM submission only | `all_files.txt` has no `csf` firmware entry; wiki line 3 |

Flags:

- MISSING DRIVER. No Valhall JM driver is integrated in this tree. Two
  references exist:
  - `refs/heliodarwin/MaliMetal/MaliValhallHw.h:1-12` is a register map for
    Mali-G57 MC2 Valhall v9 JM, cross-checked against this phone's
    `mali_kbase_mt6789.ko`. GPU MMIO base is `0x13000000`. Register offsets
    start at `:17-21`.
  - HelioDarwin patch `0189-x676c-malimetal-jm-gpu-userclient.patch:1-10`
    adds pinned buffers, GPU address spaces, serialized JM jobs and fault
    status. Its own header says "Hardware execution remains unvalidated" and
    that it compiled to arm64 objects but was never flashed.

  `redwood_mali.c:2-17` covers a Mali-G52 Bifrost part and states it needs no
  firmware. `redwood_mali.c:53-54` exposes a `kern.redwood.mali` progress
  counter; `:480-501` runs a write job in identity mode and through GPU page
  tables. That sequence belongs to Bifrost.
- REGISTER MISMATCH. Different architecture, different power domains,
  different job-slot registers.
- KNOWN BUG. Do not search for CSF microcode or use a CSF submission path. The
  module name ends in `_jm`, and the reference wiki says JM explicitly.

## Battery and charging

| Item | Value | File:line |
| --- | --- | --- |
| PMIC | `mediatek,mt6366` | `fdt.dts:3228-3229` |
| Gauge channels | `pmic_batadc`, `pmic_battery_voltage`, `pmic_battery_temp` | `fdt.dts:3822,3875` |
| Charger 1 | `southchip,sc8950` on I2C `5-006b`, IRQ GPIO 4 | `fdt.dts:5125-5133`; `iomem.txt` bus map |
| Charger 2 | `sc,sc8548-standalone` on I2C `7-006f`, 5 mOhm sense resistor | `fdt.dts:5245-5255` |
| Modules | `tc_sc8548_charger`, `tc_sc8950`, `tc_charger_class`, `mt6789_mt6366` | `modules.txt:43,47,52,147` |

Flags:

- MISSING DRIVER. No MT6366 gauge and no charger driver in XNU.
- REGISTER MISMATCH. `redwood_mtkbatt.c:2-25` reads an MT6358 fuel gauge and
  derives charge from `FGADC_CAR_CON0/1` at `0xd14`/`0xd16`. It also says no
  charger chip is driven.
- KNOWN BUG. `devices/x6886/evidence/dmesg.txt:5510` reports
  `power_supply charger: driver reporting unavailable enum value 1` on stock.
  Treat stock charger properties as imperfect until cross-checked.

## Buttons and vibrator

| Item | Value | File:line |
| --- | --- | --- |
| Keypad | `kp@10010000`, `mediatek,kp`, IRQ 0x6a, map `0x73 0x72` | `fdt.dts:4002-4012` |
| PMIC keys | `mt6366keys`, `mtk_pmic_keys` | `fdt.dts:3356-3357`; `modules.txt:338` |
| Vibrator | `regulator-vibrator`, 3.0-3.5 V, `vib-supply = <0x48>` | `fdt.dts:2333-2340`; `modules.txt:336` |
| Input names | `mtk-pmic-keys`, `mtk-kpd`, `tran-keys` | `input.txt:1-3` |

Flags:

- MISSING DRIVER. No XNU key polling and no vibrator driver.
- REGISTER MISMATCH. `redwood_mtkkeys.c:2-12` uses the same keypad base and
  the same `0x73`/`0x72` map, so its keypad half is close to reuse. Its PMIC
  half reads MT6358 `TOPSTATUS`; this board needs MT6366.
- KNOWN BUG. `redwood_haptic.c:2-20` expects an AW86927 at I2C 0x5a with a
  firmware waveform file. This board has a voltage-regulated vibrator node.
  A port must drive `vib-supply`, not an I2C haptic IC.

## Cross-check by repository

| Repo | Contains | Does not contain for X6886 |
| --- | --- | --- |
| `refs/xnu-12377-hakios` | Base XNU 12377 kernel, Mach, BSD, IOKit (`README.md:1-13`), plus `bsd/dev/board/fts.c`, `goodix.c`, `ft5x06.c`, `geni_spi.h` | MT6789-specific bus logic. Those touch drivers use Qualcomm GENI and RPMh, not MediaTek SPI |
| `refs/hakios-mtk` | `xnu/files/bsd/dev/redwood_*.c`: FTS touch, TD4160 touch, MT6768 camera, Bifrost Mali, MT6768 CONNSYS, MT6358 battery, keypad | MT6789 DSI, SPI-FTS, Valhall JM, MT6366 audio, MT6366 gauge, optical FOD |
| `refs/oosexgang-xnu27` | Feature and boot-arg table (`docs/drivers.md:9-19`), camera skeleton guide (`docs/camera-mtk-porting.md:8-21`) | Phone-independent halves only: BT and camera skeletons (`docs/drivers.md:19`) |
| `refs/unixnu-mt6789` | CPU affinity, GIC, timer DTS and config (`README.md:8-16`, `soc.toml`) | Display, touch, camera, GPU, radios, power. Its `AGENTS.md:1-3` forbids guessing registers |
| `refs/heliodarwin` | MT6789 GPU notes, X676C display notes (`wiki/MT6789-GPU-Power-Sequence.md:3,19`; `wiki/Display-Pipeline.md:12-15`), Valhall JM register map (`MaliMetal/MaliValhallHw.h:1-12`), MT6789 I2C FTS driver (`wiki/Input-and-HID.md:26,39,43`) | An X6886 board. Framebuffer addresses differ from the task-supplied value; its touch is I2C, ours is SPI |
| `refs/klios716`, `refs/ios-jellystar` | Other HakiOS device trees and patch sets, including `klios716/xnu/patches/0112-kl7-fts3519-touch.patch` for an MT6789 FT3519 | No X6886 driver contribution found for the components above |

## Bug-prevention checklist

1. Select the panel from `touch,lcm_name` (`fdt.dts:69`). Ignore
   `atag,videolfb-lcmname` at `:99`.
2. Confirm the active framebuffer address from a loader log or scanout trace.
   Do not use `0xfeab6000` until then. `Display-Pipeline.md:14` and
   `fdt.dts:93` give different addresses.
3. Read the FocalTech chip ID over `spi1` before loading FW. The DTB says
   ft3682g, the module says ft3683g (`fdt.dts:1608`, `modules.txt:70`).
4. Compare the length and header of `focaltech_ts_fw.bin` with
   `redwood_focaltech_fw.h:4,6` before copying any FW logic.
5. Treat `redwood_goodix.c` as touch code. Never wire it to the fingerprint
   node at `fdt.dts:4558`.
6. Keep the Goodix FOD sensor behind its own driver. It shares only the vendor
   name with the touch controller.
7. Generate sensor tables from the stock `.so` files and HDR blobs in
   `all_files.txt:752-766,1174-1247,1265+`. Do not copy S5K JN1 values from
   `redwood_mtkcam.c`.
8. Make the camera skeleton platform table complete before creating
   `/dev/redwood-camera` (`redwood_mtkcam_skel.c:8,415-422`). No guessed
   black level, pitch, or Bayer order.
9. Use the MT6789 CONNSYS register list at `fdt.dts:6960`, not the MT6768 list
   in `redwood_connsys.c:7-22`.
10. Keep the JM path for the GPU. No CSF firmware, no CSF submission
    (`all_files.txt:11816`, wiki line 3).
11. Read back every PMIC rail write. `camera-mtk-porting.md:25-31` records
    that LK and Android leave different VCAMD values.
12. Keep each driver behind its own boot arg. `docs/drivers.md:4-5` states
    that is how the reference tree isolates a failed probe.
13. Mark a driver done only after an X6886 kernel log line exists. Reference
    logs from X6826B, redwood, and X676C prove nothing on this board.
14. Do not touch `build/loader`, any kernel tree, or any boot image from this
    audit. This document is the only deliverable with `FACEID-2D-SPEC.md`.

## Open evidence

- nt37706a DSI init sequence and DSC parameters. Source: the panel `.ko` at
  `all_files.txt:11315`, not the DTB.
- FocalTech register map for ft3682g/ft3683g. Needs a chip ID read or the
  vendor Linux source. The MT6789 I2C report decoder in HelioDarwin is a
  starting point, not the X6886 SPI transport.
- Hardware validation of the Valhall JM transport in HelioDarwin patch 0189.
  The patch compiled but was never flashed.
- Goodix optical FOD part number and register map. The dump proves the vendor
  and the bus, not the exact part.
- MT6789 CONNSYS firmware filenames and load order.
- MT6366 AFE and gauge register values as Android leaves them.
- Front camera register table, reset/MCLK pins, and CSI lane count.
- Physical framebuffer base and stride for the active layer.
