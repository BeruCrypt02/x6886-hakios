# X6886 battery, charging, PMIC and thermal reference

Offline research only. No source, build, boot image or device change is part of this
document. Source tags match `docs/X6886-GPU-PLAN.md`:

| Tag | Source |
| :-- | :-- |
| FDT | `device-dump/fdt.dts` (live device tree) |
| PROC | `device-dump/interrupts.txt`, `iomem.txt`, `bus.txt`, `modules.txt`, `dmesg.txt`, `getprop.txt`, `X6886-specs.txt` |
| DUMP | `/home/berucrypt00/otadump` |
| REF | `refs/` repositories |

Values from another phone are marked REF. They need a readback on X6886 before use.

## ICs and addresses

| Block | Identity / binding | Bus address | Source |
| :-- | :-- | :-- | :-- |
| PMIC wrapper | `mediatek,mt6789-pwrap` | MMIO `0x10026000`, 4 KiB, IRQ SPI 252, hwirq 284 | FDT 3219-3226, PROC |
| PMIC | `mediatek,mt6366` under pwrap, interrupt controller with two cells | parent IRQ `0x89` level plus `0x89` zero | FDT 3228-3234 |
| PMIC sub-blocks | `mt6358-efuse`, `mt6358-accdet`, `pmic-auxadc`/`mt6358-auxadc`, `mt6358-regulator`, `mt6358-rtc`, `mt6358-gauge`, `mt6358-lbat_service`, `mt6366-keys`, `mt6366-clkbuf`, `mt6366-sound` | all under `pwrap@10026000/mt6366` | FDT 3244-3860 |
| Fuel gauge | `mediatek,mt6358-gauge`, phandle `0x47` | PMIC register space through pwrap | FDT 3868-3879 |
| Battery manager | `mediatek,battery manager`, `gauge1 = <0x47>`, `charger = <0x200>` | software binding | FDT 3968-3975 |
| Type-C attach | `tc_chgtype`, phandle `0x200`, USB2 PHY index 3 | software binding | FDT 7973-7980 |
| Primary switching charger | `southchip,sc8950` / `mediatek,sgm41516d` | I2C bus 5 (`0x11017000`), address `0x6b`, EINT 4 | FDT 5124-5135, PROC `bus.txt`, PROC `interrupts.txt` 21 |
| Primary charge pump | `sc,sc8548-standalone` | I2C bus 7 (`0x11f00000`), address `0x6f`, EINT 0 | FDT 5245-5257, PROC `bus.txt`, PROC `interrupts.txt` 17 |
| Boost / charge pump | `richtek,hl7139a` | I2C bus 7, address `0x5f` | FDT 5259-5265, PROC `bus.txt` |
| Charger cooler | `mediatek,mt6375-charger-cooler`, two cooling cells | software binding, no I2C address in FDT | FDT 831-835 |
| PMIC keys | `mediatek,mt6366-keys`, power keycode 116, home keycode 605, both wake sources | through pwrap | FDT 3356-3371 |
| LVTS thermal controller | `mediatek,mt6789-lvts` | MMIO `0x1100b000`, 4 KiB, IRQ SPI 201, hwirq 233 | FDT 5647-5657, PROC |
| PMIC thermal sensor | `mediatek,mt6366-pmic-temp`, four channels | AUXADC channels 5, 6, 7, 8 | FDT 807-815 |
| NTC sensors | `generic-adc-thermal` twice, phandles `0x1e`, `0x1f` | AUXADC-based lookup tables | FDT 789-805 |
| Thermal interface | `mediatek,therm_intf` | SRAM `0x114000`, 1 KiB | FDT 844-849 |

Loaded modules confirm the live stack: `mt6789_mt6366`, `mt6358_battery`, `tc_pmic`,
`tc_gauge`, `tc_sc8548_charger`, `tc_sgm41516d`, `mt6358_regulator`, `mtk_pmic_wrap`,
`mtk_pmic_keys`, `pmic_temp`, `soc_temp_lvts`, `pmic_lbat_service` and the `tc_*` charger
algorithm modules (PROC `modules.txt`).

### PMIC bus interface

