# X6886 sensor inventory and XNU mapping

Read-only research, 24 Nov 2026. No adb, no fastboot, no writes to loader, kernel trees, boot images, or source files. This is the only file written by this pass.

Path convention: `otadump/` means `/home/berucrypt00/otadump`. `devices/x6886/evidence/`, `refs/`, and `docs/` are inside `x6886-hakios`. Subagent D's `docs/X6886-DRIVER-AUDIT.md` owns the full optical-fingerprint analysis; this file repeats only the bus facts needed for the sensor matrix.

## Evidence limits

| Fact | Source | Limit |
| --- | --- | --- |
| Chip driver IDs (`lsm6dso_acc`, `lsm6dso_gyro`, `qmc6308`, `mn79911_als`, `mn79911_ps`) | `devices/x6886/evidence/X6886-specs.txt:12-17` | These are Android driver names, not package part numbers. The dump has no datasheet or chip-ID read |
| SCP owns the motion and light/proximity chips | `refs/hakios-mtk/docs/sensors-x6826b.md:6-17`; loaded `sensorhub` at `devices/x6886/evidence/modules.txt:195` | The same pattern was proved on X6826B, not on X6886 |
| AP I2C/SPI device list | `devices/x6886/evidence/bus.txt:1-30`; `devices/x6886/evidence/fdt.dts:4632-5320` | No inertial or optical sensor child node appears on any AP bus |
| Calibration slots | `otadump/odm/etc/vconfig/tranfac/aidl/default/SensorNv.json:2-127` | The NVRAM values under `/mnt/vendor/nvcfg/sensor` are not in the dump |
| SCP firmware | no sensor FW under `otadump/vendor/firmware`, see `otadump/mtk/installed-files-vendor.txt` | SCP image lives in the `scp` partition loaded by LK; it is not dumped. Chip I2C addresses and sensor type IDs are inside that firmware |
| Live properties | `devices/x6886/evidence/getprop.txt:231,1711-1715,1801` | This capture is Android 17 (`getprop.txt:1951`), otadump is Android 16 (`otadump/all_files.txt` build tags). Sensor hardware agrees; property values may not match the Android 16 image |

## Part 1: active sensor inventory

### SCP sensor hub on X6886

- `scp@10500000`, compatible `mediatek,scp`, status `okay`, SRAM `0x10500000` size `0xc0000`, cfg `0x10724000`, clk `0x10721000`. The DTB names mailbox windows `mbox0` to `mbox4` at `0x107fb000`, `0x107fc000`, `0x107fd000`, `0x107fe000`, `0x107ff000`. `devices/x6886/evidence/fdt.dts:5514-5518`; Linux reports `mbox0` to `mbox3` at `devices/x6886/evidence/iomem.txt:78-89`.
- SCP interrupts: `0x1d2`-`0x1d8`, names `ipc0`, `ipc1`, `mbox0`-`mbox4`. `devices/x6886/evidence/fdt.dts:5519-5520`.
- Reserved memory: `mediatek,reserve-memory-scp_share` at `0x8f000000` size `0x6c0000`, `mediatek,SCP-reserved` at `0xbf200000` size `0x220000`. `devices/x6886/evidence/fdt.dts:573-583`.
- SCP rails: `dvfsrc-vscp-supply`, `sshub-vcore-supply`, `sshub-vsram-supply`. `devices/x6886/evidence/fdt.dts:4131-4133`. No per-sensor rail is named in the AP tree.
- Modules on the running stock kernel: `sensorhub`, `scp`, `mtk_tinysys_ipi`, `mtk_mbox`, `hf_manager`, `elliptic_ultrasound`. `devices/x6886/evidence/modules.txt:195,197,224-228`.
- Vendor blobs: `sensorhub.ko` at `otadump/all_files.txt:11911`, `scp.ko` at `:11910`, MediaTek sensor HAL `sensors.mediatek.V2.0.so` at `:9731`, multihal service at `:8380`.
- `tran_sensor_cust` exposes `sensor_type = <0x01 0x02 0x04 0x05 0x00 0x00>` and `sensor-get-screen-power-state`. `devices/x6886/evidence/fdt.dts:338-347`. No binding in the dump decodes that bitmask, so this file does not assign meanings to the values.

