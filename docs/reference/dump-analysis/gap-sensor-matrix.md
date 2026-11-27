# X6886 (MT6789) — Sensor orientation / mount matrix gap analysis

Analysis date: 2026-11-23
Mode: **read-only**. Sources: stock OTA dump `/home/berucrypt00/otadump` plus live-device captures under
`/home/berucrypt00/hakios-x6886/devices/x6886/evidence/` (`fdt.dts`, `fdt.dtb`, `dt-nodes.txt`, `dmesg.txt`,
`getprop.txt`, `bus.txt`, `modules.txt`, `X6886-specs.txt`). No phone access, no driver code written.

## VERDICT: NEEDS LIVE

No mount/orientation matrix, axis map, or `direction` value exists in any local source. Sensor
**identity** is fully confirmed locally; the **axis transform is not**. The transform is compiled as
opaque binary data inside the SCP sensorhub firmware (`images/scp.img`) and is not recoverable as
text from the dump. Live capture is required (exact commands in §7).

---

## 1. VERIFIED — `sensor_config.json` read in full (no mount matrix)

Path: `/home/berucrypt00/otadump/odm/etc/vconfig/sensor/sensor_config.json` — 402 bytes, exact content:

```json
{
    "tran_sensors": [
        {
            "type": "als_ml",
            "parameters": {
                "screentype": "Screen"
            }
        },
        {
            "type": "sample_type",
            "parameters": {
                "double_params": 0.1,
                "integer_params": 10,
                "string_params": "sample_code"
            }
        }
    ]
}
```

Every key, exhaustively:

| Level | Key | Value |
|---|---|---|
| root | `tran_sensors` | array, 2 entries |
| `[0]` | `type` | `"als_ml"` |
| `[0].parameters` | `screentype` | `"Screen"` |
| `[1]` | `type` | `"sample_type"` |
| `[1].parameters` | `double_params` | `0.1` |
| `[1].parameters` | `integer_params` | `10` |
| `[1].parameters` | `string_params` | `"sample_code"` |

No `mount`, `orientation`, `matrix`, `axis`, `direction`, `negate`, `rotate`, or chip-name key exists.
Entry `[1]` is a template/example placeholder (`"sample_code"`, `double_params`, `integer_params`) —
this file is a Transsion ALS/ML stub, **not** the IMU orientation source. The vendor HAL does open
this exact path (string `/odm/etc/vconfig/sensor/sensor_config.json` + `Successfully read sensor_config type: %s`
in `vendor/lib64/hw/sensors.mediatek.V2.0.so`), so the path is confirmed live-relevant.

**Earlier claim ("no mount matrix") verified correct.** A second slot file `odm_b/etc/vconfig/sensor/sensor_config.json`
is listed in `all_files.txt` but is not present in the extracted slot-A tree.

## 2. VERIFIED — sensor identity (chip names)

| Source | Exact evidence |
|---|---|
| `images/scp.img` (SCP firmware) | `lsm6dso_acc`, `lsm6dso_gyro`, `lsm6dso_temp`, `lsm6dso_any_motion`, `lsm6dso_acc_self_test`, `lsm6dso_gyro_self_test`, `lsm6dso parse dts fail:%d`; `qmc6308`, `qmc6308_parse_dts exit`, `qmc6308_self_test`, `qmc6308_soft_reset`, `qmc6308 QST_ical_init successful`, `qmc6308 can't find hw gyro switch to DOS`; `mn79911_als`, `mn79911_ps`, `mn7991x_parse_dts`, `mn7991x_get_cfg_dts`, `[MN7991X]` log family |
| `vendor/lib64/hw/sensors.mediatek.V2.0.so` (383,592 B) + symlink `sensors.mt6789.so` | same chip set: `lsm6dso_acc`, `lsm6dso_gyro`, `lsm6dso_temp`, `qmc6308`, `mn79911_als`, `mn79911_ps`, plus `lsm6dsm_*`, `lsm6dsvq_*`, `icm4n607_*`, `bmi160/bmi2xy/bmi320_gyro` |
| Live FDT `fdt.dts` node `/tran_sensor_cust` | `compatible = "mediatek,tran_sensor";` `sensor_type = <0x01 0x02 0x04 0x05 0x00 0x00>;` → Android types 1=accel, 2=magnetic field, 4=gyro, 5=light |
| `X6886-specs.txt` lines 12–17 | `Accelerometer lsm6dso_acc` / `Als/ps mn79911_als, mn79911_ps` / `Magnetometer qmc6308` / `Gyroscope lsm6dso_gyro` |