MT6789 has no Qualcomm SPMI bus for this PMIC. Access goes through the MediaTek PMIC
wrapper. Two software interfaces exist in the references and they are not compatible.

| Interface | Offsets | Reference | Use |
| :-- | :-- | :-- | :-- |
| Split SWINF | CMD `+0x880`, WDATA `+0x884`, RDATA `+0x894`, VLDCLR `+0x8a4`, STATUS `+0x8a8` | REF `refs/heliodarwin/wiki/MT6789-GPU-Power-Sequence.md`, MT6789 X676C | Selected by PWRAP capability bit 5. Write address OR `BIT(29)` for writes, raw address for reads. FSM `(STATUS >> 1) & 7`, idle 0, read valid 6. |
| WACS2 | CMD `+0xc20`, RDATA `+0xc24`, VLDCLR `+0xc28` | REF `refs/oosexgang-xnu27/kernel/overlay/bsd/dev/mtk_battery.c`, MT6768 X6826B | Older transaction style with halfword-shifted PMIC addresses. Do not mix with SWINF. |

The X676C reference states the shipped MT6789 PWRAP descriptor selects split SWINF.
Confirm capability bit 5 on X6886 before picking a helper. Serialize every PMIC
transaction across GPU power, gauge, keys and charger code.

HelioDarwin's `sweet_spmi.c` and `sweet_rpmh.c` patch series target Qualcomm firmware.
They do not apply here.

## Fuel gauge registers

All offsets below come from REF `refs/oosexgang-xnu27/kernel/overlay/bsd/dev/mtk_battery.c`.
That driver targets the MT6358 on the MT6768 X6826B. X6886 declares the same
`mediatek,mt6358-gauge` compatible string, so the register layout is a strong starting
point, but the scaling constants differ between boards.

| Register | Offset | Function in the reference driver |
| :-- | :-- | :-- |
| `RTC_AL_MTH` | `0x05aa` | bits 14:8 hold the Android SOC seed |
| `CHR_CON0` | `0x0a88` | bit 5 `RGS_CHRDET`, charger presence; reference value `0x62` with cable in |
| `FGADC_CON0` | `0x0d08` | gauge control |
| `FGADC_CON1` | `0x0d0a` | bit 0 request latch, bit 3 release, bit 15 latched |
| `FGADC_CAR_CON0/1` | `0x0d14`, `0x0d16` | coulomb counter |
| `FGADC_CUR_CON0` | `0x0d8a` | signed 16-bit current |
| `AUXADC_ADC0` | `0x1088` | BATADC result, 15 bits, bit 15 ready |
| `AUXADC_RQST0` | `0x1108` | bit 0 starts a BATADC conversion |

Reference scaling from that driver: BATADC count is 1.8 V x 3 / 32768, about 164.79 uV;
current count is 250 mV / 65536 over a 5 mOhm sense resistor, about 0.763 mA. X6886
declares `r-fg-value = <0x32>` (50 mOhm) and `fg-meter-resistance = <0x64>` (100). The
5 mOhm constant must not be copied into X6886 code.

### X6886 gauge settings from FDT

| Property | Raw | Decimal / meaning |
| :-- | :-- | :-- |
| `r-fg-value` | `0x32` | 50 mOhm sense resistor |
| `fg-meter-resistance` | `0x64` | 100 |
| `car-tune-value` | `0x64` | 100 |
| `pmic-shutdown-current` | `0x14` | 20 |
| `pmic-min-vol` | `0x82dc` | 33500 raw |
| `shutdown-gauge0-voltage` | `0x84d0` | 34000 raw |
| `temperature-t0` to `t4` | `0x32`, `0x19`, `0x0a`, `0x00`, `0xfffffff6` | 50, 25, 10, 0, -10 C |
| `difference-fullocv-ith` | `0x12c` | 300 |
| `shutdown_time` / `shutdown-1-time` | `0x02` / `0x96` | 2 / 150 |
| `keep-100-percent` | `0x01` | enabled |
| `embedded-sel` | `0x01` | enabled |
| `active-table` | `0x05` | profile table 5 |
| `multi-temp-gauge0` | `0x01` | enabled |
| `tran_Q_diff` | four times `0x1428` | four cells at 5160 each |
| `fg_swocv_i` / `fg_swocv_v` | `0xffffe604` / `0xa41b` | signed offset and voltage compensation |
| `battery0-profile-t0..t4`, `battery3-profile-t0..t4` | 100 rows x 4 columns each | OCV, resistance and temperature curves; keep the tables, do not rewrite them |
| `tran_dev_name` | `mtk_gauge` | device name |