Kernel side: GKI keeps generic sensor drivers off. `CONFIG_HWMON` is not set (`otadump/kernel_config.txt:3486`), `CONFIG_HID_SENSOR_HUB` is not set (`:5074`), `CONFIG_IIO_ST_LSM6DSX` is not set (`:6569`), `CONFIG_BMC150_MAGN_I2C` is not set (`:6641`). `CONFIG_IIO=y` at `:6203` does not bring up any board sensor. Linux reaches the chips only through `sensorhub.ko` and SCP IPI.

### Sensor matrix

| Sensor | Exact IC / ID from stock | Bus and address from dump | Interrupt and GPIO | Rails | Evidence | XNU status |
| --- | --- | --- | --- | --- | --- | --- |
| Accelerometer | `lsm6dso_acc` (ST LSM6DSO) | SCP-owned I2C. No AP slave address in dump. AP I2C children are NFC, camera, PMIC, audio, TCPC only | No AP GPIO. Chip INT1 belongs to SCP | SCP domain rails `dvfsrc-vscp`, `sshub-vcore`, `sshub-vsram`; no chip rail named | `devices/x6886/evidence/X6886-specs.txt:12`; `otadump/odm/etc/vconfig/tranfac/aidl/default/SensorNv.json:2-8`; `devices/x6886/evidence/bus.txt:1-30` | SCP bring-up ported on MT6768, not on MT6789 |
| Gyroscope | `lsm6dso_gyro` | Same SCP I2C, address not dumped | SCP-owned | Same SCP rails | `devices/x6886/evidence/X6886-specs.txt:17`; `SensorNv.json:9-15`; `otadump/all_files.txt:854` | Reference reads rates from the same IMU record (`refs/hakios-common/drivers/board/imu.c:59-69`) |
| Magnetometer | `qmc6308` | SCP I2C, address not dumped | SCP-owned | Same SCP rails | `devices/x6886/evidence/X6886-specs.txt:16`; `otadump/all_files.txt:853`; HAL string `qmc6308` in `sensors.mediatek.V2.0.so` | No compass service or heading bridge in refs |
| Ambient light | `mn79911_als` | SCP I2C, address not dumped. Under-screen light uses SCP CWB path | SCP-owned | Same SCP rails | `devices/x6886/evidence/X6886-specs.txt:14`; `otadump/odm/etc/vconfig/sensor/sensor_config.json:4`; `SensorNv.json:40-80`; `otadump/all_files.txt:855` | No XNU implementation; brightness path needs display driver |
| Proximity | `mn79911_ps` | SCP I2C, address not dumped | SCP-owned | Same SCP rails | `devices/x6886/evidence/X6886-specs.txt:15`; `SensorNv.json:16-27`; `otadump/all_files.txt:856` | No XNU implementation |
| Anti-touch proximity | Derived from `mn79911_ps` plus under-screen ALS | SCP virtual sensor, no separate chip | SCP-owned | SCP rails | `SensorNv.json:28-39` (`ANTI_TOUCH_PS`, `HWMON_ALSNUMBER18BIT32`) | No XNU implementation |
| Step counter / step detector / tilt / wake gesture | Software sensors from the accelerometer | No chip, no bus | none | none | `otadump/all_files.txt:857-858`; HAL strings `android.sensor.step_counter`, `step_detect_wakeup`, `tilt_detector`, `wake_gesture` in `sensors.mediatek.V2.0.so` | Later; needs accel stream first |
| Linear Hall / cover detect | `HWMON_3DHALL` calibration slot | SCP, chip not named in dump | SCP-owned | SCP rails | `SensorNv.json:95-100` | Later; no IC or bus in dump |
| SAR | Awinic SAR assist via `hf_manager` | No bus or address in dump | none on AP | none named | `SensorNv.json:109-114`; `devices/x6886/evidence/dmesg.txt:867-870`; `devices/x6886/evidence/modules.txt:197` | Later; radio power feature, not UI |
| Ultrasound proximity | Elliptic sensor, reader `hexagon` | Audio DSP path (`elliptic_ultrasound`, AFE), not I2C or SPI | `audio_notify = ultrasound-proximity` | AFE audio path | `otadump/vendor/etc/elliptic_sensor.xml:4-14`; `otadump/all_files.txt:8696,9730`; `devices/x6886/evidence/modules.txt:224` | Later; needs audio DSP |
| Optical under-display fingerprint | Goodix, `tran_fp_legacy`; alias `mediatek,goodix-fp` | `spi3@11013000`, CS0 (`reg = <0>`), 27 MHz. No slave address: SPI | IRQ EINT `0x0d` / GPIO 13 (`interrupts = <0x0d 0x01>`), reset GPIO `0x77` (119), power GPIO `0x3b` (59). SPI controller IRQ `0xc2` | GPIO power `tran,gpio_pwr`; no regulator node | `devices/x6886/evidence/fdt.dts:4549-4581,4605-4607`; `devices/x6886/evidence/iomem.txt:98`; `devices/x6886/evidence/modules.txt:316-317,351,433`; `otadump/all_files.txt:9734,1071` | Later; full analysis in `docs/X6886-DRIVER-AUDIT.md:138-165` |
| Thermal | Board NTCs and PMIC 6366 channels | AP thermal zones, not SCP | thermal zones | PMIC rails | `devices/x6886/evidence/fdt.dts:837-1266` | Later; covered in `docs/X6886-PMIC-THERMAL.md` |

