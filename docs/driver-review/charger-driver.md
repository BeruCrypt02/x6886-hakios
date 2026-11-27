# X6886 SC8950 safe-basic-charging port manifest

Branch `feat/x6886-real-port`, 100% offline: no adb, no fastboot, no kernel build, no flash.
Only `drivers/power/hakios.x6886_charger.c` and this manifest were written.
`drivers/power/hakios.x6886_battery.c` was **not** modified.

## Scope — read this first

- **SC8950 ONLY.** The only I2C address ever passed to a transport is `0x6b`. Both `chg_read()`
  and `chg_write()` refuse any other address (`EPERM`); there is no code path that can reach the
  HL7139A.
- **HL7139A fast-charge pump STRICTLY DISABLED.** The pump node exists in the dump
  (`fdt.dts:5259-5265`, `richtek,hl7139a` at I2C `0x5f`) but no source symbol, register offset,
  pin or enable write for it exists in this driver. A grep for a pump enable returns nothing.
- **Standard 5 V charging.** No HVDCP, PD, VOOC/SCP, PE or pump negotiation is implemented.
- **Hard caps:** cell voltage `4.35 V`, charge current `1500 mA`, input current `500 mA`.
  None of these can be raised by a boot argument.
- **Gauge stays read-only.** The MT6366 gauge monitor (`r-fg-value = 50 mOhm`,
  `fdt.dts:3883`) remains in the untouched battery driver. This file performs no gauge,
  coulomb-counter, NVRAM, modem, IMEI or protect-partition access of any kind.

## Genuine source and per-register citation

Register definitions were translated from
`refs/alps-device-modules-6.12/drivers/power/supply/bq2589x_reg.h` (the file that carries the
Southchip `SC89890H_*` scaling used by this part), with the setter logic from
`.../bq2589x_charger.c`. Charger-presence ordering follows the in-tree Transsion interface
`.../transsion/tc_charger_intf.c:94-119` (ONLINE is queried before any charger action).

| Register / field | Value used | alps source (`file:line`) |
|---|---|---|
| `REG_00` EN_HIZ / EN_ILIM / IINLIM | `0x00` / `0x80:7` / `0x40:6` / `0x3f:0`, base 100, LSB 50 | `bq2589x_reg.h:21,22-23,26-27,31-34` |
| `REG_03` WDT_RESET / OTG_CONFIG / CHG_CONFIG | `0x40:6` / `0x20:5` / `0x10:4` | `bq2589x_reg.h:117,123-125,127-129,132-134` |
| `REG_04` ICHG + SC89890H scale | mask `0x7f:0`, base 0, LSB 60 mA | `bq2589x_reg.h:146,151-152,155-156` |
| `REG_05` ITERM + SC89890H scale | mask `0x0f:0`, base 30, LSB 60 mA | `bq2589x_reg.h:159,162-163,170-171` |
| `REG_06` VREG | mask `0xfc:2`, base 3840 mV, LSB 16 mV | `bq2589x_reg.h:174-176,185-186` |
| `REG_07` EN_TERM, WDT mask | `0x80:7`, `0x30:4` | `bq2589x_reg.h:189-192,195` |
| `REG_0B` VBUS_STAT / CHRG_STAT / PG_STAT | `0xe0:5` / `0x18:3` / `0x04:2` | `bq2589x_reg.h:298-300,310,317` |
| `REG_0C` WDT / charge / battery / NTC faults | `0x80` / `0x30:4` / `0x08` / `0x07` | `bq2589x_reg.h:325,327,331-332,338,340` |
| `REG_0D` FORCE_VINDPM + VINDPM | `0x80:7`, `0x7f:0`, base 2600 mV, LSB 100 mV | `bq2589x_reg.h:347,353,357,360-361` |
| `REG_11` VBUS_GD | `0x80:7` | `bq2589x_reg.h:390-391` |
| setter behaviour (floor/clamp then update bits) | charge / term current | `bq2589x_charger.c:390-405,407-423,136-155` |
| safe-init ordering (limits before enable) | init sequence shape | `bq2589x_charger.c:1289-1300` |

### Why this is the SC8950 register map (device proof, not an assumption)

The device's own DT node declares `southchip,sc8950` with `mediatek,sgm41516d` as a second
compatible string (`fdt.dts:5125-5126`, `reg = <0x6b>`), and its kernel log names the part
`sc8950 5-006b`. The logged register values decode exactly with the Southchip scaling above:

| Observation | Decode | Proof |
|---|---|---|
| ICHG read 480000 uA from `reg04=0x08` | `8 * 60 + 0` mA | `dmesg.txt:141,161`; `bq2589x_reg.h:155-156` |
| ITERM 270000 uA from code 4 | `30 + 4 * 60` mA | `dmesg.txt:135-136,162`; `bq2589x_reg.h:170-171` |
| CV 4528000 uV from `reg06=0xae` | `3840 + 43 * 16` mV | `dmesg.txt:74-75,163`; `bq2589x_reg.h:185-186` |
| IINLIM 500000 uA from code 8 | `100 + 8 * 50` mA | `dmesg.txt:65-66,144-145`; `bq2589x_reg.h:33-34` |
| VINDPM 4600000 uV, `reg0d=0x94` | `2600 + 20 * 100` mV, force bit set | `dmesg.txt:58-60,146-149,170`; `bq2589x_reg.h:353,360-361` |
| charging enabled, HIZ off, WDT kick | `reg03=0x1a`, `sc8950_set_wdt_rst wdt_rst:0x1` | `dmesg.txt:48-53,89,160` |
| full dump reg00..reg14, reg83 | `dmesg.txt:157-180` | cross-check only |

## Caps and how they are enforced

| Cap | Value | Encoding written | Note |
|---|---|---|---|
| Cell voltage | 4350 mV max | VREG code 31 = 4336 mV | code 32 would be 4352 mV and is **not** written (`bq2589x_reg.h:185-186`); cell limit from `fdt.dts:8378,8488` (`0x10fe`) |
| Charge current | 1500 mA max | ICHG code 25 (`25 * 60` = 1500 mA) | stock log shows only 480 mA (`dmesg.txt:141`); the DT's `sc-charger-current = 0x2dc6c0` is deliberately not used |
| Input current | 500 mA | IINLIM code 8 (`100 + 8 * 50`) | standard 5 V adapter only (`dmesg.txt:65-66`) |
| Termination | 270 mA | ITERM code 4 | matches stock (`dmesg.txt:135-136`) |
| VINDPM | 4600 mV | code 0x14 + force bit | matches stock (`dmesg.txt:59-60`) |

The enable write (`REG_03` CHG_CONFIG set, OTG_CONFIG clear) is the only enable in the file and
happens **after** every limit is programmed. Watchdog is disabled (`REG_07` WDT mask cleared)
and only one kick is issued afterwards.

## Boot safety

- `x6886_chg=1` boot arg is required; default is a no-op return `0` with no register access.
- **Presence first:** `x6886_chg_wait_present()` reads `REG_0B`/`REG_11` (and snapshots
  `REG_0C`/`REG_0D`) up to `PRESENCE_TRIES = 8` times with 50 ms delays. Without VBUS/PG/VBUS_GD
  it returns `ENXIO` and never writes.
- Hard fault gate: charge/battery/NTC bits in `REG_0C` abort before any write with `EIO`.
- Bounded verification: 4 status reads at 100 ms apart after init; a new hard fault clears
  CHG_CONFIG and returns `EIO`.
- All errors are clean returns (`ENXIO`, `EIO`, `EPERM`, transport errno). **No panic, no assert,
  no unbounded loop.**
- Diagnostics go to printf and to the ramoops pstore console at `0x480af000`: signature
  `0x43474244` at +0, ring `start` at +4, `size` at +8, payload from +12, with a newline and
  wrapped start update. Every line is prefixed `[X6886-charger]`. Failure register snapshots
  (`before`, `after`, fault `reg0c`) are logged through the same path.
- Mapping the pstore does not touch any other memory; no pstore write happens if the signature
  is absent.

## Declared gap — I2C provider (owner decision required)

There is **no source** for the I2C5 serial engine in the local trees (only the prebuilt
`i2c-mt65xx.ko` in `Documents/x6886-tree`), and no SC8950 GPIO bit-bang pin numbers are declared
for X6886. Per the no-guessing rule, no controller offsets and no pin numbers were invented.
The driver therefore exports a weak hook:

    struct x6886_chg_i2c_ops *x6886_charger_i2c_ops __attribute__((weak));

with `read(addr, reg, *value)` / `write(addr, reg, value)` members. Until board glue publishes a
provider for I2C5 address `0x6b`, `x6886_charger_init()` logs and returns `ENXIO` without reading
or writing a register. This is the single deliberate gap; it is safe by construction and must
be closed by the owner with a real provider, not by guessed MMIO.

## Verification performed (offline)

- `grep -n -i 'hl7139\|hl7139a\|0x5f' hakios.x6886_charger.c` — only documentation and log
  strings; no enable write, no pump register, no second I2C address.
- `grep -n '0x6b\|SC8950_ADDR' hakios.x6886_charger.c` — every transport call is address-gated.
- `git diff --stat` limited to the two named files; battery driver unchanged.
- No build, no flash, no device connection was used.