AUXADC channels in X6886 FDT: BATADC 0, vcdt 2, battery temperature 3, chip temperature 5,
vcore temperature 6, vproc temperature 7, vgpu temperature 8, accdet 9, dcxo voltage 10,
tsx temperature 11, hpofs calibration 12, dcxo temperature 13, vbif 14, imp 15, imix_r 16.
The gauge consumes channels 3, 0, 14, 15, an external battery ID channel and 16 (FDT 3874).

## Charger registers and live values

`sc8950 5-006b` dumped its registers during the Android capture. These are observed
values, not a datasheet. Register meanings below come only from the matching driver log.

| Register | Observed | Meaning shown in `dmesg.txt` |
| :-- | :-- | :-- |
| `0x00` | `0x48` | input current limit, 500000 uA |
| `0x01` | `0x01` | no log label |
| `0x02` | `0x00` | no log label |
| `0x03` | `0x1a` | charge enable readback, enabled |
| `0x04` | `0x08` | charge current, 480000 uA |
| `0x05` | `0x74` | no log label |
| `0x06` | `0xae` | charge voltage, 4528000 uV |
| `0x07` | `0x8f` | no log label |
| `0x08` | `0x23` | no log label |
| `0x09` | `0x40` | no log label |
| `0x0a` | `0xe0` | no log label |
| `0x0b` | `0x34` | charge done status, not done |
| `0x0c` | `0x00` | no log label |
| `0x0d` | `0x94` | MIVR, forced VINDPM, 4600000 uV |
| `0x0e` | `0x00` | no log label |
| `0x0f` | `0x00` | no log label |
| `0x10` | `0x00` | no log label |
| `0x11` | `0x99` | no log label |
| `0x12` | `0x00` | no log label |
| `0x13` | `0x48` | MIVR state readback, 0 |
| `0x14` | `0x54` | no log label |
| `0x83` | `0x07` | no log label |

Other writes from the same capture: `set_mivr 4600000 uV`, `set_aicr 500000 uA`,
`set_ichg 500000 uA`, `set_cv 4530000 uV`, `set_ieoc 270000 uA`, `set_ircmp 20000`,
`set_ivcmp 15000`, watchdog reset `0x1`. The charger type was `none` with VBUS about
4986 mV while these registers were enabled. The capture also logged `get pmic_node fail`
and `power_supply charger: driver reporting unavailable enum value 1`. Treat adapter
detection as unfinished in the reference capture.

## Safety limits from X6886 FDT

| Limit | Value | Source |
| :-- | :-- | :-- |
| Battery CV | 4350000 uV (4.35 V) | FDT 8533 |
| Maximum charger voltage | 6500000 uV | FDT 8534 |
| Minimum charger voltage | 4600000 uV, dynamic 4400000 / 4200000 | FDT 8535, 8563-8564 |
| Maximum input current | 3000000 uA | FDT 8642 |
| Maximum charging current | 3600000 uA | FDT 8643 |
| SDP / CDP / DCP / non-standard current | 0.5 / 1.0 / 2.0 / 1.2 A | FDT 8634-8641 |
| USB / AC / host charging current | 0.5 / 2.05 / 1.5 A | FDT 8558-8561 |
| AC input current | 3.2 A | FDT 8560 |
| Maximum dynamic MIVR current | 1800000 uA | FDT 8565 |
| Maximum charging time | 72000 s (20 h) | FDT 8633 |
| JEITA T0 / T1 / T2 / T3 / T4 | 0 / 0 / 10 / 45 / 50 C | FDT 8542-8552 |
| Charge temperature window | 0 to 50 C | FDT 8554-8557 |
| Battery overheat threshold | 60 C | FDT 8586 |
| Battery overcold threshold | -20 C | FDT 8587 |
| Disable charge at hot / cold | 58 C / -19 C | FDT 8627-8630 |
| High battery current throttling levels | 6000 / 7000 / 8000 raw | FDT 3961-3964 |
| SC8548 battery OVP / OCP | 4525 mV / 13000 mA | FDT 5250-5251 |
| SC8548 AC OVP, bus OVP, bus OCP | 12000 mV / 12000 mV / 5000 mA | FDT 5252-5254 |
| SC8548 sense resistor | 5 mOhm | FDT 5255 |
| PD VBUS window | 5 V to 10 V | FDT 8051-8054 |
| PD charge current limit | 3600000 uA | FDT 8052 |