### Optical fingerprint details for the matrix

| Item | Value | File:line |
| --- | --- | --- |
| SPI controller | `spi3@11013000`, `mediatek,mt6765-spi`, IRQ `0xc2` | `devices/x6886/evidence/fdt.dts:4549-4556`; `devices/x6886/evidence/iomem.txt:98` |
| Child | `fingerprint@0`, CS0, `tran_fp_legacy`, status `okay` | `devices/x6886/evidence/fdt.dts:4558-4581` |
| Clock | `0x19bfcc0` = 27 MHz | `devices/x6886/evidence/fdt.dts:4561` |
| GPIOs | irq `0x0d`, reset `0x77`, power `0x3b`, all under `pinctrl@10005000` | `devices/x6886/evidence/fdt.dts:4562-4570` |
| FOD centre | `540,2210` in the DTB | `devices/x6886/evidence/fdt.dts:4571` |
| Vendor flags | `tran_fp_sched`, `goodix` | `devices/x6886/evidence/fdt.dts:4572-4573` |
| Kernel modules | `tran_fp`, `tran_fp_v2`, `mcDrvModule`, `spi_mt65xx` | `devices/x6886/evidence/modules.txt:316-317,351,433` |
| User space | `tran_fingerprint.default.so`, `gf_fingerprint.default.so`, `tranhbm.jar` | `otadump/all_files.txt:9734,1071,6394` |
| Property cross-check | optical type, location `540::2172`, Goodix SPMT `PASS`, `gf.debug.fp_serial` present | `devices/x6886/evidence/getprop.txt:1714-1715,1801,231` |

The DTB location and the property location differ by 38 px in y. Pick the source before drawing the FOD circle.

## Part 2: reference research and XNU mapping

### SCP and sensor-hub IPC

