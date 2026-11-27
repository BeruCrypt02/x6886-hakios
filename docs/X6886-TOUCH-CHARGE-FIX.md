# X6886 touch + charger evidence-backed fix manifest

Branch `feat/x6886-real-port`. Read-only audit of `/home/berucrypt00/otadump` (READ ONLY)
and of `refs/alps-device-modules-6.12`; no flashing, no whole-kernel build.
Exactly three files are involved: the two drivers below and this manifest.
The working tree contains other unrelated modifications from parallel work;
none of them were touched by this task.

Source labels used below:

| Label | Path |
|---|---|
| `fdt.dts` | `/home/berucrypt00/hakios-x6886/device-dump/fdt.dts` (live FDT) |
| `iomem.txt` | `/home/berucrypt00/hakios-x6886/device-dump/iomem.txt` (live `/proc/iomem`) |
| `getprop.txt` | `/home/berucrypt00/hakios-x6886/device-dump/getprop.txt` |
| `dmesg.txt` | `/home/berucrypt00/hakios-x6886/device-dump/dmesg.txt` |
| `touch.md` | `/home/berucrypt00/hakios-x6886/dump-analysis/touch.md` |
| `kernel-modules.md` | `/home/berucrypt00/hakios-x6886/dump-analysis/kernel-modules.md` |
| `ini` | `/home/berucrypt00/otadump/odm/firmware/Conf_MultipleTest.ini` |
| `bq2589x_reg.h` | `refs/alps-device-modules-6.12/drivers/power/supply/bq2589x_reg.h` |
| `mtk_tcm.c` | `refs/oosexgang-xnu-kernel-mtk-helio/kernel/overlay/bsd/dev/mtk_tcm.c` (MT6768 REF) |
| live | parent-provided live report, explicitly labelled as such (not a dump file) |

## Audit findings (the bugs this fixes)

1. **Touch coordinate scale was 16x too large.** `report()` normalised raw FTS
   reports against `FTS_X_MAX=0x437f` (17279) and `FTS_Y_MAX=0x95ff` (38400).
   Those are the DT's `focaltech,display-coords` in **1/16 pixel units**
   (`fdt.dts:4507`; `ini:37-38` says 1080 x 2400 pixels; `getprop.txt:1309-1310`
   says height 2400 / width 1080). A full-screen raw report (1080x2400) was
   therefore posted as ~1080/17279 = 6.3% of X and ~2400/38400 = 6.3% of Y:
   every touch landed in the top-left corner. The live IC reports 1081x2401
   cells (one extra per axis), consistent with the same 1:1 pixel space.
2. **Register offsets were unverified addresses.** The GPIO/SPI offset macros
   in the touch driver are MT6768 TD4160 constants (`mtk_tcm.c:63-68,128-144`),
   absent from `fdt.dts`/`iomem.txt`. They were used without any opt-in.
3. **The primary charger SC8548 was missing entirely.** The device has
   `sc8548_master@6F`, `sc,sc8548-standalone` on i2c7 (`fdt.dts:5245-5247`)
   and `tc_sc8548_charger` is loaded (`modules.txt:43,52`), but the driver's
   address gate refused every address except 0x6b, so 0x6f could never even be
   read. `kernel-modules.md:74-76` lists 4 candidate touch ICs and
   `kernel-modules.md:102` names both `tc_sc8548_charger` and `tc_sc8950`;
   both buses are now represented.
4. No SC8548 register map exists in the dump or in the alps tree (verified by
   searching both trees), so any SC8548 register offset would be invented.
   It is therefore REF + opt-in boot arg, read-only.

## `kernel/x6886/hakios.x6886_touch.c` — every changed line

Line numbers are in the **new** file.