Confirmed active stack: **lsm6dso (acc+gyro) + qmc6308 (mag) + mn79911 (ALS/PS)** behind SCP.
`icm4n607`, `hx32062se`, `bmi*`, `lsm6dsm`, `lsm6dsvq` strings are multi-SKU build remnants — presence
in firmware does **not** mean fitted on X6886 (INFERENCE; no local evidence of board population).

## 3. VERIFIED — config actually lives inside SCP firmware, as binary

`images/scp.img` = 546,272 bytes, MTK multi-section image, header magic `0x58881688`:

| Offset | Section | Size field |
|---|---|---|
| `0x000000` | `tinysys-scp-RV55_A` | 509,440 (0x7c600) |
| `0x07c800` | `cert1` | 1,733 |
| `0x07d0d0` | `cert2` | 1,047 |
| `0x07d6f0` | `tinysys-scp-RV55_A_dram` | 28,152 |
| `0x0846f0` | `cert1` | 1,733 |
| `0x084fc0` | `cert2` | 1,047 |

Evidence that SCP holds sensor config (all from `strings -a images/scp.img`):

- Source paths: `middleware/sensorhub/core/dts.c`, `middleware/sensorhub/comm/transceiver.c`,
  `drivers/common/i2c/i2c_regmap/src/i2c_regmap.c`
- DTS machinery: `init_with_dts`, `%s.dts name %s key %s can't find`, `bus_id`, `slave_addr`, `eint_num`
- Property key strings: **`direction`** (offset `0x4c319`), **`orientation`** (offset `0x4d0f8`),
  debug format **`direction: %u`** (offset `0x50208`)
- Driver DTS parsers: `lsm6dso_parse_dts`, `qmc6308_parse_dts exit`, `mn7991x_parse_dts`,
  `mn7991x_get_cfg_dts`
- Fusion: `QST_fusion_enable`, `geo_rotvec`, `uncali_mag` — sensor fusion runs **inside SCP**

Byte-level checks:
- No FDT magic `0xd00dfeed` anywhere in `scp.img` → the internal DTS is a proprietary tinysys blob, not a parseable DTB.
- No `axis_map`, no `negate`, no ASCII matrix literals anywhere in `scp.img`.
- `mn79911` at `0x4d044`, `orientation` at `0x4d0f8`, `direction: %u` at `0x50208` — all in the string table; the associated numeric values are compiled data, not text.

Live AP FDT confirms no per-chip nodes: `fdt.dts` (350,179 B) sensor-related nodes are only
`/tran_sensor_cust` (`mediatek,tran_sensor`) and `/mtk_ssc` (`mediatek,ssc`, `ssc-enable = <0x00>`).
Grep for `axis_map|negate|orient|mount|rotat|matrix|direction` in `fdt.dts` → only `gpio_usage_mapping`.
`dt-nodes.txt` shows the same (`/sys/firmware/devicetree/base/tran_sensor_cust/*`, `/mtk_ssc/*`).

**Conclusion:** the matrix exists, but only as compiled numbers in the SCP binary. Extracting it would
require symbol-bearing firmware or RE of tinysys data structures; there is no text to quote.

## 4. VERIFIED — exhaustive negative results (dump + live captures)

