# X6886 display handoff plan — read-only research (26 Nov 2026)

**Status: PLAN ONLY.** No source, build, boot image, dump or device was touched. No
driver was written, no build was run, nothing was flashed. This document is the only
file written by this research pass. Every boot-arg step in section 8 is a *future*
change that still needs separate approval; none of its gates has been executed.

Authority order when sources disagree:

1. **DTB** — live `/home/berucrypt00/x6886-hakios/device-dump/fdt.dts` (overlays applied)
   and `/proc/iomem` in `device-dump/iomem.txt`.
2. **HANDOFF** — `docs/HANDOFF-IOS27-BOOT.md` (three independent offline audits).
3. **LOADER** — `build/loader/mtkdbg.h`, `build/loader/mtkloader.c`, `devices/x6886/board.toml`.
4. **PROC** — other `device-dump/*` captures.
5. **DERIVED** — arithmetic from the above (explicitly labelled).
6. **REF** — `refs/` material from *other phones*. REF numbers never become active
   X6886 values without an on-device read.

The stock dump at `/home/berucrypt00/otadump` was read-only and is not modified.

---

## 1. Verified address map

All bases below are DTB-proven unless marked otherwise. "FDT n" = line number in
`device-dump/fdt.dts`; "IOM n" = line in `device-dump/iomem.txt`. Every MMIO region
is 0x1000 bytes unless stated.

| Item | Address / value | DTB proof | iomem proof | Note |
|---|---|---|---|---|
| LK framebuffer base | `0xFE0C0000` | FDT 93–94 (`atag,videolfb-fb_base_l/h`) | IOM 203 (`fe0c0000-fffeffff : reserved`) | ATAG/HANDOFF 28, not a DT `reg`. |
| LK framebuffer size | `0x01DF0000` | FDT 98 (`atag,videolfb-vramSize`) | inside IOM 203 | 3 × 1088 × 2400 × 4 = `0x1DE2000`, ALIGN 64K = `0x1DF0000` **[DERIVED]**. |
| Framebuffer pitch | `4352` (`0x1100`) | FDT 93–98 imply 1088 px × 4 | — | HANDOFF 30; 1080 rounded to 32 px. Not a DT property. |
| Panel / logical size | `1080 × 2400`, 32 bpp | FDT 69, 6539 (panel name) | — | HANDOFF 13; `device-dump/X6886-specs.txt:101-113`. |
| DISP_OVL0 | `0x14005000` | FDT 53 (alias `ovl0`), 6374–6376 (`reg`) | — | HANDOFF 31. |
| DISP_OVL0_2L | `0x14006000` | FDT 54 (alias `ovl3`), 6387–6389 | — | HANDOFF 32; may be the active composer. |
| DISP_RDMA0 | `0x14007000` | FDT 55, 6400–6402 | — | downstream of OVL. |
| DISP_WDMA0 | `0x14014000` | FDT 56, 6596–6598 | — | not needed for scanout. |
| DSI0 | `0x14013000` | FDT 57 (alias `dsi0`), 6525–6527 | IOM 123 |irq SPI 0x12d=301 → hwirq 333 (GPU plan:46). |
| MIPI TX / DPHY | `0x11F60000` | FDT 9278 (alias), 6515–6517 | IOM 116 | supplies `mipi_tx0_pll`. |
| DSC wrapper | `0x14012000` | FDT 64 (alias `dsc0`), 6507–6509 | — | DSC is in the LK video path. |
| DISP_MUTEX0 | `0x14001000` | FDT 6366–6368 | IOM 119 |irq SPI 0x11d=285. |
| DISPSYS_CONFIG / MMSYS | `0x14000000` | FDT 9264 (alias), 6324–6326; 1811–1815 | IOM 118 | two nodes, same base: DRM config + clock syscon. |
| DISP_IOMMU | `0x14016000` | FDT ~6646–6656 (`iommu@14016000`, phandle `0xa3`) | IOM 124 | banks at `0x14017000`–`0x1401a000` (FDT 6611–6645). |
| DISP_PWM0 (backlight PWM) | `0x1100E000` | FDT 9263 (alias), 6314–6316 | IOM 95 |irq SPI 0xcb=203. |
| Backlight LED device | no `reg` | FDT 2320–2330 (`mediatek,disp-leds`, label `lcd-backlight`, max `0x13ff`, led-mode `4`, `pwm_config=<0 1 0 0 0>`) | — | `/sys/class/backlight` is **empty** (PROC `device-dump/display.txt:1-3`); DRM card0 is at `14000000.dispsys_config` (display.txt:6). Do not assume a standard sysfs brightness node. |
| Top clock generator (topckgen) | `0x10000000` | FDT 1582–1586 (phandle `0x2b`) | IOM 23 | |
| Infracfg AO | `0x10001000` | FDT 1589–1593 (phandle `0x2c`) | IOM 24 | reset-controller at FDT 1595–1600. |
| APMIXEDSYS (PLLs) | `0x1000C000` | FDT 1639–1643 (phandle `0x3c`) | IOM 28 | |
| SCPSYS (power domains) | `0x10006000` | FDT 1629–1636 (phandle `0x2e`) | IOM 26 | DISP domain index `0x0a`. |
| Pinctrl / GPIO | `0x10005000` | FDT 2383–2393 (phandle `0x49`) | — | `gpio-ranges` 0..0xbc (188 pins). |
| Fixed 26 MHz clock | `clk26m`, `0x018CBA80` Hz | FDT 1541–1545 (phandle `0x54`) | — | 26,000,000 Hz exactly. |
| Panel DVDD regulator | `TRANLDO23`, phandle `0x1e5` | FDT 4735–4739 (0.6–1.8 V) | — | used by `dvdd-supply` at FDT 6542 / 6561. |
| Panel GPIOs | vddi 49 (`0x31`), vci 61 (`0x3d`), reset 85 (`0x55`) | FDT 6541–6544 (and 6560–6563) | — | **do not toggle at handoff.** |
| Panel reset pinctrl | `pinmux 0x5500`, out-low / out-high | FDT 2737–2755 (phandles `0x1e2`, `0x1e3`) | — | **do not re-apply at handoff.** |
| DSI TE pinctrl / interrupt | `pinmux 0x5301`; `dsi_te` IRQ GPIO `0x53` | FDT 2757–2763; 6588–6594 | — | TE state belongs to LK. |
| XNU DRAM window | `0x140000000 + 0x0FF600000` | HANDOFF 41 | IOM 209/220 | ends `0x23f600000`, below reserved `0x23f600000-0x23fbfffff`. |