| New lines | Change | Evidence |
|---|---|---|
| 12-15 | Document `x6886_touch_ref=1` opt-in in the boot-arg header | MT6768-only offsets: `mtk_tcm.c:63-68,128-144`; gap already recorded in `PORT-TOUCH.md:49-50,79` |
| 18-22 | Explain DT 1/16 coords vs live whole-pixel reports | `fdt.dts:4507` (`0x437f,0x95ff`); `ini:37-38` (1080/2400); `getprop.txt:1309-1310`; live 1081x2401 |
| 71-79 | Add `FTS_RAW_X_MAX=1080`, `FTS_RAW_Y_MAX=2400`, live-cell counters 1081/2401 | `(0x437f>>4)+1=1080`, `(0x95ff>>4)+1=2400` from `fdt.dts:4507`; `ini:37-38`; `getprop.txt:1309-1310`; live report for the +1 cells |
| 81-85 | Mark every GPIO/SPI register offset `REF ONLY`, gated by `x6886_touch_ref=1`; note bases are dump-verified | `mtk_tcm.c:63-68,128-144` is the only source of the offsets; bases `fdt.dts:2376-2378,1581-1583,1588-1590,4489`, `iomem.txt:96` |
| 127 | `static bool ref_offsets` state | boot arg added at 658-660 |
| 439-446 | Clamp x/y to the pixel span instead of the 16x DT maxima before posting | `fdt.dts:4507`; `ini:37-38`; `getprop.txt:1309-1310`; live 1081x2401 |
| 452 | Normalise DOWN/MOVE by `FTS_RAW_X_MAX`/`FTS_RAW_Y_MAX` | same as above; replaces the 16x divisor |
| 458 | Same fix for the UP event (previously inconsistent with DOWN) | same as above |
| 526-533 | Refuse all MMIO until `x6886_touch_ref=1`; trace `STAGE_MAP` status 3 and return false (no stub-success) | offsets REF per `mtk_tcm.c`; no write is possible before opt-in |
| 658-660 | Parse `x6886_touch_ref` (exact match `1`) | new opt-in for REF offsets |
| 661-662 | Log ref state in the existing `[X6886-touch]` line | shared pstore/printf convention preserved (lines 141-195 unchanged) |
| 663-664 | Explicit `(DT>>4)+1 == pixel` sanity check (replaces the old `/16 == pixel-1` form) | `fdt.dts:4507`; `ini:37-38` |
| 668-669 | Log raw span and live cell counts | `fdt.dts:4507`; `getprop.txt:1309-1310`; live report |

Unchanged and preserved: shared ramoops ring (`X6886_PSTORE_SIG 0x43474244`,
start +4, size +8, data +12 at `0x480af000`, derived from `fdt.dts:92`), all
bounded spins/waits (`X6886_SPI_SPIN_MAX`, `X6886_IRQ_SPIN_MAX`, `max_ticks`),
FTS frame-layout boot args, bit-bang fallback and power/clock sequencing.

## `kernel/x6886/hakios.x6886_charger.c` — every changed line

Line numbers are in the **new** file.