| Source | Result |
|---|---|
| `odm/etc/vconfig/tranfac/aidl/default/SensorNv.json` (read fully) | Calibration paths only: `HWMON_ACC`, `HWMON_GYRO`, `HWMON_PS`, `HWMON_ALS`, `HWMON_SUB_ACCEL`, `HWMON_SUB_GYRO`, `HWMON_3DHALL`, `HWMON_SAR`, etc., each with `caliType`/`intSize`. **No orientation/matrix/axis key.** |
| `odm/etc/vconfig/tranfac/feature/Sensor.json` | Empty feature config; only `tranfac.sensor.fingerprint.optical`. |
| `vendor/etc/sensors/hals.conf` | One line: `android.hardware.sensors@2.X-subhal-mediatek.so` |
| `vendor/etc/elliptic_sensor.xml` | Only Elliptic Proximity ranges (`near 3.0`, `far 5.000030`). No IMU. |
| `vendor/etc/init/hw/init.sensor_2_0.rc`, `vendor/etc/init/android.hardware.sensors-service-multihal.rc` | Calibration dir + HAL service only. |
| All `build.prop` / `default.prop` in dump | Sensor-related flags only: `ro.vendor.mtk.sensor.support=yes`, `ro.vendor.init.sensor.rc`, `ro.vendor.tran.sensor.rgt.devinfo=1`, `ro.vendor.custom_kernel_alsflicker_sensor=1`, `persist.sys.sensor.disable.fusion_phone_prox`, `ro.tr_sensor.*`, `ro.odm.tr_sensor.hr_spo2.timeoutmillis=16`, `vendor.aovstream.scp_pwr_sensor=0`. No orientation value. |
| Live `getprop.txt` | No sensor orientation prop. Only `init.svc.vendor.sensors-hal-multihal=running`, `persist.sys.sensor.disable.fusion_phone_prox=false`, `ro.surface_flinger.primary_display_orientation=ORIENTATION_0` (**display**, not IMU), fingerprint sensor-location props. |
| Live `dmesg.txt` (28,854 lines) | **Zero** IMU/sensor init/orientation lines. (Apparent hits were false positives: `halSetFWOwn` contains `alS` → case-insensitive `als`.) |
| Config-file grep (`odm/etc`, `vendor/etc`; json/xml/prop/rc/conf/cfg/txt) | Only orientation hits are **microphone** vectors in `MicInfo_AudioParam.xml` / `vendor/etc/audio_param/MicInfo_AudioParam.xml` (`0,-1,0` and `0,0,-1`) and **camera** `"orientation": 90/270` in `odm/etc/vconfig/camera/jsonparams/TranOtpConfig.json`. **These are NOT IMU and must not be reused.** |
| `.ko` strings: `sensorhub.ko`, `mtk-ssc.ko`, `mtk-ssc-dbg-v2.ko`, `hf_manager.ko` | SCP comm, calibration, `sensor_list`, `hf_client_*`, `tran_get_g_sensor_val`. No axis/orientation/mount values. |
| HAL `.so` strings: `sensors.mediatek.V2.0.so`, `android.hardware.sensors@2.X-subhal-mediatek.so`, `sensors.dynamic_sensor_hal.so`, `sensors.elliptic.so`, `libtran_sensorhub_oismgr.so`, `libSensorIdJSONConvertor.so` | Chip names, sensor types, `orientation` as a sensor-name string (`android.sensor.orientation`, `dev_orient`), config path. **No numeric matrix.** |
| Live `bus.txt` | AP I2C devices only; IMU sits on SCP-owned I2C, not exposed here. No chip addresses for lsm6dso/qmc6308/mn79911. |

## 5. INFERENCE — explicitly unverified, do not adopt as fact

- **[INFERENCE]** Android device frame is X=right, Y=top, Z=out-of-screen in portrait. A bar-type phone
  commonly mounts the IMU with its package axes aligned to device axes up to a rotation about Z (0/90/180/270)
  and often a Z sign flip because the chip faces the rear of the board. **No local evidence selects which
  of these applies to X6886.** Do not encode a guessed matrix.
