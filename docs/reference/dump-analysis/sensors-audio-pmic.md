# Infinix X6886 (MT6789) — Sensors / Audio / PMIC-Battery / Thermal / NFC Inventory

Analysis date: 2026-11-23
Mode: **read-only** on `/home/berucrypt00/otadump` (nothing modified there).

Sources used:

| Source | Path / value |
|---|---|
| Kernel version | `/home/berucrypt00/otadump/kernel_version.txt` |
| Kernel config | `/home/berucrypt00/otadump/kernel_config.txt` |
| File inventory | `/home/berucrypt00/otadump/all_files.txt` |
| AP modules (A slot) | `odm_dlkm/lib/modules/`, `vendor_dlkm/lib/modules/`, `system_dlkm/lib/modules/`, `vendor_boot/ramdisk/lib/modules/` |
| Live FDT | `/home/berucrypt00/hakios-x6886/devices/x6886/evidence/fdt.dts` (350,179 bytes) |
| Sensor config | `odm/etc/vconfig/sensor/sensor_config.json` |
| Audio config | `odm/etc/vconfig/audio/` |
| NFC config | `odm/etc/vconfig/NFC/` |

Kernel: `Linux version 6.12.38-android16-5-gb575a0b6e647-ab14355190-4k ... clang version 19.0.1 ... #1 SMP PREEMPT Wed Oct 29 06:56:12 UTC 2025`
FDT identity: `model = "MT6789"`, `compatible = "mediatek,MT6789"`, `model-part-name = "MT6789V/T"`.

The dump contains both slots. Every `X/` path listed below has an `X_b/` mirror (e.g. `odm_dlkm_b/lib/modules/...`), with identical file names. Analysis was done on slot A.

---

## 0. Executive summary

| Subsystem | Active hardware | AP-side driver module(s) | FDT binding | Status |
|---|---|---|---|---|
| Accelerometer / gyro / magnet / ALS / PS | lsm6dso_acc, lsm6dso_gyro, qmc6308, mn79911_als/ps (from spec PDF) | `sensorhub.ko`, `mtk-ssc.ko`, `mtk-ssc-dbg-v2.ko` | **No per-chip FDT nodes found**; only `mediatek,ssc` + `mediatek,tran_sensor` | Sensor chips live behind SCP sensorhub firmware, not AP `.ko` per chip |
| Audio codec | MT6366 (PMIC codec) | `snd-soc-mt6366.ko`, `snd-soc-mt6789-afe.ko`, `mt6789-mt6366.ko` | `mediatek,mt6366-sound`, `mediatek,mt6789-mt6366-sound` | Verified |
| Audio smart PA | AW87xxx x2 | `snd-soc-aw87xxx.ko` | `awinic,aw87xxx_pa` @ i2c6-0x58 / 0x59 | Verified |
| Audio speaker DAI | RT5512 | (built into MTK sound machine driver) | `richtek,rt5512` @ i2c6-0x5c, referenced by `sound` node | Verified |
| Primary charger | SC8548 (standalone) | `tc_sc8548_charger.ko` | `sc,sc8548-standalone` @ i2c7-0x6f | Verified |
| Secondary / USB charger | SC8950 (with SGM41516D fallback) | `tc_sc8950.ko`, `tc_sgm41516d.ko` | `southchip,sc8950` @ i2c5-0x6b | Verified |
| Charge pump | HL7139A | `tc_hl7139a.ko` | `richtek,hl7139a` @ i2c7-0x5f | Verified |
| Fuel gauge | MT6366/MT6358 gauge | `mt6358_battery.ko`, `tc_gauge.ko`, `mtk_battery_manager.ko` | `mediatek,mt6358-gauge` under `pwrap@10026000/mt6366` | Verified |
| PMIC | MT6366 | (DT-built PMIC driver in kernel + `mt6789-mt6366.ko`) | `mediatek,mt6789-pwrap` @ 0x10026000 → child `mediatek,mt6366` | Verified |
| Thermal | MT6789 LVTS + MT6366 PMIC temp + 2 NTC ADC + connectivity/VTShell | `soc_temp_lvts.ko`, `pmic_temp.ko`, `thermal-generic-adc.ko`, `board_temp.ko`, `thd.ko` | 26 thermal zones | Verified |
| NFC | TMS (Tsingteng) | `tms_device_modules.ko` + GKI `nfc.ko` | `tms,nfc` @ i2c0-0x28 | Verified |

---

## 1. Exact kernel module inventory (paths are under `/home/berucrypt00/otadump/`)

### 1.1 Sensors

| Module | Full path | Notes |
|---|---|---|
| `sensorhub.ko` | `vendor_dlkm/lib/modules/sensorhub.ko` | AP-side sensorhub driver; strings show SCP comm, `sensor_list`, `tran_sensor` DTS parsing, CWB control |
| `mtk-ssc.ko` | `vendor_dlkm/lib/modules/mtk-ssc.ko` | Sensor System Control; matches `mediatek,ssc` |
| `mtk-ssc-dbg-v2.ko` | `vendor_dlkm/lib/modules/mtk-ssc-dbg-v2.ko` | debug companion |

