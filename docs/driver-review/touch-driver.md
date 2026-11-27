# X6886 (MT6789) FocalTech FT3683G touch port manifest

Branch: `feat/x6886-real-port`. Done 100% offline: no adb, no fastboot, no kernel/XNU build, no flash.
Files written: `drivers/input/hakios.x6886_touch.c` and this manifest only. No `build/`, Makefile or other driver changed.

## 1. Source proof

| Role | File | Evidence |
|---|---|---|
| Framework (allowed source) | `refs/oosexgang-xnu-kernel-mtk-helio/kernel/overlay/bsd/dev/mtk_tcm.c` (505 lines) | genuine MediaTek SPI/GPIO/IRQ code; header at `:1-29` names MT6768 / TD4160 on SPI2; MMIO at `:42-46`, GPIO at `:63-124`, SPI at `:128-209`, bit-bang at `:216-267`, posting at `:397-444`, thread at `:448-505` |
| Banned source | `refs/iscle-hakios-mt6762` | NOT read, NOT copied. It is the Qualcomm POCO X5 Pro tree; it contains no MTK SPI MMIO. No symbol, string or path from it appears in the new file (`grep -niE 'iscle|qualcomm|geni|rpmh|poco|wcn'` on the new driver → no hits) |
| Device evidence | `/home/berucrypt00/hakios-x6886/devices/x6886/evidence/fdt.dts`, `iomem.txt` | live FDT decompilation and `/proc/iomem` of this handset |
| Dump evidence | `/home/berucrypt00/otadump/odm/firmware/Conf_MultipleTest.ini` | production FTS test config consumed by the shipped driver |
| Derived analysis | `/home/berucrypt00/x6886-hakios/docs/reference/dump-analysis/touch.md`, `gap-ft-chipid.md` | `.ko` string/ELF evidence, explicitly marked VERIFIED vs INFERENCE |

Chip identity: `Conf_MultipleTest.ini:23` says `IC_Type=FT3683G`; `gap-ft-chipid.md:1.1-1.3` confirms every hard identifier (`focaltech_ft3683g.ko`, `modules.alias`, `modules.load`, `Conf_MultipleTest.ini`, live `modules.txt:70`) says FT3683G, and that `ft3682g` is only an ODM supplier label (`fdt.dts:1608`, one leftover string in the `.ko`). The driver therefore targets FT3683G, not TD4160 and not a Qualcomm part.

## 2. Every constant, with dump file:line

`fdt.dts` below = `/home/berucrypt00/hakios-x6886/devices/x6886/evidence/fdt.dts`;
`iomem.txt` = same directory; `ini` = `/home/berucrypt00/otadump/odm/firmware/Conf_MultipleTest.ini`;
`touch.md` = `/home/berucrypt00/x6886-hakios/docs/reference/dump-analysis/touch.md`.