| Reference | What it gives | File:line |
| --- | --- | --- |
| `redwood_scp.c` identifies the SCP as the sensor hub, with accel/gyro/light/proximity behind it | Rationale and register map for MT6768 | `refs/hakios-mtk/xnu/files/bsd/dev/redwood_scp.c:1-23,38-56` |
| Direct hub request: wake through `INFRA_IRQ_SET`, send object at TCM `0x6e0`, read at `0x5c0`, `IPI_SENSOR = 8` | Wire protocol, poll loop, ack | `refs/hakios-mtk/xnu/files/bsd/dev/redwood_scp.c:64-77,102-143` |
| Nanohub over `IPI_CHRE` 24, ack `IPI_CHREX` 25, ConfigCmd 20 bytes, CRC32 | Enable/disable any hub sensor | `refs/hakios-mtk/xnu/files/bsd/dev/redwood_scp.c:145-211,240-262` |
| Accel ID 1 at 50 Hz, DRAM FIFO before config | Bring-up order that avoids the SCP watchdog | `refs/hakios-mtk/xnu/files/bsd/dev/redwood_scp.c:408-443` |
| FIFO record 44 bytes, x/y/z at bytes 12/16/20, hub units 1000 per m/s^2 | Sample decode | `refs/hakios-mtk/xnu/files/bsd/dev/redwood_scp.c:421-452`; `refs/hakios-mtk/docs/sensors-x6826b.md:40-41` |
| Thread that keeps taking hub messages, exposes `kern.redwood.accel` | Runtime shape | `refs/hakios-mtk/xnu/files/bsd/dev/redwood_scp.c:472-521` |
| Working X6826B bring-up, steps 1 to 6, and what is still open | Evidence this path ran on a phone | `refs/hakios-mtk/docs/sensors-x6826b.md:27-45` |
| Driver table: `scp_step=N`, feature `scp-sensors`, phone-dependent part is the firmware layout | Boot gating convention | `refs/oosexgang-xnu27/docs/drivers.md:4-5,14` |

### IOKit, HID, CoreMotion bridges

| Reference | What it gives | File:line |
| --- | --- | --- |
| Accelerometer HID service, Apple usage page `0xff00`, usage `0x03`, event type 13, `IOHIDEventCreateAccelerometerEvent`, `ReportInterval` from locationd | CoreMotion accel feed | `refs/hakios-common/userland/userland/src/hid_motion.h:1-27,101-102,136-146,191-199` |
| Gyroscope HID service, usage `0x09`, event type 20, `IOHIDEventCreateGyroEvent`, rates in rad/s | CoreMotion gyro feed | `refs/hakios-common/userland/userland/src/hid_gyro.h:1-20,48-51,145-153` |
| No compass service: heading from CoreMotion is absent | Explains why magnetometer is later | `refs/hakios-common/userland/userland/src/hid_motion.h:29-31` |
| `kern.redwood.orientation_state` between kernel accel and backboardd's `orientation_start` | Orientation injection without HID motion | `refs/hakios-mtk/xnu/files/bsd/dev/redwood_scp.c:495-502`; `refs/hakios-mtk/userland/src/inject.c:1866-1878,2022-2042` |
| `sensors_probe.m` reads MobileGestalt, CoreLocation heading, CoreMotion availability and samples | Verification tool after bring-up | `refs/hakios-common/userland/userland/src/sensors_probe.m:1-13,25-31,64-80` |
| Signed-permutation mounting, scale to milli-g/milli-rad/s/milli-uT, gyro sign inversion rule | Calibration and orientation model | `refs/hakios-common/drivers/board/sensor_axes_core.h:9-17,28-35,55-68,70-79`; `refs/hakios-common/drivers/board/sensors_core.h:1-11,31-43` |
| `imu-lsm6dso` on AP I2C, address `0x6a` or `0x6b`, WHO_AM_I `0x6c`, polled INT1 | Register-level XNU driver shape, but only for boards where the AP owns the bus | `refs/hakios-common/arch/arm64/boot/dts/_template/board.dts:192-210`; `refs/hakios-common/drivers/board/imu.c:1-20,24-33` |
| The base XNU tree also carries that IMU driver at `bsd/dev/board/imu.c`, with `lsm6dso_core.h` and the same `hw.sensors.*` sysctls | Compiled code for the same LSM6DSO ID, gated by a board feature | `refs/xnu-12377-hakios/bsd/dev/board/imu.c:1-20,24-33`; `refs/xnu-12377-hakios/bsd/dev/board/lsm6dso_core.h:1-12` |
| X6886 board row in that tree has `.features = 0` and `HAKIOS_BACKLIGHT_SOFT`; `imu-lsm6dso` is listed among feature names but not enabled | The driver is present and off on this board | `refs/xnu-12377-hakios/bsd/dev/board/board_table.h:20-26,32` |
| `hakios_als_report(millilux, source)` feeds `hw.backlight.lux` and the automatic curve | Seam for a real ambient-light sensor once SCP light works | `refs/xnu-12377-hakios/bsd/dev/board/backlight.c:15-23,637-646` |