Raw ATAG cross-check: FDT 74 `atag,videolfb` words decode little-endian to
`nt37706a_fhdp_dsi_vdo_dsc_boe_boe_144hz_x6886` (0x6e743337, 0x37303661, 0x5f666864,
0x705f6473, 0x695f7664, 0x6f5f6473, 0x635f626f, 0x655f626f, 0x655f3134, 0x34687a5f,
0x78363838, 0x36002851).

**Contradictory ATAG fields — do not use to reinitialise anything:** the separate
`atag,videolfb-lcmname` property says `nt36672e_fhdp_dsi_vdo_60hz_jdi_dphy_drv`
(FDT 99), `atag,videolfb-fps` is `0x1770` = 6000 (FDT 97), and
`atag,videolfb-islcm_inited` is `0x00` (FDT 96). HANDOFF 13 explicitly states 144 Hz is
proved by the panel name, not by the fps field. Live `chosen,touch,lcm_name` (FDT 69)
and `panel1@0` (FDT 6538–6539) select the BOE 144 Hz part. The display is visibly
scanning under LK (loader banner, HANDOFF 50/54), so `islcm_inited=0` cannot be read as
"panel is off".

---

## 2. DTB-verified clocks and power for DSI / backlight

A `clocks = <phandle index>` pair is a DTB fact. The **index→name** translation for
mmsys/topckgen/infracfg comes from MT6789 clock-driver convention and is **[REF]**;
only the FDT properties listed here are X6886 proof.