| Driver constant | Value | Verified source | Kept / changed from MT6768 |
|---|---|---|---|
| `X6886_SPI1_PHYS` | `0x11010000` | `fdt.dts:4486,4489` (`spi1@11010000`, `reg = <0 0x11010000 0 0x100>`); `iomem.txt:96` (`11010000-110100ff : ... spi1`) | Changed: TD4160 used `spi2` via board file (`mtk_tcm.c:42`); X6886 touch is a child of SPI1 |
| `X6886_SPI_SIZE` | `0x100` | `fdt.dts:4489`; `iomem.txt:96` | Kept size from `mtk_tcm.c:43` (0x1000 mapping) narrowed to the DT/iomem window |
| `X6886_GPIO_PHYS` / `_SIZE` | `0x10005000` / `0x1000` | `fdt.dts:2376-2378` (`gpio@10005000`, `reg = <0 0x10005000 0 0x1000>`) | Changed: MT6768 read GPIO base from `hakios_board()`; X6886 board table has no touch entries, so the DT address is used directly |
| `X6886_TOPCKGEN_PHYS` / `_SIZE` | `0x10000000` / `0x1000` | `fdt.dts:1582-1584` (`mediatek,mt6789-topckgen`) | New: clock-domain presence check before SPI work |
| `X6886_INFRACFG_PHYS` / `_SIZE` | `0x10001000` / `0x1000` | `fdt.dts:1589-1591` (`mediatek,mt6789-infracfg_ao`) | New: same |
| `X6886_RAMOOPS_PHYS` | `0x480af000` | `fdt.dts:92` (`ramoops.mem_address=0x48090000`, `mem_size=0xe0000`) and `fdt.dts:712-713` (pstore region); `0x480af000` lies inside it. Exact address supplied by the task | New: no equivalent in `mtk_tcm.c` (used `printf` only) |
| `FTS_PIN_IRQ` | 12 | `fdt.dts:4503` (`focaltech,irq-gpio = <0x49 0x0c 0x00>`) | Changed: MT6768 IRQ was GPIO1 (`mtk_tcm.c:16,70`) |
| `FTS_PIN_RESET` | 116 | `fdt.dts:4504` (`focaltech,reset-gpio = <0x49 0x74 0x00>`) | New: TD4160 had no reset line |
| `FTS_PIN_AVDD` | 150 | `fdt.dts:4505` (`focaltech,avdd-gpio = <0x49 0x96 0x00>`) | New |
| `FTS_PIN_IOVDD` | 15 | `fdt.dts:4502` (`focaltech,iovdd-gpio = <0x49 0x0f 0x00>`) | New |
| `vtouch-supply` | `ldo-vmch` / VMCH 2.9–3.3 V | `fdt.dts:4501` (phandle `0x143`); `fdt.dts:3616-3620` (`ldo-vmch`, min `0x2c4020`, max `0x325aa0`, ramp `0x3c`) | New: power is checked through the GPIOs; PMIC register is a gap (below) |
| `FTS_SPI_PIN_CS/CLK/MO/MI` | 37 / 35 / 36 / 34 | `fdt.dts:2829-2833` (`ctp_spimode_default`, `pinmux = <0x2501 0x2301 0x2401 0x2201>`; encoding `(pin<<8)\|func`, corroborated by `mmc0default` at `fdt.dts:2395-2399`) | Changed: MT6768 bit-bang pins 25/26/27/28 func 2 (`mtk_tcm.c:71-75`); X6886 SPI1 group is these four pins func 1 |
| `FTS_SPI_FUNC` | 1 | `fdt.dts:2833` (low byte of each pinmux cell) | Changed from 2 (`mtk_tcm.c:75`) |
| `SPI_PAD_SEL` write 0 | 0 | `fdt.dts:4488` (`mediatek,pad-select = <0x00>`) | Kept offset `mtk_tcm.c:134`, value now cited |
| `FTS_READ_CMD` | `0x03` | `touch.md:466-467` (verbatim `.ko` strings `read 03(SPI) command fail` / `SPI_V2`) | Changed: TD4160 marker/code protocol (`mtk_tcm.c:271-310`) removed |
| `FTS_MAX_POINTS` | 10 | `fdt.dts:4506` (`max-touch-number = <0x0a>`); `ini:34` (`Max_Points=10`) | Changed from 10 by coincidence; `MAX_FINGERS` (`mtk_tcm.c:334`) kept as the posting bound |
| `FTS_X_MAX` / `FTS_Y_MAX` | `0x437f` / `0x95ff` | `fdt.dts:4507` (`display-coords = <0 0 0x437f 0x95ff>`); `ini:37-38` | Changed: replaced `PANEL_W/PANEL_H` from the board file (`mtk_tcm.c:332-333`) and all picture-fit maths (`:340-382`) |
| `FTS_PIXEL_X` / `FTS_PIXEL_Y` | 1080 / 2400 | `ini:37` / `ini:38` | New: `0x437f = 1080*16-1`, `0x95ff = 2400*16-1`; checked at init, otherwise no touch is touched |
| `FTS_BUF_BYTES` | 256 | `mtk_tcm.c:451` (`uint8_t buffer[256]`); `ini:50` (`Read_Bytes=256`) | Kept |
| SPI mode 0 (`CMD` = 0) | CPHA=0, CPOL=0 | `ini:31` (`Phase_Pola=0`); no `spi-cpol`/`spi-cpha` on the node (`touch.md:609`) | Changed: TD4160 used mode 3 (`mtk_tcm.c:177`) |
| `spi_ticks` default 2 | 2 | `mtk_tcm.c:153,160-161` (kept); DT asks for 8 MHz (`fdt.dts:4499`, `0x7a1200`) but the selected clock rate is not in the dump (gap) | Kept, boot-arg overridable |
| `poll_us` default 8000 | 8000 | `mtk_tcm.c:452,456-458` | Kept, boot-arg overridable |
| Max frame length `SPI_FIFO_BYTES-2` | 30 | `mtk_tcm.c:144,173-175` (FIFO cap) | New bound for a single-CS FTS frame |
| GPIO register offsets | `0x004,0x008,0x104,0x108,0x200,0x330` | `mtk_tcm.c:63-68` | Kept; `GPIO_MODE4 = 0x340` derived from the 8-pin/0x10 stride at `:68` (flagged as inference) |
| SPI register offsets / CMD bits / DONE / spin 200000 | `0x00..0x28`, bits `0,2,8,9,12,13`, 200000 | `mtk_tcm.c:128-144,193` | Kept unchanged: this is the real MTK controller code |
| pstore ring header | sig `0x43474244`, start `+4`, size `+8`, text `+12`, ring size `0x1000-12` | same convention as the sibling X6886 drivers (`drivers/power/hakios.x6886_battery.c:38-80`); memory region `fdt.dts:92,712-713` | New; `X6886_PSTORE_SIG` kept identical so this driver cannot corrupt their ring |