**No `.ko` named `lsm6dso`, `qmc6308`, `mn79911`, `st_hal`, or any vendor accelerometer/gyro/magnetometer/ALS driver exists anywhere in the dump.** Searches performed: `all_files.txt`, all `modules.alias` / `modules.dep` / `modules.load` files, FDT text, and `strings` of `sensorhub.ko`. The names `lsm6dso_acc`, `lsm6dso_gyro`, `qmc6308`, `mn79911_als`, `mn79911_ps` come only from the spec PDF; they are **not present verbatim in the dump**.

### 1.2 Audio

| Module | Full path | Binding / alias (verified from `modules.alias`) |
|---|---|---|
| `snd-soc-mt6366.ko` | `vendor_dlkm/lib/modules/snd-soc-mt6366.ko` | `of:N*T*Cmediatek,mt6366-sound`, `of:N*T*Cmediatek,mt6358-sound` |
| `snd-soc-mt6789-afe.ko` | `vendor_dlkm/lib/modules/snd-soc-mt6789-afe.ko` | MT6789 AFE platform |
| `mt6789-mt6366.ko` | `vendor_dlkm/lib/modules/mt6789-mt6366.ko` | `alias mt6789 mt6366 soc card mt6789_mt6366` |
| `snd-soc-aw87xxx.ko` | `vendor_dlkm/lib/modules/snd-soc-aw87xxx.ko` | Awinic smart-PA; strings contain `aw87xxx_acf.bin`, `awinic_monitor.bin`, AWINIC_BIN_ERR parser |
| `snd-soc-mtk-common.ko` | `vendor_dlkm/lib/modules/snd-soc-mtk-common.ko` | MediaTek common ALSA SoC helpers |
| `snd-soc-audiodsp-common.ko` | `vendor_dlkm/lib/modules/snd-soc-audiodsp-common.ko` | `of:N*T*Cmediatek,snd-audio-dsp` |
| `snd-soc-fs1599.ko`, `snd-soc-fs1894.ko`, `snd-soc-oca72xxx.ko`, `mtk-sp-spk-amp.ko` | `vendor_dlkm/lib/modules/` | alternative amp / speaker protection drivers present but not bound by FDT nodes found |
| `audio_ipi.ko`, `mtk-scp-audiocommon.ko`, `mtk-scp-ultra.ko`, `mtk-scp-vow.ko`, `scp.ko`, `mtk-vow.ko`, `mtk-btcvsd.ko` | `vendor_dlkm/lib/modules/` | SCP/VoW/BT voice path |
| `tc_audio.ko` | `odm_dlkm/lib/modules/tc_audio.ko` | `of:N*T*Ctc_audio` |
| `tran_audio_typec.ko`, `tran_audio_control.ko` | `odm_dlkm/lib/modules/`, `vendor_dlkm/lib/modules/` | Transsion audio/Type-C glue |

Firmware blobs (verified, exact sizes):

| File | Size |
|---|---|
| `odm/firmware/aw87xxx_acf.bin` | 5,988 bytes |
| `odm/firmware/awinic_params.bin` | 41,288 bytes |
| `odm/etc/vconfig/audio/pa_param/AW_DSP.bin` | 43,628 bytes |
| `odm/etc/vconfig/audio/pa_param/AW_SINWAVE_DSP.bin` | 14,596 bytes |

`snd-soc-aw87xxx.ko` contains the literal string `awinic_monitor.bin`; **that file was not found in the dump** (only `aw87xxx_acf.bin` and `awinic_params.bin` exist under `odm/firmware/`).

### 1.3 Charger / battery / gauge

All paths `odm_dlkm/lib/modules/` unless noted:

`tc_sc8548_charger.ko`, `tc_sc8950.ko`, `tc_hl7139a.ko`, `tc_sgm41516d.ko`, `tc_charger_framework.ko`, `tc_charger_class.ko`, `tc_gauge.ko`, `tc_pmic.ko`, `tc_adapter_class.ko`, `tc_ta_class.ko`, `tc_algorithm_class.ko`, `tc_pe20_algo.ko`, `tc_hvdcp20_algo.ko`, `tc_tc30_algo.ko`, `tc_pd_algo.ko`, `tc_pep50.ko`, `tc_pid_chg_algo.ko`, `tc_chg_type_det.ko`, `tc_ambient_temp_det.ko`, `tc_temp_forecast.ko`, `tc_otg_ctrl.ko`, `tc_phy_insert_det.ko`, `tc_water_detect.ko`, `tc_port_corrosion_prevention.ko`, `tc_trancare.ko`, `tcpc_class.ko`, `tcpc_upm7610.ko`, `tcpci_late_sync.ko`, `mt6358_battery.ko`, `mtk_battery_manager.ko`, `pca_tfcp_ta.ko`, `tc_tfcp_ta.ko`, `rt_pd_manager.ko`, `tran_extcon_usb.ko`, `tran_usb_ctrl.ko`.

Also mirrored in `vendor_boot/ramdisk/lib/modules/`: `tc_sc8548_charger.ko`, `tc_sc8950.ko`, `tc_charger_class.ko`, `tc_charger_framework.ko`, `tc_gauge.ko`, `mt6358_battery.ko`, `mtk_battery_manager.ko`, `tms_device_modules.ko`.

