# X6886 hardware identity — verified from live device + spec PDF

Sources (all read-only, 2026-11-23):
- `/sys/firmware/fdt` on serial `153857057T001452` (KernelSU `su -c`)
- `INFINIX_Infinix_X6886.pdf` (Device Info HW+ 5.27.1)
- `/sys/bus/i2c/devices`, `/sys/class/input`, `/sys/class/drm`

## Panel

`touch,lcm_name = "nt37706a_fhdp_dsi_vdo_dsc_boe_boe_144hz_x6886"`

The DTB has two panel nodes because Transsion ships two display suppliers:

| node | panel | vendor | touch IC | status |
|---|---|---|---|---|
| panel1@0 | nt37706a | BOE | focaltech ft3682g | **the active one** |
| panel2@0 | vtdr6126a | TM | chipsemi chct5560 | alternate supplier |

`touch,lcm_name` picks panel1. `touch@0` compatible list is
`"focaltech,fts"`, `"chipsemi,chsc_cap_touch"`, with focaltech properties first.

- Panel: **nt37706a**, BOE, 1080x2400, DSI video + DSC, 144 Hz
- Touch: **focaltech ft3682g** (FTS), max 10 touches
- Touch coords: `0x437f` x `0x95ff` = 17279 x 38400 raw; display 1080x2400
- Touch firmware: `focaltech_ts_fw.bin`
- Interrupt: GPIO spec `<0x49 0x09 0x02>` (irq-gpio `<0x49 0x0c>`, reset `<0x49 0x74>`)
- DSI host: `14000000.dispsys_config`, output `card0-DSI-1`
- Supply: `vtouch-supply = <0x143>`

## SoC / board

- MT6789V/T (Helio G200), 2x A76 2.20 GHz + 6x A55 2.00 GHz, 6 nm, TSMC N6
- 8 GB LPDDR4X, UFS, aarch64, arm64-v8a
- Kernel: `6.12.38-android16-5-gb575a0b6e647-ab14355190-4k`
- Android 16, API 36
- `mt6789-mt6366` audio, `CONSYS_MT6789` Wi-Fi

## I2C map (from live device)

```
i2c-0  0x28  tms,nfc
i2c-2  0x0c  MAINAF
i2c-2  0x2a  unk2a
i2c-2  0x50  unk50
i2c-3  0x28  unk28
i2c-3  0x42  unk42
i2c-3  0x60  upm7610
i2c-4  0x1a  unk1a
i2c-4  0x51  unk51
i2c-5  0x6b  sc8950
i2c-6  0x11  unk11
i2c-6  0x58  aw87xxx_pa
i2c-6  0x59  aw87xxx_pa
i2c-6  0x63  unk63
i2c-7  0x1b  unk1b
i2c-7  0x6f  sc8548-charger
```

Touch IC is **not** on I2C. It sits behind `spi1.0` (`11010000.spi1`), which is why the
I2C scan shows no focaltech address. `spi-max-frequency = <0x7a1200>` = 8 MHz.

## Sensors (spec PDF)

```
accelerometer  lsm6dso_acc
als/ps         mn79911_als / mn79911_ps
magnetometer   qmc6308
gyroscope      lsm6dso_gyro
fingerprint    tran_keys (goodix_fp in DTB, 10006000 region)
charger        sc8548-charger (i2c 7-006f), sc8950 (i2c 5-006b)
audio amp      aw87xxx_pa (i2c 6-0058, 6-0059)
NFC            tms,nfc (i2c 0-0028)
```

## Not yet resolved

- Which GPIO numbers `0x49` phandle maps to (need gpio controller node from DTB)
- Backlight regulator node behind `<0x1e5>`
- Exact DSI timing from the panel driver's init sequence (need the panel DT node's
  `dsi-on` / `lcm` driver, not present in the stock DTB; vendor module holds it)
- The second supplier's data is kept for a future fallback, not for this board