## 3. What changed from the MT6768 reference

1. Bus moved from SPI2 (board table) to SPI1 at a verified MMIO window; IRQ/pins come from the X6886 FDT, not from TD4160 constants.
2. TD4160 message framing (`0xa5` marker, `0x03` continuation, `0x11` report — `mtk_tcm.c:271-310`) and the 88-bit finger layout (`:331-444`) are deleted. They do not describe FT3683G.
3. Panel-picture fitting (`PICTURE_*`, `ONE_*`, `picture_x/y`, `fbfit`) is deleted; coordinates are normalised against the dump's raw maxima `0x437f`/`0x95ff`.
4. GPIO power/reset lines and a clock-ready probe were added; the reference assumed the loader had already brought everything up.
5. Real SPI MMIO, FIFO transfer, bounded DONE polling, bit-bang fallback, IRQ-line polling and `hakios_touch_post` DOWN/MOVE/UP semantics are retained.
6. All loops are bounded. No `for(;;)` / `while(1)` / `goto`; the poll thread ends after `x6886_ticks` iterations with a log line.
7. Failure paths write `[X6886-touch]` records to ramoops and return; there is no `panic()` and no write after a failed read.
8. Boot gating changed from `touchscreen=2` + board feature to `x6886_touch=N`, because `HAKIOS_HAS_TOUCH_TD4160` does not describe this part and the X6886 board row has no SPI1/touch blocks.
9. There is no Qualcomm GENI, RPMh, SMEM or SMMU code, and no `ispcle-hakios-mt6762` content, anywhere in the file.

## 4. Boot safety

Sequence: map GPIO → read AVDD (150) and IOVDD (15) input levels → map topckgen/infracfg → map SPI1 → write/read-back a pattern in `SPI_CFG2` → restore → set pins to SPI function → `spi_setup()`. Any failure records stage/status/registers and returns before a transfer. `spi_fifo` polls DONE at most 200001 times; IRQ inner loop at most 64 times; the thread at most `x6886_ticks` times (default 360000 at 8 ms ≈ 48 min). `x6886_touch_stop()` unmaps everything and terminates the thread. The only non-returning authorisation is `kernel_thread_start`, whose failure is a logged clean return.