Thermal/PMIC throttle modules in `vendor_dlkm/lib/modules/`: `mtk_battery_oc_throttling.ko`, `mtk_low_battery_throttling.ko`, `mtk_apu_power_throttling.ko`, `mtk_cpu_power_throttling.ko`, `mtk_gpu_power_throttling.ko`, `mtk_md_power_throttling.ko`, `mtk_peak_power_budget.ko`, `mtk_dynamic_loading_throttling.ko`.

`odm_dlkm/lib/modules/modules.load` confirms all tc_* / gauge / battery modules above are explicitly loaded at boot.

### 1.4 NFC

| Module | Full path | Notes |
|---|---|---|
| `tms_device_modules.ko` | `odm_dlkm/lib/modules/tms_device_modules.ko` | TMS NFCC driver; strings: `alias=of:N*T*Csouthchip...`, `<TMS-%s>` logs, `device-name`, `irq-gpio`, `ven-gpio`, `download-gpio` parsing, char device creation |
| `nfc.ko` | `system_dlkm/lib/modules/nfc.ko` | GKI NFC core (net/nfc) — `alias=net-pf-16-proto-16-family-nfc`, `description=NFC Core ver 0.1` |
| `tms_device_modules.ko` | `vendor_boot/ramdisk/lib/modules/tms_device_modules.ko` | early-boot copy |

Userspace: `vendor/lib64/nfc_nci.thn31nfc.tms.so`, `vendor/lib64/vendor.tms.tmsnfc_aidl-V1-ndk.so`, `vendor/lib64/libtms_log_record.so`, `vendor/lib64/tms-cos-dl-common.so`, `vendor/lib64/tms-cos-dl-ree.so`, `vendor/lib64/tms-utils.so`.
Manifest `odm/etc/vintf/manifest/nfc-service-tms.xml` declares AIDL `android.hardware.nfc::INfc/default` and `vendor.tms.tmsnfc_aidl::ITmsNfc/default`.
`vendor/firmware/st21nfc_fw7.bin` and `vendor/lib64/nfc_nci.st21nfc.st.so` are STMicro leftover/secondary paths; the active controller in FDT and manifests is TMS.

---

## 2. FDT — I2C bus numbering, addresses, GPIO IRQ pins (all VERIFIED)

FDT `/aliases` at lines 43-52 and 9191-9215:

| Alias | Node | Base register |
|---|---|---|
| `i2c0` | `/soc/i2c@11e00000` | 0x11e00000 |
| `i2c1` | `/soc/i2c@11e01000` | 0x11e01000 |
| `i2c2` | `/soc/i2c@11eb0000` | 0x11eb0000 |
| `i2c3` | `/soc/i2c@11015000` | 0x11015000 |
| `i2c4` | `/soc/i2c@11eb1000` | 0x11eb1000 |
| `i2c5` | `/soc/i2c@11017000` | 0x11017000 |
| `i2c6` | `/soc/i2c@1101a000` | 0x1101a000 |
| `i2c7` | `/soc/i2c@11f00000` | 0x11f00000 |
| `i2c8` | `/soc/i2c@11eb2000` | 0x11eb2000 |
| `i2c9` | `/soc/i2c@11eb3000` | 0x11eb3000 |

GPIO phandle `0x49` = MediaTek PIO controller (`gpio-controller; #gpio-cells = <0x02>; gpio-ranges = <0x49 0x00 0x00 0xbc>`) at FDT line ~2393. All `<0x49 pin flags>` tuples below therefore mean main GPIO bank, pin number = second cell.

| Device (FDT node, line) | Compatible | Bus / address | GPIO / IRQ (raw FDT) | Key properties |
|---|---|---|---|---|
| `tms_nfc@28` (4644), under i2c@11e00000 | `tms,nfc` | **i2c0-0x28**; `reg = <0x28>` | `tms,irq-gpio = <0x49 0x01 0x00>` → **GPIO 1**; `tms,ven-gpio = <0x49 0x41 0x00>` → **GPIO 65**; `tms,download-gpio = <0x49 0x08 0x00>` → **GPIO 8** | `status = "okay"`, `tms,device-name = "tms_nfc"`, `tms,device-count = <0x01>`; bus `clock-frequency = <0x61a80>` (400 kHz) |
| `sc-swchg@6b` (5124), under i2c@11017000 | `southchip,sc8950`, fallback `mediatek,sgm41516d` | **i2c5-0x6b**; `reg = <0x6b>` | `sc8950,intr_gpio = <0x49 0x04 0x00>` → **GPIO 4** | `status = "okay"`, `chg_name = "primary_chg"`, `chg_alias_name = "sc8950_chg"`, `alias_name = "sgm41516d_chg"`, `phys = <0x61 0x03>`, `usb = <0x62>`; VBUS regulator `usb-otg-vbus` 4.9-5.3 V (`0x4a0150`-`0x50df20`) |
| `aw87xxx_pa@58` (5172), under i2c@1101a000 | `awinic,aw87xxx_pa` | **i2c6-0x58**; `reg = <0x58>` | none in node | `dev_index = <0x01>`, `status = "okay"` |
| `aw87xxx_pa@59` (5179), same bus | `awinic,aw87xxx_pa` | **i2c6-0x59**; `reg = <0x59>` | none in node | `dev_index = <0x00>`, `status = "okay"` |
| `speaker_amp@5c` (5185), same bus | `richtek,rt5512` | i2c6-0x5c | — | `#sound-dai-cells = <0x00>`, `sound-name-prefix = "Left"`, `phandle = <0x204>` |
| `gate_ic@11` (5157), same bus | `mediatek,gate-ic-i2c` | i2c6-0x11 | `gate-power-gpios = <0x49 0x0c 0x00>` → GPIO 12 | — |
| `sc8548_master@6F` (5245), under i2c@11f00000 | `sc,sc8548-standalone` | **i2c7-0x6f**; `reg = <0x6f>` | `sc8548,intr-gpios = <0x49 0x00 0x00>` → **GPIO 0** | `chg_name = "primary_dvchg"`, `set_vil`, `sc,sc8548,bat-ovp-threshold = <0x11ad>`, `bat-ocp-threshold = <0x32c8>`, `ac-ovp-threshold = <0x2ee0>`, `bus-ovp-threshold = <0x2ee0>`, `bus-ocp-threshold = <0x1388>`, `sense-resistor-mohm = <0x05>` |
| `hl7139a_master@5f` (5253), same bus | `richtek,hl7139a` | i2c7-0x5f | `hl7139a,intr-gpios = <0x49 0x00 0x00>` → GPIO 0 | `rm_name = "hl7139a"`, `rm_slave_addr = [5f]`, `chg_name = "primary_dvchg"`, `status = "okay"` |
| `tran_ldo@28` (4722), under i2c@11015000 | `tran,tran_ldo` | i2c3-0x28 (not NFC) | — | LDOs TRANLDO13/23/33/43 etc. |
| `fakesensor` (8214) | `transsion,fakesensor` | — | `fakesensor_output = <0x49 0x16 0x00>` → GPIO 22 | `sensor_matecodes = "alspdt17_17201873"`, `sensor_index = "2"` |

