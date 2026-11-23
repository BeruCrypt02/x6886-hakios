# X6886 dump analysis — verified findings and bring-up map

Seven subagents analyzed 27 GB of the read-only stock dump in parallel. Every
claim below was re-checked against the dump directly. Full reports are in
this directory; this file is the index and the decision input for the first
driver.

Source: `/home/berucrypt00/otadump`. Live device evidence is in
`../device-dump/HARDWARE-IDENTITY.md`. Both were read only. No phone writes.

## Verified hardware identity (three independent sources agree)

| Part | Identity | Source of truth |
|---|---|---|
| SoC | MT6789V/T, Helio G200, 2x A76 + 6x A55, 6 nm | spec PDF + live FDT |
| Panel | nt37706a, BOE, 1080x2400, DSI video + DSC, 144 Hz | live FDT `touch,lcm_name` |
| Touch | FocalTech FT3682g/3683g, SPI1, 8 MHz | live FDT + driver .ko |
| Fingerprint | Goodix GF3976 (candidate), optical FOD, SPI3, 27 MHz | live FDT + HAL strings |
| PMIC | MT6366 at pwrap `0x10026000` | live FDT |
| Chargers | SC8548 (i2c7-0x6f) + SC8950 (i2c5-0x6b) | live FDT + .ko |
| Audio | MT6366 codec + dual Awinic AW87xxx PA (i2c6-0x58/59) | live FDT + .ko |
| NFC | TMS @ i2c0-0x28 | live FDT |
| Sensors | lsm6dso / qmc6308 / mn79911 — behind SCP sensorhub, no AP .ko | spec PDF + dump gap |
| GPU | Mali-G57 MC2 (Valhall r54p1), no firmware blob in dump | .ko + firmware scan |

## The two supplier ambiguity, resolved

Live FDT has two panels and two touch chips:
- panel1 `nt37706a` + FocalTech, panel2 `vtdr6126a` + ChipSemi.
- `touch,lcm_name` selects panel1 (BOE/nt37706a). The FocalTech node is first
  in `touch@0`'s compatible list.

So this board uses **nt37706a + FocalTech**, not the TM/ChipSemi pair. The
vtdr6126a/chct5560 data stays as a documented fallback.

## Touch chip name discrepancy — open

The FDT supplier string says `ft3682g`; the actual driver is
`focaltech_ft3683g.ko` (`modules.alias focaltech,fts` → this module, version
`V4.1 20230424`). The firmware has no ASCII chip ID. The driver reads a 2-byte
chip ID from the IC at runtime. To settle 3682g vs 3683g we need one live
register read from the fitted controller. The driver works either way; the
protocol is the FT3xxx SPI family.

## The critical assets found in the dump

1. **Panel init blob** (the thing that was missing): in
   `nt37706a_..._boe_boe_144hz_x6886.ko` (359,416 B), symbol
   `lcm_initialization_setting`, raw bytes at file offset `0x420`,
   `0x7F00` bytes, 254 entries of `u32 cmd; u8 count; u8 para[123]` at `0x80`
   stride. Contains DCS `0x11` → 120 ms → `0x29`, `0x2A/0x2B` = 1080x2400,
   register-page writes `0xD2/0xD3`, `0xFFFC` MTK delay marker, 60/90/120/144 Hz
   mode sets, DSC parameter block, HBM/suspend payloads. Exact byte ranges are
   in `display.md` §9. This is enough to write the DSI init path.

2. **Touch protocol**: `focaltech_ft3683g.ko` (544,952 B), firmware
   `focaltech_ts_fw.bin` (237,568 B / `0x3A000`, header `00 00 20 60 00 03 8c
   7c`, `x6886` string at `0x140`). SPI opcodes `0x03` read, `0x09` upgrade,
   `0xAA`/`0x55` on register `0xFC`, registers `0x0A/0x06/0x91/0xBD/0xC0`.
   Details in `touch.md` §7.

3. **Fingerprint**: `tran_fp.ko` (194,952 B) matches the live FDT's
   `tran_fp_legacy` compatible; `tran_fp_v2.ko` has no matching compatible and
   likely does not probe. HAL strings say GF3976. Matching runs in MobiCore
   TAs (`05060000...tabin` 12.1 MB, plus GF3956/76/86 variants), kernel is
   SPI passthrough. Porting optical FOD to XNU needs a new matcher outside
   TrustZone. Details in `fingerprint.md`.

4. **Boot load order**: authoritative `modules.load` reproduced in
   `kernel-modules.md` §7. Foundations → pinctrl/clocks/PMIC → UFS →
   display → touch → fingerprint → GPU → Wi-Fi.

## Boot and partitions — safety facts

- Boot chain: preloader → **LK** (`lk_a/b`). No U-Boot exists anywhere in the
  dump. `fastboot boot` rejection seen on the device is conditional on
  PayJoy/unlock state (`check_payjoy_flag`, `tran_tkv_get_payjoy_flag`), not
  an unconditional LK limitation.
- `super` is exactly `13,083,561,984` B (`0x30bd75000`, 12.185 GiB), groups
  `main_a`/`main_b`. Slot A ≈10.54 GiB (6 GiB XOS apps in tr_product/tr_region),
  slot B ≈5.20 GiB.
- Anti-crack: `ro.vendor.tran.anti.crack.p7.support=1` at
  `odm/etc/build.prop:189`, consumed by `init.rc:1246` → `verify_para`.
- `tr_*` partitions mount `nofail`; core dynamic partitions do not.
- Forbidden to write: GPT, preloader, userdata, metadata, whole super, any
  slot A partition, secure/calibration/tkv. Slot B static images are the only
  conditionally writable area; logical B partitions need a DSU-style scoped
  mechanism, not a super rewrite.

## Gaps that need the live device or vendor source

1. FT3682g vs FT3683g: one live chip-ID register read.
2. GPU firmware: `mali_kbase` requests `sffw.bin`; no such file in the dump.
   Need the real name from a live device (`dmesg`/`strace` of the HAL) or from
   a sibling port.
3. Sensor orientation matrix: absent from `sensor_config.json`; derive
   empirically when sensors matter.
4. Goodix GF3976 register map: TA is encrypted; no usable init sequence.
5. DSI timing/lane count from the panel blob is decoded, but hardware
   bring-up still needs the clock/PHY register writes from
   `clk-mt6789-disp*.ko` and `mediatek_drm_v1`.

## Recommended first driver (decision, not yet executed)

Write the **nt37706a DSI panel init** first. It has the most complete
evidence (254 commands decoded, live FDT confirms the panel, the .ko exists
with a matching alias). Touch and fingerprint follow. This sequence matches
the dump's own `modules.load` order and the QEMU result, where the iOS
userland already boots to SpringBoard once a framebuffer exists.

No driver code is written yet. The board/soc definitions in
`device/x6886/` record verified wiring with `features = <>`.
