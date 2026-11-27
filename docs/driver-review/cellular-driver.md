# X6886 (MT6789) cellular control channel port manifest

Branch: `feat/x6886-real-port`. Work done 100% offline: no adb, no fastboot, no build, no flash,
no device access. Only two files were written: `drivers/modem/hakios.x6886_cell.c` and this manifest.
No other driver, no `refs/` repo and no `build/` file was modified or staged.

## 1. Source proof (genuine MediaTek MT6789 code)

Everything translated comes from the 6.12 kernel module tree, never from guesswork:

- `refs/alps-device-modules-6.12/drivers/misc/mediatek/eccci/hif/ccci_dpmaif_v1_6789.c/.h` —
  the MT6789-specific SMEM DRB implementation (Copyright 2025 MediaTek).
- `.../hif/ccci_dpmaif_reg_v1.h`, `.../hif/ccif_hif_reg.h`, `.../hif/ccci_hif_ccif.c`,
  `.../hif/ccci_ringbuf.h` — register windows, CCIF doorbell, ring control layout.
- `.../port/port_cfg.c` — the char node named `ttyC0` (the AT command queues).

Physical addresses and IRQs are from this device's own dumps (`devices/x6886/evidence/fdt.dts`,
`devices/x6886/evidence/interrupts.txt`), not from the donor port.

## 2. Constants with proof

| Constant in driver | Value | Proof (`devices/x6886/evidence/…`) |
|---|---|---|
| `X6886_DPMAIF_AO_PHYS` | `0x10014000` | `fdt.dts:4159` node `dpmaif@10014000`, reg index 0 at `:4161` |
| `X6886_DPMAIF_PD_MD_PHYS` | `0x1022d000` | `fdt.dts:4161` reg index 1 |
| `X6886_DPMAIF_PD_AP_PHYS` | `0x1022c000` | `fdt.dts:4161` reg index 2 |
| `X6886_DPMAIF_PD_UL_PHYS` | `0x1022e000` | `fdt.dts:4161` reg index 3 |
| `X6886_DPMAIF_SIZE` | `0x1000` | `fdt.dts:4161` (each window is 4 KB) |
| `X6886_DPMAIF_SPI` / `X6886_DPMAIF_GIC` | `0xf1` / `273` | `fdt.dts:4162` `<0 0xf1 4 0>`; `interrupts.txt:133` `GICv3 273 Level DPMAIF_AP_RX0` |
| `X6886_CCIF_AP_PHYS` | `0x10209000` | `fdt.dts:4172` node `ccifdriver@10209000`, reg index 0 at `:4174` |
| `X6886_CCIF_MD_PHYS` | `0x1020a000` | `fdt.dts:4174` reg index 1 |
| `X6886_CCIF_SIZE` | `0x1000` | `fdt.dts:4174` |
| `X6886_CCIF_SPI0` / `GIC0` | `0xe2` / `258` | `fdt.dts:4176`; `interrupts.txt:131` `GICv3 258 Level CCIF_AP_DATA0` |
| `X6886_CCIF_SPI1` / `GIC1` | `0xe3` / `259` | `fdt.dts:4176`; `interrupts.txt:132` `GICv3 259 Level CCIF_AP_DATA1` |
| `X6886_SMEM_CACHED_PHYS` / size | `0x8c000000` / `0x16a0000` | `fdt.dts:549-553` `ap_md_c_smem` |
| `X6886_SMEM_NC_PHYS` / size | `0x8e000000` / `0xe20000` | `fdt.dts:585-589` `ap_md_nc_smem` |
| `X6886_CCCI_TAG_PHYS` / size | `0xbf5f0000` / `0x10000` | `fdt.dts:561-565` `ccci_tag_mem` |
| ramoops `0x480af000`, sig `0x43474244` | — | established contract in `wifi-driver.md:7` and `hakios.x6886_wifi.c:35-48` (same branch) |