- **[INFERENCE]** `QST_fusion_enable` + `geo_rotvec` strings mean SCP already applies its own orientation to
  fused outputs. If the xnu port re-applies a matrix after receiving events, it will double-rotate.
  Determine SCP's convention once, then apply at most one transform.
- **[INFERENCE]** `ro.surface_flinger.primary_display_orientation=ORIENTATION_0` (portrait-native panel)
  does not imply sensor rotation = 0; the opposite is common.
- **[INFERENCE]** Mic/camera orientation values in §4 belong to those subsystems only.

## 6. What is still unknown

1. Exact 3×3 accel matrix (axis permutation + signs) as seen by Android.
2. Exact gyro matrix (usually same rotation as accel, but not guaranteed).
3. Exact magnetometer matrix (qmc6308 mount can differ from lsm6dso; SCP string
   `qmc6308 can't find hw gyro switch to DOS` implies runtime gyro/mag coordination).
4. SCP `direction` enum numeric values for each sensor.

## 7. NEEDS LIVE — exact dump commands

Run against the stock XOS device **before** flashing anything. Nothing here modifies the phone.

```sh
# 1) Primary: HAL-reported per-sensor metadata (vendor, type, and orientation where exposed)
adb shell dumpsys sensorservice > sensorservice.txt

# 2) Filter for orientation / chip identity
adb shell dumpsys sensorservice | grep -iE 'lsm6|qmc|mn799|orient|accel|gyro|magnet|gyro|rotation'

# 3) Confirm live config files match the dump (cheap sanity check)
adb shell cat /odm/etc/vconfig/sensor/sensor_config.json
adb shell cat /vendor/etc/sensors/hals.conf
adb shell ls -la /vendor/etc/sensors/ /odm/etc/vconfig/sensor/

# 4) FDT sensor list (binary property) - confirms enabled types only, no matrix
adb shell cat /sys/firmware/devicetree/base/tran_sensor_cust/sensor_type | xxd

# 5) SCP-side orientation: firmware logs "direction: %u" when parsing DTS.
#    Capture kernel + SCP logs while sensors are enabled:
adb shell dmesg -w | grep -iE 'direction|sensor|lsm6|qmc|mn799|scp' &
adb shell dumpsys sensorservice --enable   # or open a compass/level app to force sensor enable
#    Also discover the SCP console/debug node on this build, then read it:
adb shell 'find /sys /dev -iname "*scp*" -o -iname "*sensorhub*"' 
adb shell cat /sys/class/misc/scp/log        # if present

# 6) Most decisive for the xnu port: empirical axis determination.
#    With a sensor-test app or `dumpsys sensorservice` live-event read:
#    - phone flat, screen UP  -> gravity axis/sign in accel output
#    - phone flat, screen DOWN-> opposite sign on same axis
#    - phone upright portrait -> gravity reads on the device -Y axis
#    - rotate 90 deg about screen normal -> confirm in-plane X/Y swap
#    Record raw vectors for accel, gyro (after a deliberate rotation about each device axis),
#    and magnetometer (align to geographic North / known field direction).
```

Minimal acceptable artifact to close this gap: **`adb shell dumpsys sensorservice` full output** plus
**one raw accel vector with the phone flat screen-up**. If SCP `direction: %u` lines appear in the
live log, those plus the enum mapping in the tinysys source would yield the exact compiled values.

## 8. Method / coverage note

Whole-dump `grep -r -a` for `lsm6dso|qmc6308|mn79911` was launched over `/home/berucrypt00/otadump`
and is still long-running due to dump size; its decisive subset is already complete: all text/config
files under `odm/etc` + `vendor/etc`, `strings -a` of all sensor-related `.ko` and HAL `.so`, and byte-level
`strings`/`xxd` of `images/scp.img`. Every chip-name hit found by that subset is listed in §2–§4.
`/home/berucrypt00/Documents/port-tools` was excluded per instruction.