| Consumer | Clock (as wired in DTB) | Proof | Name / meaning |
|---|---|---|---|
| DSI engine | mmsys (`0x36`) index `0x10` | FDT 6529, `clock-names` 6530 | `engine` |
| DSI digital | mmsys index `0x15` | FDT 6529–6530 | `digital` |
| DSI HS PHY | `mipi_tx0_pll` (phandle `0xa4`) | FDT 6529–6532, output name FDT 6521 | `hs`, `phys=<0xa4>` |
| MIPI TX PLL parent | `clk26m` = 26 MHz | FDT 6518 (`clocks=<0x54>`), 1544–1545 | fixed 26 MHz |
| DSC wrapper | mmsys index `0x0b` | FDT 6511 | DSC CG **[REF name]** |
| OVL0 / OVL0_2L | mmsys index `0x02` / `0x04` | FDT 6378 / 6391 | OVL CG **[REF name]** |
| RDMA0 | mmsys index `0x03` | FDT 6404 | RDMA CG **[REF name]** |
| DISP_MUTEX0 | mmsys index `0x00` | FDT 6370 | mutex CG **[REF name]** |
| DISP_IOMMU bclk | mmsys index `0x14` | FDT ~6650 (`clock-names="bclk"`) | bclk |
| Backlight PWM `main` | infracfg_ao (`0x2c`) index `0x23` | FDT 6319–6320 | `main` |
| Backlight PWM `mm` | topckgen (`0x2b`) index `0x1c` | FDT 6319–6320 | `mm` |
| Backlight PWM `pwm_src` | topckgen index `0x85` | FDT 6319–6320 | `pwm_src` |
| Display power domain | SCPSYS (`0x2e`) index `0x0a` | FDT 6332 | DISP power domain |
| `disp` / `disp0` top clock | topckgen index `0x04` | FDT 1633–1634 (`clock-names` 7th = `disp`), FDT 2146–2147 (`disp0`) | shared display clock |
| `mux-disp0` parents | `TOP_UNIVPLL_D6_D2`, `TOP_UNIVPLL_D4_D2`, `TOP_UNIVPLL_D6`, `TOP_MAINPLL_D4` | FDT 2135 | PLL selection; current selector **not measured** |

MMSYS clock gates are entirely inside the `0x14000000` syscon (FDT 1811–1815), the
same block as `dispsys_config` (FDT 6324–6326). An XNU driver that "only touches DSI"
can still kill scanout by changing MMSYS CG or topckgen `disp0`.

---

## 3. What LK leaves configured at the XNU handoff

These are handoff *state*, not things to reproduce:

- Panel `nt37706a_fhdp_dsi_vdo_dsc_boe_boe_144hz_x6886` is already initialised and in
  DSI **video** mode with DSC at 1080×2400/144 Hz (FDT 69, 6538–6539; HANDOFF 13).
- The DSI/DPHY/DSC PLLs, mmsys gates and SMI/IOMMU mappings are live. The panel keeps
  scanning the OVL layer buffer continuously; pixels written into that buffer appear
  on the next frame (LOADER `mtkdbg.h:9-11`). This is the whole reason no XNU panel
  init is needed.
- OVL0 or OVL0_2L is compositing LK's framebuffer. `display_find()` scans both blocks
  and every layer enabled in `SRC_CON`, then checks that ADDR lies in the LK FB window
  and PITCH is 4320..8192 (`mtkloader.c:506-550`). On X6886 LK composes **layer 1**
  (`SRC_CON=0x2`), not layer 0 (`mtkloader.c:555-557`).
- LK enables per-pixel alpha (`aen=1`) on that layer (`mtkdbg.h:11`). XNU's opaque
  console pixels therefore need alpha cleared (HANDOFF 34, LOADER `display_alpha_off()`).
- LK's other layers (unlock warning overlay) may still be enabled; the loader keeps
  only the layer that holds the framebuffer and never zeroes `SRC_CON`
  (`mtkloader.c:573-584`) — zeroing it was observed to black out the panel.
- The loader has already written the handoff fields into `boot_args.video`:
  `v_baseAddr=fb_base`, `v_rowBytes=4352`, `v_width=1080`, `v_height=2400`,
  `v_depth=32`, `v_display = XNU_VERBOSE ? 0 : 1` (`mtkloader.c:2991-2996`), and then
  issues `display_alpha_off()` (`mtkloader.c:3031`).
- The on-screen banner and pstore console prove the pipeline survived the jump to XNU
  (HANDOFF 50, 54). The loader's banner prints `fb`, `pitch` and `OVL0 SRC_CON`
  (`mtkloader.c:2764-2765`, 4081-4087).
- Backlight is on at an LK-chosen level. The DTB defines the LED device and PWM base
  (FDT 2320–2330, 6314–6320) but **no XNU kernel backlight driver exists**
  (HANDOFF 93; `docs/DRIVER-MATRIX.md:10`). Userland `libhakios-backlight.dylib` /
  `libhakios-brightness.dylib` exist in the iOS tree but do not constitute a kernel
  DSI/backlight driver.