Ramoops: this driver writes text lines into the **same ring buffer** the other X6886 drivers use at `0x480af000` (signature `0x43474244`, start/size words, ring bytes from offset 12). It never writes a private binary format over their header under a spin lock. Line format: `[X6886-touch] stage N status 0x... seq N regs <hex...>`. Stage codes 1=map, 2=power, 3=clock, 4=pins, 5=SPI, 6=reset, 7=presence, 8=layout, 9=thread, 10=report.

## 5. Boot args

`x6886_touch=N` (0 off, 1 readiness, 2 + power/reset + presence, 3 + polling); `x6886_spi_ticks=1..16` (2); `x6886_poll_us=250..100000` (8000); `x6886_ticks=1..3600000` (360000); `x6886_bitbang=1` (off by default); `fts_reg`, `fts_frame`, `fts_head`, `fts_point`, `fts_x_off`, `fts_y_off`, `fts_xy_bytes`, `fts_reset_ms`. Out-of-range values are rejected, not clamped.

## 6. Gaps (missing from the dump — nothing invented)

1. **FTS report frame geometry** — head size, CRC offset/length, bytes per point, x/y offsets and widths are compiled constants in `focaltech_ft3683g.ko`; the live node has no `focaltech,thp_*` properties (`touch.md:550-558,653-659`). No aarch64 disassembler was available (`touch.md:656`). Step 2/3 therefore return a clean error unless the operator supplies every `fts_*` value. The first header byte is treated as the point count — a documented FTS convention, not a dump-verified fact.
2. **Chip/boot ID expected values** — the driver reads a 2-byte ID at runtime (`touch.md:503-504`) and the FW contains no ASCII ID (`touch.md:210-213`); no expected value is hardcoded. Presence is only a sanity read rejecting `00 00` / `ff ff`.
3. **Clock gate register/bit for SPI1** — `fdt.dts:4491-4492` names `parent-clk/sel-clk/spi-clk` with indices `0x7f,0x12,0x26`, but no clock-gate offset or bit is in the dump. Readiness is proven by GPIO levels and an SPI register write/read-back, not by a guessed gate bit.
4. **VMCH regulator register** — `fdt.dts:4501,3616-3620` names the supply and voltage, but not the PMIC register; the driver checks the AVDD/IOVDD GPIO levels instead.
5. **SPI clock rate** — DT requests 8 MHz (`fdt.dts:4499`); the selected parent rate is not in the dump, so `spi_ticks` stays at the reference value 2 (6.5 MHz on 26 MHz) and is overridable.
6. **Bit-bang pin roles** — the DT pinmux list does not say which of 34/35/36/37 is CS/CLK/MO/MI; the assignment in `pins_to_gpio()` is an inference and bit-bang is off unless `x6886_bitbang=1`. Controller FIFO mode does not depend on it.
7. **Frame length above 30 bytes** — the retained MTK FIFO path handles one chip-select of at most 32 bytes (`mtk_tcm.c:173-175`); a longer FTS frame needs FIFO water-level bits that are not in the verified source, so longer frames return an error.
8. **`GPIO_MODE4 = 0x340`** — derived from the MODE3 stride in the reference, not directly present in the dump.
9. **Caller wiring** — `x6886_touch_init()` / `x6886_touch_stop()` are defined here; their declaration and call site are outside the write scope of this task, so no header or boot file was changed.
10. **Panel geometry conflict** — `touch.md:560-565` notes that /16 scaling gives 1080×2400 while the known panel is 1220×2712; the task fixes 1080×2400, which the dump's `ini:37-38` and `fdt.dts:4507` support. Final on-device mapping is unverified.

No flash, no runtime test, no build was performed; this manifest is static evidence only.
