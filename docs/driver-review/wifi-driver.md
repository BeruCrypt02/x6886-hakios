# X6886 CONNSYS Wi-Fi port manifest

Branch: `feat/x6886-real-port`. Work done 100% offline: no adb, no fastboot, no kernel/XNU build, no flash.
Only files written are under `refs/xnu-12377-hakios/bsd/dev/` (driver + `firmware/x6886/`) and this manifest.

## 1. Source proof, including a correction to the task brief

The brief named `refs/iscle-hakios-mt6762/xnu/files/bsd/dev/hakios.wifi.c` (4394 lines) as the ConnSys base.
That file was inspected and **is not a MediaTek ConnSys driver**: its own header says
"The Wi-Fi of the POCO X5 Pro (redwood) ... The radio is a WCN6750", it talks SMEM/SMP2P/IPCC/QRTR
(`:5-10`, `:826`, `:1581`, `:3231`), calls `ath11k` (`:3270`, `:3346`) and touches a Qualcomm DSP and SMMU.
There is no `CONNSYS`, `0x18002000`, `WIFI_RAM_CODE`, I2C or MediaTek symbol anywhere in that file
(`grep -nEi 'connsys|CONNSYS|0x1800|mt6762|mediatek|WIFI_RAM_CODE' hakios.wifi.c` finds only the text "firmware").
Using it as a ConnSys base would produce a Qualcomm radio driver with fabricated addresses, which the
"no guessing / error return" rule forbids. The iscle repo has no ConnSys source in its tree or git history.

The real ConnSys MMIO/DMA/firmware-download logic that exists locally is:
- `refs/oosexgang-xnu-kernel-mtk-helio/kernel/overlay/bsd/dev/mtk_wifi.c` (1918 lines) — steps 12–13:
  driver own, WPDMA rings, INIT commands, `WIFI_RAM_CODE_*` tailer parsing, DMA chunks, EMI copies,
  `WIFI_START`, `sw_sync0` ready bits (`:121-592`).
- `refs/oosexgang-xnu-kernel-mtk-helio/kernel/overlay/bsd/dev/mtk_connsys.c` (2340 lines) — SPM power
  survey and the CONNSYS register windows (`:16-26`, `:62-75`, `:2283-2340`).
  (Identical logic is in `refs/hakios-mtk/xnu/files/bsd/dev/redwood_{connsys,mtkwifi}.c`.)

Those are the actual source of `bsd/dev/hakios.x6886_wifi.c`: the **real** logic (ring descriptors,
DMADONE bits, INIT frame layout, region features, ready bits) is kept; only addresses, firmware names,
firmware access and names of exported functions changed.

## 2. Firmware blobs copied (raw binary only; no .so/.ko)

Source directory: `/home/berucrypt00/otadump/vendor/firmware/`
Destination: `refs/xnu-12377-hakios/bsd/dev/firmware/x6886/`

| Blob | Bytes | SHA256 | Source line proof |
|---|---:|---|---|
| `WIFI_RAM_CODE_soc2_2_1d_1.bin` | 712676 | `fd19001193470a7076216cf36db017758b388a6a0ee2e9a0043a21f0dff27f4f` | `docs/reference/dump-analysis/firmware.md:49`; file in stock `vendor/firmware/` |
| `WIFI_RAM_CODE_soc2_2_1e_1.bin` | 707284 | `3b3d32e9122f7400990daf9e50305b282b8612b899f37016d864656bdf22c263` | `docs/reference/dump-analysis/firmware.md:50` |
| `soc2_2_ram_wifi_1d_1_hdr.bin` | 241680 | `3389422ea919f2448d349d21cc37deea8a2612415ecec2845d344a9be7c3a1c8` | `docs/reference/dump-analysis/firmware.md:51` |
| `soc2_2_ram_wifi_1e_1_hdr.bin` | 243556 | `28ae8b595f54f45edb5292c02a35aa8de7c82d3e08daf339bb75019dcec14059` | `docs/reference/dump-analysis/firmware.md:52` |

`file(1)` reports `data` for all four; nothing executable was copied. The `_hdr.bin` files are carried
for a later WMT patch stage; the driver in this commit downloads only the `WIFI_RAM_CODE_*` files.
`docs/reference/dump-analysis/firmware.md:299-302` shows the stock module asks for `WIFI_RAM_CODE_soc2_2` and the
chip ID is auto-detected (`persist.vendor.connsys.chipid=-1` in `vendor/build.prop:601`), so the
driver accepts `connsys_chip=1d` (default, per `firmware.md:302`) or `connsys_chip=1e`.