The spec-PDF I2C addresses (7-006f, 5-006b, 6-0058, 6-0059, 0-0028) are all **confirmed by the live FDT**.

### PMIC / pwrap (verified)

`pwrap@10026000` (line 3219) `compatible = "mediatek,mt6789-pwrap"`, register `0x10026000`; child node (line 3229) `compatible = "mediatek,mt6366"`. Relevant children:

- line 3277 `compatible = "mediatek,pmic-auxadc", "mediatek,mt6358-auxadc"`
- line 3357 `compatible = "mediatek,mt6366-keys"`
- line 3786 `compatible = "mediatek,mt6366-clkbuf"`
- line 3833 `mt6358codec { compatible = "mediatek,mt6366-sound"; ... }`
- `mtk-gauge` (line ~3868) `compatible = "mediatek,mt6358-gauge"`, `charger = <0x200>`, `io-channel-names = "pmic_battery_temp", "pmic_battery_voltage", "pmic_bif_voltage", "pmic_ptim_voltage", "batteryID-channel", "pmic_ptim_r"`
- line 808 `mt6366_temp { compatible = "mediatek,mt6366-pmic-temp"; ... #thermal-sensor-cells = <0x01> }`
- `pwraphal@10026000` (3977) `compatible = "mediatek,pwraph"`, `pwrap_mpu@10026000` (3983).

Audio machine node (line 7625): `sound { compatible = "mediatek,mt6789-mt6366-sound"; mediatek,platform = <0xbd>; mediatek,scp-audio = <0xbe>; mediatek,spk-i2s = <0x03 0x00>; mediatek,speaker-codec { sound-dai = <0x204>; } }`. `0x204` = the RT5512 `speaker_amp@5c` node. The `mediatek,snd_scp_audio` node (`0xbe`) has `status = "disabled"` and `scp-spk-process-enable = <0x00 0x04 0x0f 0x13>`.

---

## 3. Sensors — detail

### 3.1 What the dump actually contains

`odm/etc/vconfig/sensor/sensor_config.json` (402 bytes, complete content):

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

There is **no orientation / mount matrix / placement matrix** in this file.

`odm/etc/vconfig/tranfac/aidl/default/SensorNv.json` holds calibration NV paths only (no mount matrix): `ACCELEROMETER → HWMON_ACC` (intSize 3), `GYROSCOPE → HWMON_GYRO` (3), `PROXIMITY → HWMON_PS` (3) plus `GOODIX/HWMON_ALSNUMBER18BIT32` (4), `LIGHT → HWMON_ALS` (2), `HWMON_ALS_MAIN_KVALUE` (2), `HWMON_ALS_AVALUE` (9), `HWMON_ALS_BVALUE` (9), `SENSOR_TEK_CUTOUT_V100/HWMON_ALS_NUMBER27_BIT32` (2, totalIntSize 20), `EMINENT_CUTOUT_V100` (4), `GOODIX_CUTOUT_V100` (1), plus `SUBACCEL`, `SUBGYRO`, `LINEAR_HALL`, `OIS`, `SAR`, `SUBALS`.

FDT sensor-side nodes (only these were found):

- `mtk_ssc { compatible = "mediatek,ssc"; safe-vlogic-uv = <0x927c0>; ssc-enable = <0x00>; phandle = <0x1d8>; }` (line 8682)
- `tran_sensor_cust { compatible = "mediatek,tran_sensor"; sensor-get-screen-power-state = <0x00>; tran-charger-poll = <0x01>; tran-cloud-tracking-sensor-data = <0x00>; tran-sensor-debug = <0x00>; nv_restore_time = <0x03>; sensor_type = <0x01 0x02 0x04 0x05 0x00 0x00>; }` (line 338)
- `fakesensor` (described in §2) — camera/ALS-adjacent fake sensor, not the IMU.