Conflict to resolve: the gauge charger node sets battery CV at 4.35 V, while the live
sc8950 log reports a charge voltage target of 4.53 V and a readback of 4.528 V. XNU must
not inherit both. Pick a target from the cell profile and verify it before enabling the
charger.

## Thermal zones and trips

All thresholds are millidegrees in FDT. GPU and most SoC zones use the LVTS controller.
The PMIC zones use `mt6366_temp` channels. NTC zones use the generic ADC sensors.

| Zone | Sensor | Trip | Source |
| :-- | :-- | :-- | :-- |
| `gpu2` | LVTS channel 9 | 85 C passive, 2.5 C hysteresis; cooling device is the GPU with contribution 1024 | FDT 982-1018 |
| `gpu1` | LVTS channel 10 | 85 C passive, 2.5 C hysteresis | FDT 1021-1045 |
| `soc_max` | LVTS channel 0 | 113.5 C critical | FDT 853-870 |
| `cpu_little1..4`, `cpu_big1..4` | LVTS channels 1 to 8 | 113.5 C passive | FDT 871-980 |
| `soc1`, `soc2`, `md` | LVTS channels 11 to 13 | 113.5 C passive | FDT 1046-1084 |
| `ap_ntc`, `ltepa_ntc` | NTC phandles `0x1e`, `0x1f` | 113.5 C passive | FDT 1086-1119 |
| `pmic6366_pmu`, `pmic6366_bk1..bk3` | PMIC temperature channels 0 to 3 | 113.5 C passive | FDT 1121-1183 |
| `consys` | CONNSYS sensor | 113.5 C passive | FDT 1185-1201 |
| `vtshell_max`, `vtshell_front`, `vtshell_back` | virtual shell channels 0 to 2 | 119 C passive | FDT 1203-1250 |
| `camera_main`, `camera_sub` | camera sensors | polling only, no trip in FDT | FDT 1251-1265 |

Observed thermal snapshot from `dmesg.txt`: `soc 49.185 C`, `lte 43.058 C`,
`tpcb 52 C`, `conn 47 C`, `gpu TTJ 85 C`, `apu TTJ 85 C`. A value of -274 C is a
missing-sensor sentinel in that log. Android properties in `getprop.txt` set HBM thermal
trigger at 44 C with clear at 42 C, and HBM battery-temperature trigger at 43 C with
clear at 40 C.

## Observed battery state

| Snapshot | SOC | Voltage | Temperature | Current | Capacity |
| :-- | :-- | :-- | :-- | :-- | :-- |
| `X6886-specs.txt` | 74 percent | 4084 mV | 36.8 C | discharging 543.2 mA | 5160 mAh profile |
| `dmesg.txt` FGADC | `ui_soc 10000` (100.00 percent), `soc 9723` (97.23 percent), `fg_c_soc 9137` (91.37 percent) | `VBAT 44170` raw | 39 C average 40 C | -740 mA | cycle 333, aging 10000 |

The two captures are at different times and charge states. SOC fields in the FGADC log
use hundredths of a percent. `VBAT` uses a 0.1 mV scale in that driver, so 44170 reads as
4417 mV. Confirm the scale before using it in an XNU power source.

## Cross-check with display, storage and GPU