`imu.c` does not apply here. Its own header says it is only for boards where the application processor drives the chip bus. On X6886 the bus belongs to the SCP, and the x6886 board row leaves the feature off. Reuse its axis math and its `hw.sensors.*` contract, not its bit-banged I2C or its register writes.

### Implementation order

| Order | Sensor group | Reason | First reference to port |
| --- | --- | --- | --- |
| 1 | Accelerometer | Auto-rotate, shake, and the base sample for step and tilt | `redwood_scp.c:1-23,389-455` then `hid_motion.h` and `sensors_core.h` |
| 2 | Ambient light and proximity | Screen brightness and call screen-off; both visible to UI at once | `redwood_scp.c:145-262` with hub type IDs still to be found, then `backlight.c:637-646` for the lux feed |
| 3 | Gyroscope | Same FIFO record as accel, needs its own HID service | `redwood_scp.c:421-452` then `hid_gyro.h` |
| 4 | Step, tilt, gesture, wake | Software derived from the accel stream | HAL sensor names in `sensors.mediatek.V2.0.so` and `otadump/all_files.txt:857-858` |
| 5 | Magnetometer / compass | Needs qmc6308 config, NVRAM calibration, and a new HID compass service | `devices/x6886/evidence/X6886-specs.txt:16`; `sensors_probe.m:5-13` |
| 6 | Thermal, linear Hall, SAR, ultrasound proximity | Board power and radio features, no UI dependency | `docs/X6886-PMIC-THERMAL.md`; `SensorNv.json:95-114`; `otadump/vendor/etc/elliptic_sensor.xml` |
| 7 | Optical fingerprint | Separate SPI3 device, TEE trustlet, HBM display control | `docs/X6886-DRIVER-AUDIT.md:138-165` |

Immediate boot and UI sensors are rows 1 to 3. Rows 4 to 7 wait until the SCP stream and display path work.

### Missing drivers and gaps

