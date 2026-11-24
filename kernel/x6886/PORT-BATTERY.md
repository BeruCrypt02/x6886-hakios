# X6886 MT6366 gauge monitor port manifest

Branch `feat/x6886-real-port`, 100% offline: no adb, no fastboot, no build, no flash.
Only `kernel/x6886/hakios.x6886_battery.c` and this manifest were written.

## Source
Real MTK logic: `refs/oosexgang-xnu-kernel-mtk-helio/kernel/overlay/bsd/dev/mtk_battery.c`
(WACS2 read + bounded wait `:42-46,82-109`; RTC_AL_MTH bits 14:8 hold Android's percentage
`:48,230-231`; BATADC scale and sanity `:59-62,255-259`).
**Not ported on purpose:** `pmic_write`, the FGADC latch writes (`:174-180`) and the AUXADC
request write (`:145`). This driver only reads; it never writes a charging or gauge-control register.

## Constants (file:line)
| Constant | Value | Proof |
|---|---|---|
| pwrap | `0x10026000`, size `0x1000` | `device-dump/fdt.dts:3219-3220` |
| gauge node | `mediatek,mt6358-gauge` under `mt6366` | `fdt.dts:3868-3870` |
| r-fg-value | `0x32` = **50 mOhm** | `fdt.dts:3883` |
| fg-meter-resistance | `0x64` | `fdt.dts:3887` |
| BATADC channel name | `pmic_battery_voltage` | `fdt.dts:3874-3875` |
| cell limit | `0x10fe` = **4350 mV / 4.35 V** | `fdt.dts:8378`, `:8488` |
| WACS2 cmd/rdata/vldclr, FSM | `0xc20/0xc24/0xc28`, `0`/`6` | `mtk_battery.c:43-46` |
| bounded wait | 200000 spins | `mtk_battery.c:85` |
| RTC_AL_MTH / bits 14:8 | `0x05aa` | `mtk_battery.c:48,230-231` |
| CHR_CON0 / CHRDET bit 5 | `0x0a88` | `mtk_battery.c:49-50` |
| BATADC ADC0 / ready bit 15 | `0x1088` | `mtk_battery.c:19-20,59` |
| count scale | 164795 nV/count | `mtk_battery.c:62` |

## Boot safety and gaps
- `x6886_batt=1` opt-in; map failure / unready pwrap is a clean `ENXIO`/`EBUSY`, no panic.
- Only PMIC **reads**. Current and coulomb-counter percentage are unavailable by design (the
  reference obtains them only by writing FGADC_CON1), so `current_ua` is 0 and the SOC comes from
  the already-latched RTC value; BATADC is read without requesting a conversion.
- Final audit fix: `fdt.dts:3868-3883` proves the gauge node, `r-fg-value` and the BATADC channel
  name, but carries **no PMIC register numbers**. `RTC_AL_MTH 0x5aa`, `CHR_CON0 0xa88` and
  `AUXADC_ADC0 0x1088` come only from `mtk_battery.c:48-60` and are not DTB-verified. All PMIC
  reads are now behind the separate, **default-OFF** boot-arg `x6886_batt_pmic=1`
  (`hakios.x6886_battery.c:19-24,229-235`); without it no register is read and no thread starts.
  Owner sign-off is required before enabling. A failed read publishes no fabricated value.
- Voltage is clamped at 4350 mV and sanity-checked below 2500 mV (`mtk_battery.c:256`).
- Bounded tick count (`x6886_batt_ticks`, max `0x00ffffff`); no unbounded loop.
- Every diagnostic goes to printf and pstore `0x480af000` via `X6886_LOG` (`[X6886-battery]`).