| Check | Result |
| :-- | :-- |
| Duplicate GICv3 hwirqs | none across the 109 active GICv3 rows in `/proc/interrupts` |
| GPU | hwirq 428, 429, 430; DT SPI 396-400 |
| PMIC wrapper | hwirq 284; DT SPI 252 |
| LVTS | hwirq 233; DT SPI 201 |
| UFS | hwirq 169; DT SPI 137 |
| Display | OVL0 hwirq 319, OVL0_2L 320, RDMA0 321, DSI 333 |
| I2C controllers | hwirq 176 to 185, ten lines |
| External charger IRQs | EINT 4 for sc8950, EINT 0 for sc8548, both edge triggered |
| Clock and regulator mismatch | GPU node has no clocks or power-domains; gpufreq owns the register blocks and the `vgpu` / `vsram_gpu` supplies |
| Regulator range mismatch | `vgpu` and `vsram_gpu` both allow 0.5 to 1.293 V; OPP top is 0.9 V. Keep firmware selectors until DVFS is proven. |
| Firmware blobs | no GPU CSF firmware needed or present. No PMIC firmware blob declared in FDT. |
| Busy PMIC path | GPU power, keys, gauge and charger all share pwrap; one mutex is required |

## Implementation order

Each step needs explicit approval before any source file changes. No step here has been
executed by this research track.

1. **Identify the PWRAP interface.** Read the capability bit that selects split SWINF. Keep a single serialized transaction helper. Gate: a known PMIC register reads back with the expected FSM state.
2. **Publish battery readings first, read-only.** Implement gauge SOC, BATADC voltage, battery temperature and charger presence. Leave the charger off. Gate: XNU battery percentage tracks the FDT profile and the AUXADC reading within the reference tolerance.
3. **Seed SOC from the PMIC counter.** Use the coulomb counter and the 50 mOhm X6886 sense resistor for the running estimate. Do not import the X6826B 5 mOhm constant. Gate: SOC change matches integrated current over a fixed window.
4. **Wire PMIC keys through the same helper.** Power keycode 116 and home keycode 605 are wake sources. Gate: press and release produce one event each with no long-press reset programmed.
5. **Register LVTS and PMIC temperature sensors.** Expose GPU zones at 85 C and SoC trips at 113.5 C. Gate: a thermal event can be observed from the XNU side before GPU load exists.
6. **Add a charge policy gate.** Keep Hi-Z until adapter type, VBUS and cell temperature are known. Enforce 0 to 50 C charge window, 58 C hot cutoff, -19 C cold cutoff, 20 h safety timer and the current limits above. Gate: no charger enable write happens before all three inputs are valid.
7. **Resolve the CV conflict.** Choose between 4.35 V from FDT and 4.53 V from the Android capture by checking the cell profile and the gauge OCV curve. Gate: one written target, verified by readback.
8. **Add overcurrent and OVP handling.** Use SC8548 thresholds as hard limits. Treat the -20 C and 60 C cell limits as shutdown boundaries for charging. Gate: a simulated out-of-range value blocks charging and reports a fault.
9. **Couple GPU load to thermal state.** Cap GPU frequency at 485 MHz when the GPU passive trip fires or the battery current throttling level is reached. Gate: GPU load falls before the 113.5 C critical trip.
10. **Qualify with a bounded charge session.** Log SOC, voltage, current, cell temperature, VBUS, adapter type and charger register dump at a fixed interval. Gate: all values agree with the gauge and stay inside the FDT limits for the whole session.

## Blockers to fix first

| Priority | Blocker | Why it stops safe power management |
| :-- | :-- | :-- |
| 1 | SWINF versus WACS2 not verified on X6886 | Wrong PWRAP transactions can read stale data or write the wrong PMIC register |
| 2 | 4.35 V versus 4.53 V charge voltage conflict | Two different cell targets in local evidence |
| 3 | 50 mOhm X6886 sense resistor versus 5 mOhm reference driver | Current and SOC math would be off by a factor of ten |
| 4 | Adapter detection logged as `none` while registers enabled | Charge policy cannot trust the live reference behaviour |
| 5 | No XNU charger driver | Charging stays under firmware control or stays off |
| 6 | No XNU thermal driver | GPU throttling and critical shutdown have no consumer |
| 7 | PMIC bus shared with GPU power | Missing serialization breaks both rail control and gauge reads |