GIC numbers are the SPI plus the 32-line GIC SPI offset: `0xf1 = 241 -> 273`,
`0xe2 = 226 -> 258`, `0xe3 = 227 -> 259`; the dump confirms each mapping by name.

Register-level constants (all from the named MediaTek sources, no new addresses):

| Constant | Value | Proof |
|---|---|---|
| `APCCIF_CON/BUSY/START/TCHNUM/RCHNUM/ACK` | `0x00/04/08/0c/10/14` | `hif/ccif_hif_reg.h:30-35` |
| `X6886_AT_CELL_CHANNEL` | `0` | `hif/ccci_hif_ccif.c:902-909` (data notify on channel 0) |
| `X6886_AT_TTY_MINOR` | `3` | `port/port_cfg.c:41` (`char_port_ops, 3, "ttyC0"`, `DATA_AT_CMD_Q`) |
| `X6886_CELL_QUEUE_NUM` / `_SIZE` | `3` / `1024` | `hif/ccci_dpmaif_v1_6789.h:34-37` |
| `X6886_CELL_DATA_LEN` | `1600` | `hif/ccci_dpmaif_v1_6789.h:39` (`DPMA_SKB_DATA_LEN`) |
| ring `read/write/length` layout | — | `hif/ccci_ringbuf.h:18-22` (`struct ccci_ringbuf`) |
| poll bound `100 x 1000us` | — | `hif/ccci_dpmaif_v1_6789.c:579` (`++count >= 1000` in the reset wait) |

## 3. What was translated, and what was deliberately not

Translated (safe subset):
- AP-local ring setup: UL/DL rings use the `read/write/length` control layout of
  `ccci_ringbuf.h:18-22` and the queue geometry of `ccci_dpmaif_v1_6789.h:34-39`.
  Descriptors live in `kalloc`'d AP memory. The SMEM DRB layout of
  `ccci_dpmaif_v1_6789.c:400-460` (`dpmaif_alloc_smem_to_drb`) was **read, not executed**:
  no `memset` or descriptor is written into SMEM.
- Doorbell: the AP-side `APCCIF_BUSY` / `APCCIF_TCHNUM` sequence of
  `ccci_hif_ccif.c:902-909`, following `dsb sy`. It is gated off by default and only
  fires with `cell_doorbell=1`.
- EMI read of modem state: four words from the start of the non-cached AP<->MD window
  plus the AP CCIF state registers (`ccif_hif_reg.h:30-35`, dump at
  `ccci_hif_ccif.c:168-205`). This is a bounded read and logs raw values; no state field
  is decoded from a constant that could not be proven.
- AT transport: `x6886_cell_at_bind/send/recv` bridge bounded AT bytes to/from the UL/DL
  rings and the `ttyC0` char node named in `port_cfg.c:41`.

Not translated: modem image loading, NVRAM/IMEI/protect1/protect2 access, the HW reset
and bus-protect writes in `drv1_hw_reset_for_6789()` (`ccci_dpmaif_v1_6789.c:589+`),
any SMEM write, and any workqueue/CCCI IRQ handler registration.

## 4. Safety confirmation

- **ZERO writes to modem, NVRAM, IMEI, protect1 or protect2.** EMI/SMEM access is
  read-only; the only MMIO writes in the file are the two AP-side CCIF doorbell registers,
  and those require an explicit `cell_doorbell=1` boot-arg.
- **No flash command is generated** anywhere in the file or this manifest.
- Nothing runs without `cell_step=N`. Without it the driver logs and returns 0.
- All failure paths unmap their windows and return a negative errno (or -1). There is no
  `panic()`, no `assert`, no unbounded `while(1)`, no retry loop without a counter.
  Modem polling is capped at 100 x 1000 us; busy CCIF skips the doorbell.
- Missing hardware (`ml_io_map` fails, SMEM unreadable, ring `kalloc` fails) is a clean
  error return, never a fault.
- Boot diagnostics go to the log **and** to pstore at `0x480af000` with sig `0x43474244`
  using the existing bounded, spinlock-serialised ring write; a bad signature is ignored.