HAL config: `vendor/etc/sensors/hals.conf` contains exactly `android.hardware.sensors@2.X-subhal-mediatek.so`; service binary `vendor/bin/hw/android.hardware.sensors-service.multihal`; AIDL manifest `vendor/etc/vintf/manifest/android.hardware.sensors-multihal.xml`; other subhals `vendor/lib64/hw/sensors.elliptic.so`, `sensors.dynamic_sensor_hal.so`.

### 3.2 Conclusion on sensor orientation

**No mount matrix exists in any examined dump file.** Verified by searching `odm/etc`, `vendor/etc`, `sensor_config.json`, `SensorNv.json`, FDT, and `all_files.txt` for `orientation`, `mount.matrix`, `mount_matrix`, `matrix` in sensor context. Any XNU port will have to derive orientation empirically (or from SCP firmware/calibration partition not present in this dump). This is a hard gap, not an inference.

---

## 4. Audio — detail

Config files (all under `odm/etc/vconfig/audio/`):

- `aurisys_config.xml` — scenario→library map. Relevant keys:
  - `AURISYS_SCENARIO_PLAYBACK_NORMAL` / `..._LOW_LATENCY` → library `mtk_bessound`
  - `AURISYS_SCENARIO_PLAYBACK_SMARTPA` → library `smartpa_aw88xxx` (name preserved verbatim; driver is aw87xxx)
  - record / VoIP scenarios → `mtk_speech_enh`
  - `AURISYS_SCENARIO_HDR_RECORD` → `aurisys_demo`
  - line 88-90: `lib_path="/odm/lib/libawinic_mtk_aurisys.so"`, `lib64_path="/odm/lib64/libawinic_mtk_aurisys.so"`, `adb_cmd_key="AWINIC"`, `param_path="/odm/etc/vconfig/audio/pa_param"`
- `audio_device.xml` — card name `<card name="mt6789mt6366" />`; mixer paths with kcontrols such as `DAC In Mux`, `HPL Mux`, `HPR Mux`, `RCV Mux`, `LOL Mux`, `Ext_Speaker_Amp Switch`, `ADDA_DL_CH1 DL1_CH1`
- `TranAudioConfiguration.conf` — `TRAN_AUDIO_AW87XXX_SUPPORT = yes`
- `audio_effects_config.xml`, `Denoise_Param_Call_VP.json`, `tranMagiqueSonMode.dat`, `license.lic`
- `audio_param/` — large set of `*_AudioParam.xml` / `*_ParamUnitDesc.xml` files (Speech, Record, VoIP, VoW, Playback, HpImpedance, SmartPa, etc.)

Note: `vendor/etc/aurisys_config.xml` line 90 points at `param_path="/odm/etc/smartpa_param"` and `vendor/etc/aurisys_config_adsp.xml` line 299 at `/vendor/etc/smartpa_param/`; **neither directory exists in the dump**. The ODM copy points at `/odm/etc/vconfig/audio/pa_param`, which does exist.

Awinic userspace library: `odm/lib64/libawinic_mtk_aurisys.so`. Amp dev_index mapping from FDT: i2c6-0x58 → `dev_index = 1`; i2c6-0x59 → `dev_index = 0`.

---

## 5. Battery / charger / gauge — detail

### 5.1 FDT battery and charging topology (verified values)

Fuel gauge node `mtk-gauge` (under `pwrap@10026000/mt6366`, ~line 3868):
- `compatible = "mediatek,mt6358-gauge"`, `phandle = <0x47>`
- `charger = <0x200>`
- `fg_swocv_i = <0xffffe604>`, `fg_swocv_v = <0xa41b>`, `shutdown_time = <0x02>`, `bootmode = <0x46>`
- `keep-100-percent = <0x01>`, `r-fg-value = <0x32>`, `fg-meter-resistance = <0x64>`, `car-tune-value = <0x64>`, `pmic-min-vol = <0x82dc>`
- temperature points `temperature-t0 = <0x32>`, `t1 = <0x19>`, `t2 = <0x0a>`, `t3 = <0x00>`, `t4 = <0xfffffff6>`
- battery profile tables `battery0-profile-t0` … `battery0-profile-t4` and `battery3-profile-t0` … `battery3-profile-t4` (lines 3915-3942), encoding voltage/current/temperature curves for two battery IDs.

Framework node (line 8525): `charger { compatible = "tc,charger"; gauge = <0x47>; charger = <0x51>; bc12-psy = <0x51>; bootmode = <0x46>; algorithm-name = "Basic"; charger-configuration = <0x00>; battery-cv = <0x426030>; max-charger-voltage = <0x632ea0>; min-charger-voltage = <0x4630c0>; ... jeita thresholds ... }`.
Phandle `0x51` resolves to node `chg { compatible = "mediatek,mt6375-chg"; chg_name = "primary_chg"; ... status = "disabled"; }` at line 4890 — i.e. the framework still references the disabled MT6375 internal charger path; the actually-enabled chargers are SC8548 and SC8950 (see §2). This is a verified FDT inconsistency/legacy reference.