| New lines | Change | Evidence |
|---|---|---|
| 2 | Header now states both parts and that SC8548 is never written | `fdt.dts:5125-5127,5245-5247` |
| 4-8 | DTB/IOMEM-verified part addresses and bus windows | SC8950 `fdt.dts:5126`, `dmesg.txt:48`; SC8548 `fdt.dts:5247`; i2c5 `fdt.dts:4860` + `iomem.txt:41,100`; i2c7 `fdt.dts:5233` + `iomem.txt:43,113` |
| 9-12 | SC8548 register offset declared REF, opt-in only | no SC8548 register map in dump or alps tree (tree-wide search); `kernel-modules.md:102` only names the module |
| 45-48 | Write gate still 0x6b-only; read gate extended to 0x6f; pump 0x5f refused | `fdt.dts:5126,5247,5259-5265` |
| 135-138 | `SC8548_ADDR 0x6f` plus REF explanation | `fdt.dts:5247` (`reg = <0x6f>`) |
| 139-142 | `X6886_I2C5_PHYS/SIZE` and `X6886_I2C7_PHYS/SIZE` | `fdt.dts:4860,5233`; `iomem.txt:41,43,100,113` |
| 144 | `SC8548_PROBE_TRIES 2` (bounded, 2 x 50 ms) | bounded-wait requirement; uses existing `POLL_DELAY_US` (old line 192) |
| 235-236 | Address-gate comment updated | same as 45-48 |
| 240-241 | `chg_read()` accepts 0x6f in addition to 0x6b; `chg_write()` unchanged (0x6b-only) | `fdt.dts:5126,5247` |
| 455-459 | SC8548 probe comment: no map exists, REF boot arg, no write path | tree-wide absence of SC8548 registers; `kernel-modules.md:102` |
| 460-484 | `x6886_chg_sc8548_probe()`: validates `x6886_sc8548_reg=0..255`, at most 2 reads with 50 ms delay, logs via the existing `X6886_LOG`/pstore path, returns `ENODEV` when not opted in, never writes | REF rule + bounded waits; `fdt.dts:5245-5247`; pstore convention at old lines 64-119 (unchanged) |
| 490 | local `uint8_t sc8548` | probe result storage |
| 497-499 | Log DTB-verified i2c5 window and SC8950 address | `fdt.dts:4860,5126`; `iomem.txt:41,100` |
| 507-509 | Call the read-only probe after the provider check, before safe init; result is diagnostic only | `fdt.dts:5247`; no SC8548 enable sequence exists in any dump/alps source |

Unchanged and preserved: SC8950 register map (all `bq2589x_reg.h` citations in
the original header), caps (4.35 V / 1500 mA / 500 mA / 270 mA / 4600 mV),
presence-first ordering, single enable write, bounded verification, safe
disable path, MT6366 gauge isolation, ramoops layout and boot-arg gating.

## Verification performed

- `git diff --check` on the two C files and the manifest: clean (no trailing
  whitespace, no conflict markers).
- C structural check on both C sources: braces/parentheses/brackets balanced,
  no NUL bytes, files end in a newline.
- `git status --short` checked: only the two requested C files plus this new
  document are added/modified by this task.
- No kernel build, no flash, no write to `/home/berucrypt00/otadump`.

## Limitations / explicit REF list

- **Touch register offsets are REF**: `GPIO_DIR0_SET/CLR`, `GPIO_DOUT0_SET/CLR`,
  `GPIO_DIN0`, `GPIO_MODE4`, `SPI_CFG0/1/2`, `SPI_TX_DATA`, `SPI_RX_DATA`,
  `SPI_CMD`, `SPI_STATUS0`, `SPI_PAD_SEL`, all CMD bits and the FIFO size come
  from MT6768 `mtk_tcm.c`, not from the X6886 DTB/IOMEM. They are inert until
  `x6886_touch_ref=1`. Base addresses are DTB-verified.
- **Touch report frame geometry is still a gap**: head/point/x/y offsets and
  widths are compiled into `focaltech_ft3683g.ko`; the dump has no verified
  layout, so step 2/3 still require all `fts_*` boot args. No value invented.
- **No verified I2C controller register map** for MT6789 i2c5/i2c7 exists in
  the dump or the alps refs (only `i2c-mt65xx.ko` as a binary and CAM_I2C
  defines in `mtkcam.h`, a different block). The driver therefore keeps the
  published `x6886_charger_i2c_ops` provider; without it, init logs and returns
  `ENXIO` without touching a register. That is an explicit failure, not a
  success stub.
- **SC8548 register offset is REF**: only `x6886_sc8548_reg=N` enables a
  bounded read; SC8548 is never enabled or written by this driver.
- Axis order follows the dump (`x=1080`, `y=2400`): the live "2401x1081" report
  is interpreted as height x width, matching `getprop.txt:1309-1310` and
  `ini:37-38`. If the live order were x=2401/y=1081, the axes would need
  swapping; the dump does not support that reading.
- The panel is 144 Hz (`fdt.dts:1608`, `display.md:51`); this driver polls on
  the IRQ line at a bounded rate and does not implement 144 Hz report-rate
  negotiation.