- No other driver, Kconfig, Makefile or build file was touched.

## 5. CommCenter / RIL / cellular userspace audit (read-only)

Scanned every repo and branch under `refs/` except `iscle-hakios-mt6762` (explicitly BANNED),
plus `build/ios27-overlay/`, using `git grep` across all refs of each repo and a text-only
scan of the overlay. Patterns: `CommCenter`, `comm_center`, `pdp_ip`, `ctl_data`, `libril`,
`ril.so`, `cellular bridge`, and `ttyC0`.

**NO CommCenter bridge found in refs/.** There is no functional CommCenter, RIL,
`pdp_ip0` or AT userspace bridge in any repo or branch. What exists is plumbing, not a bridge:

- `com.hakios.nullcomm.plist` (and `com.redwood.nullcomm.plist`) plus `src/nullxpc.c` in
  `oosexgang-xnu27` (all branches: `devices`, `devices2`, `ios27`, `ios27b`, `main`,
  `mali-native`), `oosexgang-xnu-kernel-mtk-helio` (`main`, `ios27`, `mali-native`),
  `hakios-mtk`, and `build/ios27-overlay/SRC/{ios27,mali-native}/userland/`. The plist's own
  comment says: *"CommCenter's services, there and answering nothing … this phone has no
  baseband for CommCenter"* — these are null XPC stubs, not a bridge.
- `refs/xnu-12377-hakios/bsd/dev/board/cell.c` (also `refs/hakios-common/drivers/board/cell.c`)
  is an **en1 data bridge** over the IPA (`hakios_ipa.c`, QMAP mux 1). It states that
  Darwin's `pdp_ip0` is made by a baseband stack "which is not here". Its `pdp_ip` hits are
  explanatory comments only.
- Other `ctl_data` hits are generic XNU kernel-control internals (`bsd/kern/kern_control.c`,
  `bsd/net/content_filter.c`, `if_utun.c`, `ntstat.c`, `flow_divert.c`, …), not cellular.
- `ttyC0` appears in exactly one place across all of `refs/`: the MediaTek Linux source
  `refs/alps-device-modules-6.12/.../eccci/port/port_cfg.c:41`. No XNU-side device node,
  tty glue, or userspace bridge implements it.

Consequence: the driver exposes `x6886_cell_at_bind()` as the integration point. Until an
existing `/dev/ttyC0` char node is bound, AT send/recv return an error and nothing is
written. This is reported as a gap, not papered over with a fake device.

## 6. Gaps (explicit, not stubbed)

1. No `/dev/ttyC0` device exists in this XNU tree — the AT transport boundary needs a
   real char node bound via `x6886_cell_at_bind()`; the reference driver's `char_port_ops`
   is Linux-only.
2. No GIC handler is claimed for DPMAIF (GIC 273) or CCIF (GIC 258/259); as in the Wi-Fi
   port, these XNU sources have no driver-facing IRQ registration API. IRQs are recorded,
   modem presence is polled with a bounded loop.
3. The modem-state words are logged raw because no DTB- or source-proven state-word offset
   was found. Decoding a state machine would require guessing, which is forbidden.
4. MMIO windows are mapped with `ml_io_map` and only read; no `VM_WIMG_WCOMB` caching
   attribute was selected for the doorbell window since the doorbell is off by default.

## 7. Verification performed (offline)

- `nl -ba` / `grep -n` on `fdt.dts` and `interrupts.txt` for every address and IRQ above.
- Read of the full 665-line `ccci_dpmaif_v1_6789.c` and the four cited headers.
- Read of existing `hakios.x6886_wifi.c` / `wifi-driver.md` to match the pstore and boot-arg
  conventions exactly.
- `git grep` over every `refs/` repo and all its local/remote refs, plus a text-only scan
  of `build/ios27-overlay/`, for the audit patterns (result in section 5).
- Balanced-delimiter check on the new C file (`{`/`}` and `(`/`)` counts match).
- No build, no flash, no device access, no changes outside the two declared files.