- LK also leaves the TOPRGU watchdog configured; reference designs keep servicing it
  independently of display state (`refs/heliodarwin/wiki/Display-Pipeline.md:20`,
  patch 0200) **[REF]**.

---

## 4. What an XNU framebuffer driver must do to take over scanout

Goal: become the writer of the LK framebuffer while the panel, DSI, DSC, PLLs, PWM and
SCMI/SMI/IOMMU mappings stay exactly as LK left them.

1. **Consume `boot_args.video`, do not rediscover geometry.** Base `0xFE0C0000`,
   row bytes `4352`, 1080×2400, depth 32 (loader `mtkloader.c:2991-2996`). Reject a
   framebuffer whose pitch is not in 4320..8192 or whose base is outside the ATAG FB
   window — that is the failure mode fixed in HANDOFF 50.
2. **Map the FB window into XNU VA with explicit cache policy.** Physical window
   `0xFE0C0000..0xFE0C0000+0x1DF0000` (FDT 93–94, 98; IOM 203). Use write-combine or
   an uncacheable mapping plus explicit clean/invalidate around each present; do not
   let the CPU and OVL disagree about the same DRAM. Cache granularity differs between
   CPU (16 KiB) and GPU (4 KiB) pages (`docs/X6886-GPU-PLAN.md:135`).
3. **Read the live OVL state before writing anything.** `SRC_CON`, the enabled layer's
   `PITCH` and `ADDR` on both `0x14005000` and `0x14006000` (loader `display_find()`).
   Do not assume OVL0 layer 0: on this board LK uses layer 1.
4. **Keep exactly the front-buffer layer enabled.** Clear the alpha bit on that layer
   if writing opaque pixels, keep only that layer's bit in `SRC_CON`, and never write
   `SRC_CON=0` (loader `mtkloader.c:555-584`).
5. **Publish a framebuffer device that reports 1080×2400 with a 4352-byte stride and
   32 bpp.** Pixel channel order (RGBA vs BGRA) is **not proven** by any local file;
   establish it with a known colour test pattern before declaring correct output.
6. **Re-arm only the presentation path, if needed.** A mutex enable/run pulse uses
   `DISP_MUTEX0 +0x20/+0x24` **[DERIVED, not DTB]**. A DSI re-push uses the serialized
   sequence in section 7 **[REF]**. Neither touches panel init.
7. **Preserve backlight and watchdog.** Do not reprogram `DISP_PWM0` or panel GPIO/reset
   during takeover; keep TOPRGU serviced on its own timer **[REF]**.
8. **Fail closed.** If `boot_args.video` is missing/invalid or the live OVL layer cannot
   be identified, present nothing and leave LK's pixels alone. A black panel from a
   wrong `SRC_CON` write is the known regression (loader comment, mtkloader.c:555-557).

**Must NOT do:** issue an LCM init sequence; toggle reset/VDDI/VCI GPIOs; re-apply
`lcm_rst_out0/1` pinctrl; rewrite DSI PLL/DSC timing; reset the DPHY; reprogram SMI or
display IOMMU; change topckgen `disp0` or MMSYS CG; zero `SRC_CON`; assume the stale
`atag,videolfb-lcmname`/fps/islcm_inited fields.

---

## 5. Passing a GPU-rendered surface into the existing framebuffer first

This is deliberately the *first* display consumer of the GPU; an XNU DSI driver is a
separate, later task (`docs/X6886-GPU-PLAN.md:123`).

1. GPU completes a fragment job into a staging BO (JM job completion, zero fault bits:
   GPU plan gate at line 119). Do not pass a CPU VA as a GPU VA; CPU pages are 16 KiB,
   GPU pages 4 KiB (GPU plan:135).
2. Prefer a buffer **inside the existing LK FB window**. The DTB-attached display IOMMU
   (`FDT 6381`, `6394`, phandle `0xa3` at ~FDT 6656; IOM 124) already maps LK's memory;
   a new external GPU buffer has no proven SMI mapping. Three-buffer accounting from
   FDT 98 gives a per-buffer size of `4352 × 2400 = 0x9F6000` (10,444,800 B), total
   `0x1DE2000`, rounded to `0x1DF0000` **[DERIVED — exact buffer boundaries are not
   DTB-proven; verify before hardcoding offsets]**.