Offline structural check of both blobs (Python, read-only): the 36-byte tailer reports **5 regions**;
region features are `0x27, 0x7, 0x7, 0x80, 0x80` — i.e. three DMA downloads and two EMI copies,
exactly the shape `mtk_wifi.c:498-557` expects. Entry address `0x77000`, EMI offsets `0x2f0000`
and `0x386000`, both inside the 5 MB CONNSYS EMI from the device tree. No format guess was made.

## 3. Every adapted physical address / IRQ, with file:line

Evidence files are the device's own dumps:
`/home/berucrypt00/x6886-hakios/devices/x6886/evidence/fdt.dts`, `iomem.txt`, `interrupts.txt`.
(The stock dump at `/home/berucrypt00/otadump` has no fdt/iomem/interrupts text; these dumps were
produced from that same device and are the only fdt evidence available. The fdt-compatible string
`mediatek,mt6789-consys` at `fdt.dts:6956` pins them to this SoC.)

| Constant in driver | Value | Proof | Reference logic kept from |
|---|---|---|---|
| `X6886_MCU_PHYS` | `0x18002000` | `fdt.dts:6955` node name, `:6960` reg index 0, `:9291` alias | `mtk_connsys.c:66` |
| `X6886_PWRAP_PHYS` | `0x10007000` | `fdt.dts:6960` reg index 1 | `mtk_connsys.c:71` |
| `X6886_INFRACFG_PHYS` | `0x10001000` | `fdt.dts:6960` reg index 2 | `mtk_connsys.c:71-75` (constant kept, not used yet) |
| `X6886_SPM_PHYS` | `0x10006000` | `fdt.dts:6960` reg index 3 | `mtk_connsys.c:63` |
| `X6886_MCU_MISC_PHYS` | `0x180b1000` | `fdt.dts:6960` reg index 6 | `mtk_connsys.c:65` |
| `X6886_WF_CSR_PHYS` / size | `0x18000000` / `0x100000` | `fdt.dts:7571`, `:7574`; `iomem.txt:141` (`18000000-180fffff : AXI-BUS`) | `mtk_wifi.c:58-59` |
| `X6886_CONNSYS_EMI_PHYS` / size | `0x7f000000` / `0x500000` | `fdt.dts:6963-6964`, `:513-516`; `iomem.txt:168-169` (`WIFI-EMI`) | `mtk_wifi.c:40-41` |
| `X6886_WIFI_DMA_PHYS` / size | `0xbe000000` / `0x600000` | `fdt.dts:506-511` (`wifi-reserve-memory_dma`, no-map, phandle `0x24a` used by `wifi@18000000` at `:7572`); `iomem.txt:184` | `mtk_wifi.c:42-43` (MT6768's `0x51400000`/1 MB replaced) |
| `X6886_BTIF_PHYS`, `X6886_APDMA_PHYS` | `0x1100c000`, `0x10217b00` | `fdt.dts:4477-4479`, `:9182` | `mtk_connsys.c:406-408` (constants kept, not used yet) |
| `X6886_WLAN0_SPI` | `0x199` (GIC 441, IRQ line 356) | `fdt.dts:7575` (`<0 0x199 4 0>`); `interrupts.txt:175` (`wlan0`) | used in the driver's step-1 route report |
| `X6886_CONN_BTIF_SPI` | `0x19a` (GIC 442) | `fdt.dts:6961`; `interrupts.txt:174` (`BTIF_WAKEUP_IRQ`) | same |
| `X6886_CONN_LOG_SPI` | `0x19b` (GIC 443) | `fdt.dts:6961`; `interrupts.txt:173` (`CONN_LOG_IRQ`) | same |
| `X6886_CONN_MCU_SPI` | `0x6f` (GIC 143) | `fdt.dts:6961`; not present in `interrupts.txt` — gap below | same |

Offsets inside those windows are *not* invented: SPM `0x32c/0x180/0x184`, MCU `0x0/0x4`, misc
`0x10/0x1c` come from `mtk_connsys.c:16-26,62-66`; WPDMA/CONN_HIF offsets and all INIT command IDs,
region feature bits and TX/RX ring layouts come from `mtk_wifi.c:64-122,127-140`. The software-visible
firmware format was independently checked against the X6886 blobs (section 2).

## 4. Driver behaviour

- `x6886_wifi_init()` runs only with `connsys_step=N` (1 survey; 2 requires powered CONNSYS;
  3 takes driver-own, sets up WPDMA rings and reads TOP_HVR/TOP_FVR; 4 downloads WIFI_RAM_CODE and
  waits for `sw_sync0` ready bits). `connsys_chip=1d|1e` selects the blob.
- Every step reads before writing and stops on the first failed read. If SPM says CONNSYS is off,
  the driver **returns with an error and writes nothing**: the MT6366 VCN registers are unverified
  for this device (see gaps), and the brief forbids guessing constants.
- `x6886_wifi_stop()` unmaps the CSR and DMA windows and masks the device interrupts.
- The integrator must call `x6886_wifi_init()` from the board init path and add `hakios.x6886_wifi.c`
  to the build; no Makefile, build file or other driver was touched in this commit.
- The firmware lookup uses `hakios_image_file("fw/connsys/x6886", ...)`; the small image must carry
  `bsd/dev/firmware/x6886/` at `/fw/connsys/x6886/`. That packaging is the integrator's step.

## 5. Gaps (explicit, not papered over with stubs)

1. **Named source mismatch** — the iscle `hakios.wifi.c` is WCN6750/Qualcomm, not ConnSys. Section 1.
2. **MT6366 VCN supplies** — the reference powers VCN18/VCN28/VCN33 through MT6358 PWRAP registers
   (`mtk_connsys.c:145-245`), but this phone has MT6366 (`fdt.dts:6967` `pmic = <0xab>`;
   `mt6789-mt6366.ko` in `vendor_dlkm`). No verified MT6366 VCN register map exists in the local
   refs. Power-on is therefore an error return, not a guess.
3. **CONNSYS patch / WMT stage** — `wmt_launcher` expects `connsys_mt6835_mt6631.bin` and
   `connsys_mt6835_mt6635.bin`, neither present in the dump (`docs/reference/dump-analysis/firmware.md:299`).
   WMT steps 5–11 of `mtk_connsys.c` are not ported.
4. **GIC handler** — `osfmk/arm64/gic_v3_irq.c:583` only installs the kernel's own handler; there is
   no driver-facing interrupt-registration API in this tree. Wi-Fi events are therefore polled from
   the WPDMA rings as in the reference; the four SPI numbers are recorded and the device-side masks
   are programmed, but no GIC line is claimed. The unlabelled SPI `0x6f` needs on-device naming.
5. **MT6789 register-table validation** — `wf_bus()`'s chip-to-window map, `TOP_HVR/TOP_FVR/SW_SYNC0`
   and `CONN_HIF_ON_LPCTL` are the MT6768/gen4m values from `mtk_wifi.c`. `docs/X6886-DRIVER-AUDIT.md:213-218`
   flags a register-window mismatch with the MT6789 fdt; this port keeps the reference values as the
   only sourced constants and reports HVR/FVR/ready bits instead of assuming they are correct.
6. **Station / network layer** — steps 14–17 of `mtk_wifi.c` (scan, en0, WPA, join) are not ported;
   they need the `hakios_wpa` layer, which is absent from `refs/xnu-12377-hakios/bsd/sys/board/`.
   No fake `sta`/`net` callbacks were added.

## 6. Verification performed (offline)

- `wc -l` and header inspection of the named iscle source; `grep` for ConnSys symbols (none).
- `fdt.dts` / `iomem.txt` / `interrupts.txt` line citations above, read directly.
- Firmware SHA256 computed from source and from the copies; both sets match; `file` = `data`.
- Python parse of both `WIFI_RAM_CODE` tailers: 5 regions, features and offsets as above.
- No build, no flash, no device access, no changes outside the write destination and this manifest.

## 7. Boot-audit fix (same branch, later commit)

- GATE 4: every diagnostic in `hakios.x6886_wifi.c` now goes through `X6886_LOG`, which writes the
  same line to the normal log with prefix `[X6886-wifi]` **and** to the `persistent_ram_buffer` at
  physical `0x480af000` (sig `0x43474244` at +0, `start` +4, `size` +8, data at +12). The window is
  mapped once in `x6886_wifi_init()`; the ring write is bounded and spinlock-serialised, and a bad
  signature is ignored rather than panicking.
- GATE 3: `wf_init_event()` (old `:392`) now has a hard bound — at most 64 RX frames and a
  `wait_ms`-derived nanosecond deadline — so a wrong-sequence firmware stream cannot hang.
- MINOR: `CONNSYS_IP_VER_ID 0x10020501` is no longer labelled "MT6768's value"; it is the value read
  back from this chip's CONNSYS and is only compared, never required (reference `mtk_wifi.c:88`).
- **Owner sign-off still needed:** the `CONN_HIF_ON_LPCTL` / `CONN_HIF_ON_GLO_CFG` offsets at old
  `:110` are MT6768/gen4m-derived (`mtk_wifi.c:64-122`); they are not DTB-verified for MT6789.
  The driver logs the LPCTL value before and after instead of assuming it is correct.
- Mali/touch were deliberately left untouched here: another worker owns those files.