- No XNU driver talks to the SCP on MT6789. `refs/hakios-mtk/xnu/files/bsd/dev/redwood_scp.c` was written for the MT6768's `0x105c0000` cfg and `0x105c4000` clkctrl (`:38,47`). X6886 uses cfg `0x10724000` and clk `0x10721000` (`devices/x6886/evidence/fdt.dts:5517`; `devices/x6886/evidence/iomem.txt:67-68`). Reading the reference constants as-is will touch the wrong registers.
- The reference uses TCM share objects at `0x5c0` and `0x6e0` with `GIPC_IN` at cfg+`0x28` (`redwood_scp.c:64-77`). X6886 adds four mailbox register windows at `0x107fb000` onward (`devices/x6886/evidence/iomem.txt:78-89`) and exposes `mbox0`-`mbox4` interrupts (`devices/x6886/evidence/fdt.dts:5519-5520`). Which transport the X6886 firmware prefers stays unknown until the SCP image is read.
- The reference allocates the hub FIFO at `0x4e000000` (`redwood_scp.c:432-442`). X6886 reserves `0x8f000000` + `0x6c0000` and `0xbf200000` + `0x220000` for SCP memory (`devices/x6886/evidence/fdt.dts:573-583`). Pick a FIFO inside an X6886 reserved region and keep it clear of XNU.
- Nanohub type IDs exist in the reference only for the accelerometer (`redwood_scp.c:389-393,408-413`). Light, proximity, magnetometer, Hall, and SAR IDs are not in `refs/` and not in the dump.
- SCP firmware is absent. The `scp` partition is not in `otadump`, and `otadump/mtk/installed-files-vendor.txt` lists no sensor-hub FW blob. Exact chip slave addresses, chip IDs, and per-board sensor maps sit in that firmware.
- No AP I2C or SPI driver exists in refs for these chips. `spi_mt65xx` serves fingerprint and touch only (`devices/x6886/evidence/modules.txt:433`); the touch driver is `focaltech_ft3683g` on `spi1.0` and is a different device. The XNU `imu.c` in `refs/xnu-12377-hakios` and `refs/hakios-common` is compiled for an AP-owned LSM6DSO bus; the x6886 board row sets `.features = 0` (`refs/xnu-12377-hakios/bsd/dev/board/board_table.h:20-26`), so it adds no sensor on this board.
- No XNU mbox or tinysys IPI driver exists in refs. `mtk_tinysys_ipi.ko` and `mtk_mbox.ko` are Linux-only (`devices/x6886/evidence/modules.txt:226,228`).
- No gyro sign and accel sign validation exists for X6886. The dump contains no `mount-matrix` for motion sensors. Use `sensor_axes_core.h:28-35` and determine the permutation on the phone with `sensors_probe.m`. The ST part reads +1 g along the up axis while Darwin wants -1 g (`board.dts:204-207`; `sensor_axes_core.h:70-79`).
- Hub units are 1000 per m/s^2 (`sensors-x6826b.md:40-41`), HID motion expects milli-g (`hid_motion.h:15-18`). Convert; do not pass hub counts straight to `IOHIDEventCreateAccelerometerEvent`.
- Calibration paths exist (`SensorNv.json:2-127`) but their NVRAM values are not dumped. `sensors.mediatek.V2.0.so` names `iFilePRODUCT_HWMON_MAG_LID`, `acc_bias.json`, `gyro_bias.json`, `mag_cali.json`, `als_cali.json`, and `/mnt/vendor/nvcfg/sensor/gls_inject_location_cfgs.bin` as inputs; none of those data files are in otadump.
- `sensorhub.ko` strings show under-screen light CWB handling (`tran_cwb_msg`, `als_cwb_info`, "under screen sensor support info"). The API that dims the panel for FOD and under-screen ALS lives in Android's display stack (`tranhbm.jar`, `otadump/all_files.txt:6394`) and has no XNU counterpart.
- Two fingerprint HALs ship: `gf_fingerprint.default.so` (`otadump/all_files.txt:1071`) and `tran_fingerprint.default.so` (`:9734`). Pick one before designing the XNU interface. D audit already flags this at `docs/X6886-DRIVER-AUDIT.md:160-162`.
- HAL strings for `bmp280`, `bmp390`, `akm09918`, `akm09919`, `icm4263x`, and `lsm6dsm` exist inside `sensors.mediatek.V2.0.so`. They are compiled-in generic driver names. No X6886 evidence supports treating any of them as fitted parts.

## Open evidence to collect

1. SCP image from the `scp` partition: sensor type IDs, chip I2C addresses, orientation matrix.
2. Stock `sensorhub` driver source or disassembly for the MT6789 mailbox layout.
3. `/mnt/vendor/nvcfg/sensor` calibration blobs for ACC, GYRO, ALS, PS, MAG, SAR, and 3DHALL.
4. Exact Goodix optical part number and register map. The dump proves vendor and bus only.
5. On-phone check of auto-rotate and `sensors_probe.m` output after the first SCP bring-up.