Bootloader charger node (8257): `lk-charger { compatible = "mediatek,lk_charger"; enable-anime; enable-pd20-reset; power-path-support; max-charger-voltage = <0x632ea0>; fast-charge-voltage = <0x231860>; usb-charger-current = <0x7a120>; ac-charger-current = <0x1e8480>; ... }`.

Algorithm nodes: `transsion,pe20_charger` (8008), `transsion,tc30_charger` (8021), `transsion,hvdcp20_charger` (8035), `transsion,pd_charger` (8049). Top-level `tran-charger-poll = <0x01>` (line 342).

### 5.2 Which driver is active

- `tc_sc8548_charger.ko` — strings contain the exact DT compatibles `sc,sc8548-standalone` and `sc,sc8548-secondary`, `SC8548-STANDALONE` log prefix, `failed to register fc2_psy`, `devm_power_supply_register`. Binds `sc8548_master@6F` (i2c7-0x6f). Registers a `fc2_psy` power supply.
- `tc_sc8950.ko` — `modules.alias` contains `of:N*T*Csouthchip,sc8950` and `...C*`; embedded `description=SC8950 Charger Driver`, `name=tc_sc8950`. Binds `sc-swchg@6b` (i2c5-0x6b).
- `tc_hl7139a.ko` — binds `richtek,hl7139a` @ i2c7-0x5f, charge pump path.
- `mt6358_battery.ko` — binds `mediatek,mt6358-gauge` (from `modules.alias`); gauge HW driver, exposes battery properties via MTK battery manager.
- `tc_gauge.ko` — binds `tc_gauge`; strings: `register battery psy failed`, `gauge psy register fail`, `get chg_psy failed`, `Couldn't get capacity`, `TRAN_PROP_PSY_TEMP`.
- `mtk_battery_manager.ko` — binds `mediatek,battery_manager`; strings: `[BAT_probe] power_supply_register Battery Fail !!`, `bm_update_psy_property`, `get gauge1/gauge2 psy`, `get POWER_SUPPLY_PROP_CHARGE_NOW fail`.
- `tc_charger_framework.ko` — binds `tc,charger`; strings show it creates `/proc/tc_battery_cmd` and `/proc/tran_chg_limit_current`, and properties `adapter_capacity`, `battery_health_percent`, `battery_health_level`, `BATTERY_QMAX`, `BatteryNotify`.

### 5.3 Capacity path

Verified: multiple modules register/read Linux `power_supply` class devices (`devm_power_supply_register`, `power_supply_register`, `power_supply_get_by_name`, `POWER_SUPPLY_PROP_CHARGE_NOW`), and `mtk_battery_manager.ko` registers a psy named `Battery`.

**Inference (not a literal string found in the dump):** the standard Android capacity node is `/sys/class/power_supply/battery/capacity` (class name lowercased by the power_supply core); the battery health HAL in use is `android.hardware.health-service.example` — manifest `vendor/etc/vintf/manifest/android.hardware.health-service.example.xml` declares AIDL `android.hardware.health@4::IHealth/default`, started by `vendor/etc/init/android.hardware.health-service.example.rc` (`service vendor.health-default /vendor/bin/hw/android.hardware.health-service.example`). The AOSP example HAL reads standard sysfs power_supply attributes. Additional Transsion-only hooks: `/proc/tc_battery_cmd`, `/proc/tran_chg_limit_current`.

### 5.4 Build property

`odm/etc/build.prop:189`: `ro.vendor.tran.anti.crack.p7.support=1` — anti-crack prop noted per project rules; must be handled by an explicit allowlist when building.

---

## 6. Thermal — detail

Drivers:

| Module | Binds (verified alias) | Hardware |
|---|---|---|
| `soc_temp_lvts.ko` | `of:N*T*Cmediatek,mt6789-lvts` (plus other MT68xx LVTS compatibles) | `mediatek,mt6789-lvts` node @ `0x1100b000`, `reg <0 0x1100b000 0 0x1000>`, `interrupts <0 0xc9 4 0>` = **IRQ 201**, `clocks = lvts_clk`, `nvmem-cells = e_data1, e_data2`, `#thermal-sensor-cells = <0x01>`, phandle `0x1b` |
| `pmic_temp.ko` | `of:N*T*Cmediatek,mt6366-pmic-temp` | `mt6366_temp` @ line 808, 4 ADC channels `pmic6366_ts1..ts4` (io-channels `<0x19 5>`, `<0x19 6>`, `<0x19 7>`, `<0x19 8>`), `#thermal-sensor-cells = <0x01>`, phandle `0x20` |
| `thermal-generic-adc.ko` | GKI generic ADC thermal | `thermal-ntc1` (phandle `0x1e`, io-channel `<0x18 0>`) and `thermal-ntc2` (phandle `0x1f`, io-channel `<0x18 1>`), both `compatible = "generic-adc-thermal"` with large `temperature-lookup-table` |
| `board_temp.ko` | `of:N*T*Cmediatek,thermal-auxadc-sensor` | auxiliary board temperature |
| `thd.ko`, `thermal_interface.ko`, `thermal_trace.ko` | — | thermal daemon / interfaces |
| `backlight_cooling.ko` | `mediatek,backlight-cooler` | backlight cooling device |
| `mtk_battery_oc_throttling.ko`, `mtk_low_battery_throttling.ko`, `md_cooling_all.ko`, `mtk_apu_power_throttling.ko`, `mtk_cpu_power_throttling.ko`, `mtk_gpu_power_throttling.ko`, `mtk_md_power_throttling.ko`, `mtk_dynamic_loading_throttling.ko`, `mtk_peak_power_budget.ko` | various | cooling / throttling devices |