3. Write into the buffer that is **not** currently scanned (discovered from OVL ADDR in
   step 4 of section 4), with the 4352 stride and 1080 visible columns. Never overwrite
   the live front buffer mid-frame.
4. Clean/invalidate the written lines from the CPU, then flip by programming that
   layer's OVL ADDR to the new buffer's physical address and pulsing the mutex; keep
   `SRC_CON` unchanged. Do not add a second competing DSI push while a push is busy.
5. Alternative, lower-risk first step: CPU `memcpy` the finished GPU surface into the
   currently scanned buffer during a quiescent period. This avoids ADDR flips and SMI
   questions at the cost of a copy; use it as the fallback gate if flipping tears.
6. Verify colour, orientation, stride and absence of tearing with a static GPU-drawn
   test pattern **before** any SpringBoard animation (GPU plan gate at line 123).

---

## 6. REF-only offsets and other non-DTB values

Everything in this table is explicitly **not** X6886 DTB proof. It must not be copied
into an active X6886 board file without a device read.

| Offset / value | Applies to | Origin | Status |
|---|---|---|---|
| `OVL_SRC_CON +0x2c` | OVL0 / OVL0_2L | `mtkdbg.h:53`, HANDOFF 33 | local derived from vendor headers/patches; not DTB |
| `OVL_L0_CON +0x30`, `OVL_L0_PITCH +0x44`, `OVL_L0_ADDR +0xf40` | OVL0 | `mtkdbg.h:54-56`, HANDOFF 34 | local derived; live-read by loader |
| `OVL_Ln_*` stride `+0x20` (L1 PITCH `+0x64`, ADDR `+0xf60`; L2/L3 at `+0x84/+0xa4`, `+0xf80/+0xfa0`) | OVL0 | `mtkdbg.h:57-62`, HANDOFF 35–36 | L2/L3 **extrapolated, not directly read** |
| `OVL_SRC_SIZE` | OVL0 | no local definition, HANDOFF 37 | UNPROVEN — do not use |
| `DISP_MUTEX_EN +0x20`, `DISP_MUTEX_RUN +0x24` | `0x14001000` | `mtkdbg.h:47-48` | local derived |
| `DSI_START +0x00`, `DSI_INTSTA +0x0c` (busy = bit 31, not bit 0) | `0x14013000` | `refs/heliodarwin/wiki/Display-Pipeline.md:30-33`; `x676c/shim/display.c:10-11`; patches 0146/0177/0190 | **REF (X676C)** |
| `DSI_CON_CTRL +0x10` reset pulse | `0x14013000` | `Display-Pipeline.md:32` | **REF (X676C)** |
| `100 ms` busy-recovery timeout, nonblocking DSI gate | DSI push | `Display-Pipeline.md:26-37` | **REF (X676C)** |
| mmsys/topckgen clock index→name mapping | clock controllers | MT6789 driver convention | **[REF name]**, indices are DTB facts |
| DISP_PWM duty/enable register offsets | `0x1100e000` | no local X6886 definition | UNKNOWN — base only is DTB-proven |
| Per-buffer offsets inside the FB window | `0xFE0C0000` | FDT 98 arithmetic | **[DERIVED]**, verify before use |
| `sweetfb_native`, `sweetbl`, `sweetfb_trace`, `sweetreboot` boot args | HelioDarwin | Display-Pipeline.md:45-47; patches 0110, 0188 | **REF** naming only; X6886 names are defined in section 7 |
| `0xfeabe000` / stride 4320, `0xfe0c8000` | X676C buffers | `Display-Pipeline.md:14-15` | **REF — explicitly wrong for X6886** (HANDOFF/DRIVER-MATRIX 7) |

---

## 7. Ordered boot-arg-gated steps and verification gates

The loader assembles `boot_args.commandLine` itself from `XNU_EXTRA_ARGS`
(`mtkloader.c:150-151`, 2996–3011); there is no passthrough of LK's `bootargs` into
XNU. To set a gate, a *future, separately approved* loader rebuild must add the flag
to `XNU_EXTRA_ARGS`, e.g.
`-DXNU_EXTRA_ARGS="cpus=1 x6886fb=1 x6886fb_trace=1"`. Every gate defaults to **off**.
Kernel-side checks use `PE_parse_boot_argn` (same pattern as REF patch 0110). Removing
the flag is the rollback; no state is written to the device by this document.

