# X6886 dump analysis — verified findings and bring-up map

Seven subagents analyzed 27 GB of the read-only stock dump in parallel. Every
claim below was re-checked against the dump directly. Full reports are in
this directory; this file is the index and the decision input for the first
driver.

Source: `/home/berucrypt00/otadump`. Live device evidence is in
`../devices/x6886/evidence/HARDWARE-IDENTITY.md`. Both were read only. No phone writes.

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

## The four gaps — after a second dump-only investigation

Four subagents re-searched the dump (no port-tools, no refs). Two gaps closed
from the dump alone, two remain genuinely blocked. Reports: `gap-*.md`.

1. **FT3682g vs FT3683g — CLOSED LOCALLY.** `IC_Type=FT3683G` at
   `odm/firmware/Conf_MultipleTest.ini:23`, a file the driver itself reads
   (strings `Conf_MultipleTest.ini` at .ko offset `0x33027`, `IC_Type` at
   `0x35587`). Live `modules.txt` shows `focaltech_ft3683g` loaded. `FT3682G`
   has zero matches in odm/odm_dlkm/vendor/vendor_boot/vendor_dlkm; it is a
   board label only. Treat the touch as FT3683G, FTS SPI-v2 protocol.
2. **GPU firmware names — CLOSED LOCALLY, blobs still absent.** The driver
   requests `valhall-1691526.wa` (literal string at .ko offset `0x9245e`) and
   `mali_csffw.bin` (`sffw.bin` was only its tail). Confirmed by
   `vendor/etc/selinux/vendor_file_contexts:1208,1261`. The blobs themselves
   are not in the dump. Need the official firmware package, not a live read.
3. **Sensor orientation — NEEDS LIVE.** `sensor_config.json` has no mount
   matrix. `scp.img` (`tinysys-scp-RV55_A`) carries a compiled DTS parser with
   `direction`/`orientation` keys, so the values exist only as compiled
   numbers. Need `adb shell dumpsys sensorservice` plus a raw accel vector with
   the screen up. Do not reuse the orientation values in audio/camera XMLs.
4. **Goodix FOD — BLOCKED BY DESIGN.** No register map, opcodes or init
   sequence in `tran_fp.ko`; the kernel is SPI passthrough and matching lives
   in an encrypted MobiCore TA. Live `modules.txt`: `tran_fp` refcount 2
   (active), `tran_fp_v2` 0 (inactive, so the GF3976 TA is unused). No open
   matcher in the dump. Needs the TA key or a live SPI capture.

One further hardware item is decoded but not yet executed: DSI timing and
lane count come from the panel blob; the clock/PHY register writes still have
to be taken from `clk-mt6789-disp*.ko` and `mediatek_drm_v1` when the driver
is written.

## Recommended first driver (decision, not yet executed)

Write the **nt37706a DSI panel init** first. It has the most complete
evidence (254 commands decoded, live FDT confirms the panel, the .ko exists
with a matching alias). Touch and fingerprint follow. This sequence matches
the dump's own `modules.load` order and the QEMU result, where the iOS
userland already boots to SpringBoard once a framebuffer exists.

No driver code is written yet. The board/soc definitions in
`devices/x6886/legacy/` record verified wiring with `features = <>`.