26 thermal zones under `/thermal-zones` (FDT lines 851-1262), in order:

`soc_max`, `cpu_little1`, `cpu_little2`, `cpu_little3`, `cpu_little4`, `cpu_big1`, `cpu_big2`, `cpu_big3`, `cpu_big4`, `gpu2`, `gpu1`, `soc1`, `soc2`, `md`, `ap_ntc`, `ltepa_ntc`, `pmic6366_pmu`, `pmic6366_bk1`, `pmic6366_bk2`, `pmic6366_bk3`, `consys`, `vtshell_max`, `vtshell_front`, `vtshell_back`, `camera_main`, `camera_sub`.

Sensor source per zone (from `thermal-sensors` phandle):
- `soc_max`, `cpu_little*`, `cpu_big*`, `gpu1/2`, `soc1/2`, `md` → `0x1b` (MT6789 LVTS) with cell indices 0x00-0x0d
- `ap_ntc` → `0x1e` (thermal-ntc1 / generic-adc-thermal)
- `ltepa_ntc` → `0x1f` (thermal-ntc2)
- `pmic6366_pmu`, `pmic6366_bk1/2/3` → `0x20` (MT6366 PMIC temp) cells 0x00-0x03
- `consys` → `0x21` (connectivity); `vtshell_*` → `0x22`; `camera_main` → `0x220`; `camera_sub` → `0x221`

Trip types: `soc_max` has a `critical` trip (`/thermal-zones/soc_max/trips/soc_max_crit@0` in `/aliases`); all other listed zones use `passive` trips. `gpu2` is the only zone with non-zero polling (`polling-delay = <0x1f4>` = 500 ms, `polling-delay-passive = <0x0c>` = 12 ms); `camera_main`/`camera_sub` poll at `0x3e8` (1000 ms).

Kernel config: `CONFIG_THERMAL=y`, `CONFIG_THERMAL_OF=y`, `CONFIG_THERMAL_NETLINK=y`, `CONFIG_THERMAL_DEFAULT_GOV_STEP_WISE=y`, `CONFIG_THERMAL_GOV_STEP_WISE=y`, `CONFIG_THERMAL_GOV_BANG_BANG=y`, `CONFIG_THERMAL_GOV_USER_SPACE=y`, `CONFIG_THERMAL_GOV_POWER_ALLOCATOR=y`, `CONFIG_CPU_THERMAL=y`, `CONFIG_CPU_FREQ_THERMAL=y`, `CONFIG_CPU_IDLE_THERMAL=y`, `CONFIG_DEVFREQ_THERMAL=y`, `CONFIG_THERMAL_EMERGENCY_POWEROFF_DELAY_MS=100`.

---

## 7. Kernel config findings (verified from `kernel_config.txt`)

Present:
- `CONFIG_NFC=m`
- `CONFIG_POWER_SUPPLY=y`
- `CONFIG_THERMAL=y` (+ thermal governors listed in §6)
- `CONFIG_IIO=y`, `CONFIG_IIO_BUFFER=y`, `CONFIG_IIO_TRIGGER=y`

Absent (no match at all, enabled or disabled):
- any `CONFIG_SENSORS_LSM6DSO*`, `CONFIG_LSM6DSO*`, `CONFIG_ST_*`
- any `CONFIG_*QMC6308*`, `CONFIG_*MN79911*`
- any `CONFIG_MT6366*`, `CONFIG_AWINIC*`, `CONFIG_AW87*`
- any `CONFIG_CHARGER_SC8548*`, `CONFIG_CHARGER_SC8950*`, `CONFIG_BATTERY_*` vendor-specific symbols

Interpretation (inference): these are GKI vendor modules built out-of-tree by Transsion/MediaTek, so they do not appear in the GKI `kernel_config.txt`. `CONFIG_HISI_THERMAL=y` is present but is a generic GKI option, not evidence of a Hisilicon thermal device on this board.

---

## 8. XNU/HakiOS early-boot priority

Legend: **NEEDED** = required for a stable first boot / to avoid hardware damage; **SOON** = required before daily use; **OPTIONAL** = can be disabled for first boot.