Offline gates (before any boot, no device needed):

- **G0 — address provenance.** A diff of any future board file contains only bases from
  section 1. Gate: zero occurrences of X676C addresses (`0xfeabe000`, `0xfe0c8000`,
  4320) in active X6886 fields.
- **G1 — no reinit path.** Static audit finds no calls to panel reset GPIOs, LCM init
  tables, DSI PLL/DSC setup or MMSYS CG changes on the takeover path. Gate: empty result.

Boot-arg gates (ordered; each requires the previous gate to pass):

| # | Boot arg | Action | Verification gate | Rollback |
|---|---|---|---|---|
| B0 | *(none)* | Baseline current loader + XNU. | Banner shows `fb fe0c0000 pitch 4352`; pstore console at `0x480af000` (HANDOFF 38). | n/a |
| B1 | `x6886fb=1` | Map the `boot_args.video` window; register a framebuffer device with 1080×2400 / 4352 / 32 bpp. No MMIO writes. | Kernel log shows base `0xfe0c0000`, pitch `4352`, size `0x1df0000`; panel still shows the LK banner unchanged. | remove flag |
| B2 | `x6886fb_probe=1` (requires B1) | Read-only dump of `SRC_CON`, per-layer `PITCH`/`ADDR` on OVL0 and OVL0_2L. | Enabled layer index and ADDR match the loader banner; pitch `4352`; ADDR inside the ATAG FB window. No writes logged. | remove flag |
| B3 | `x6886fb_alpha=1` (requires B2) | Clear alpha bit on the selected layer; keep only that layer in `SRC_CON`; pulse mutex **[DERIVED offsets]**. | Opaque test pixels render with no black screen and no ghost overlay; never `SRC_CON=0`. | remove flag, reload LK |
| B4 | `x6886fb_trace=1` (requires B3) | Write a known static pattern into a non-scanned buffer and flip OVL ADDR; log source/pixel hashes. | Pattern visible on glass at 1080×2400 with correct colour order and no tearing; no DSI init/reset writes. | remove flag |
| B5 | `x6886fb_surface=1` (requires B4) | Accept a completed GPU surface, clean caches, copy or flip into the FB window (section 5). | Static GPU pattern appears before SpringBoard animation; GPU job reached terminal state with zero faults; no panel reinit. | remove flag |
| B6 | `x6886dsi=1` (requires B5) | Optional serialized DSI re-push with busy check **[REF offsets]**. | `DSI_INTSTA` busy clears within 100 ms; no concurrent push; TOPRGU still serviced. | remove flag |
| B7 | `x6886bl=1` (requires B5) | Optional backlight brightness service through `DISP_PWM0` (base DTB-proven, offsets UNKNOWN). | Readback matches request; panel never goes black; no sysfs assumption. | remove flag |
| B8 | `x6886fb_panelreinit=1` | **FORBIDDEN — not implemented.** Panel is already initialised by LK. | No code path exists; any future request needs new evidence and separate approval. | n/a |
| B9 | *(final)* | Keep only B1–B5 flags needed for production; drop trace/probe. | Full SpringBoard frame visible at native 1080×2400; touch coordinates still map (HARDWARE 35–37). | revert to B0 |

A pass requires **both** the logged gate and a physical observation; pixel hashes alone
do not prove DSI transmitted the pixels (REF `Display-Pipeline.md:69`). If a gate
fails, stop at that step: a black panel under XNU is the documented `SRC_CON` failure.

---

## 8. Open items / blockers

1. Pixel format (RGBA vs BGRA), exact OVL ADDR write semantics, and the three buffer
   boundaries are not DTB-proven; verify before hardcoding.
2. No XNU kernel DSI or backlight driver exists in this tree (HANDOFF 93,
   DRIVER-MATRIX 10). B6/B7 have no implementation to gate yet.
3. `OVL0_2L` may be the active composer; the loader's alpha handling still needs a
   final on-glass check (HANDOFF 84).
4. MIPI PLL selector and topckgen `disp0` parent are not measured on X6886; do not
   change them.
5. `sweetfb_native`, `sweetbl` etc. are HelioDarwin REF names; X6886 names in section 7
   are proposed, not present in any source.
6. No device, boot image or dump was modified in producing this plan; no gate above was
   executed.