| Item | Priority | Why / what to port first |
|---|---|---|
| PMIC MT6366 + pwrap @ 0x10026000 | **NEEDED** | All core rails/clocks/regulators come from `mediatek,mt6366`. Without PMIC init nothing else runs. Bootloader may leave rails on, but XNU must not re-init blindly. |
| Fuel gauge + battery psy (`mt6358_battery.ko`, `tc_gauge.ko`, `mtk_battery_manager.ko`) | **NEEDED / SOON** | Need SoC and battery presence to avoid sudden shutdown. Gauge is in PMIC; minimal read path (voltage, SoC, temperature) is enough for first boot. |
| SC8548 / SC8950 / HL7139A charger drivers | **SOON** | Not needed for a single boot if bootloader left charging state sane, but needed to sustain device on USB/battery. SC8548 has explicit OVP/OCP limits in FDT — do not skip if enabling charging. |
| MT6789 LVTS (`soc_temp_lvts.ko`, IRQ 201 @ 0x1100b000) + MT6366 PMIC temp + NTC ADC | **NEEDED / SOON** | Thermal safety. With conservative clocks, LVTS can be deferred past first boot, but PMIC temp and NTC should be online before sustained load. |
| Audio codec MT6366 + AFE + AW87xxx amps | **OPTIONAL** | No audio needed for first boot. Later: `snd-soc-mt6366.ko`, `snd-soc-mt6789-afe.ko`, `mt6789-mt6366.ko`, `snd-soc-aw87xxx.ko` + `aw87xxx_acf.bin` / `AW_DSP.bin`, then `audio_device.xml` / `aurisys_config.xml`. |
| Sensors (IMU/mag/ALS/PS) | **OPTIONAL** | Entirely SCP-backed. Requires sensorhub firmware not present in this dump; no mount matrix available. Auto-rotation/gesture features will be unavailable — acceptable for first boot. |
| NFC TMS | **OPTIONAL** | Independent i2c0 device; no boot dependency. Port last. |
| Throttle/cooling framework (`thd.ko`, cooling devices) | **OPTIONAL initially** | Can be replaced by conservative XNU thermal policy until real drivers exist. |

Suggested sequence: PMIC/regulators → clock/reset (outside this report's scope) → gauge/battery → minimal thermal read → storage/console → then audio → charging → sensors → NFC.

---

## 9. Verified facts vs inference

### Verified (direct evidence from dump/FDT/config files)
1. Exact module file names and paths in §1.
2. I2C bus alias table and every device address in §2 (FDT `/aliases` lines 43-52, node `reg` values).
3. All GPIO tuples and their pin numbers in §2 (GPIO controller phandle `0x49`, `gpio-ranges <0x49 0 0 0xbc>`).
4. SC8548 property values, SC8950 aliases, AW87xxx `dev_index` values, TMS GPIOs.
5. PMIC/pwrap topology: `mediatek,mt6789-pwrap` @ 0x10026000 → child `mediatek,mt6366`; MT6366 sound/gauge/temp/keys/auxadc children.
6. `sensor_config.json` complete content — contains no mount matrix; `SensorNv.json` calibration path list.
7. Audio firmware names/sizes and config keys quoted in §4; `TRAN_AUDIO_AW87XXX_SUPPORT = yes`.
8. `tc,charger` FDT values including the stale `charger = <0x51>` → disabled `mediatek,mt6375-chg` node.
9. `/proc/tc_battery_cmd` and `/proc/tran_chg_limit_current` from `tc_charger_framework.ko` strings.
10. 26 thermal zone names, their sensor phandles, LVTS IRQ/register, PMIC temp ADC channels.
11. Kernel config symbol list in §7, including which symbols are absent.
12. `ro.vendor.tran.anti.crack.p7.support=1` at `odm/etc/build.prop:189`.
13. `awinic_monitor.bin` referenced in driver strings but absent from dump; `/odm/etc/smartpa_param` referenced in `vendor/etc/aurisys_config.xml` but absent.

### Inference (reasoning, not directly evidenced)
1. Sensor chips are handled by **SCP sensorhub firmware** rather than AP per-chip `.ko`, because no per-chip module/alias/FDT node exists while `sensorhub.ko` + `mtk-ssc.ko` + `android.hardware.sensors@2.X-subhal-mediatek.so` are present. (The physical presence of lsm6dso/qmc6308/mn79911 comes from the spec PDF, not the dump.)
2. `ssc-enable = <0x00>` means the SSC hardware path is disabled in this build; sensor data likely flows through the standard sensorhub/SCP IPI instead.
3. Capacity node is the standard `/sys/class/power_supply/battery/capacity` — derived from `power_supply` class usage and the AOSP example health HAL; the literal path string was not found in any module.
4. Both SC8548 and SC8950 can claim `primary_chg`/`primary_dvchg` roles simultaneously (dual-charger configuration); exact runtime arbitration lives in Transsion `tc_*` algorithm modules and was not reverse-engineered here.
5. `vendor/firmware/st21nfc_fw7.bin` / `nfc_nci.st21nfc.st.so` are secondary/leftover ST paths; TMS is the active NFCC (based on FDT compatible + AIDL manifest).
6. Missing mount matrix means orientation must be obtained empirically or from a source outside this dump.
7. `CONFIG_HISI_THERMAL=y` is a GKI generic option and does not indicate Hisilicon hardware here.

### Open gaps / recommended follow-up
- Obtain SCP sensorhub firmware binary to identify actual sensor IDs, I2C/SPI buses, and mount matrices for lsm6dso/qmc6308/mn79911.
- Dump live sysfs (`/sys/class/power_supply/`, `/sys/class/thermal/`, `/sys/kernel/debug/...`) from the stock device if a booted phone becomes available — currently out of scope (read-only dump only, no device access).
- Resolve whether `awinic_monitor.bin` is fetched from a different partition or is dead code in this build.
- Determine why `tc,charger` still points at disabled `mediatek,mt6375-chg` (`phandle 0x51`) and whether any tc_* algo rebinds to SC8950/SC8548 at runtime.
