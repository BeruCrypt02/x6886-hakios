# X6886 (MT6789) real MUSB USB2.0 + MediaTek T-PHY v2 port manifest

Branch: `feat/x6886-real-port`. Done 100% offline: no adb, no fastboot, no kernel/XNU build, no flash, no runtime test.
Files written: `drivers/core/hakios.x6886_usb.c` and this manifest only. No `build/`, Makefile, header, boot file or other driver touched.

## 1. Source proof

| Role | File | Evidence |
|---|---|---|
| Genuine controller source | `refs/alps-device-modules-6.12/drivers/misc/mediatek/usb/usb20/mtk_musb_reg.h` (545 lines) | real MUSB register map: offsets `:197-258`, endpoint registers `:260-298`, MTK `MUSB_SWRST` `:300-309`, L1 interrupt `:311-336`, OTG20 `:345-400` |
| Genuine controller logic | same dir `musb_core.c` (4918 lines) | PIO FIFO `:383-464`, `musb_start()` `:1241-1320`, `musb_generic_disable()` `:1322-1344`, `musb_core_init()` `:1842-1956`, interrupt role read `:1968-1981`, device/host detection `:3715-3725`, software reset `:4218-4220` |
| Genuine indexed access | same dir `musb_core.h` | `musb_ep_select()` `:190-192`, `test_devctl_hst_mode()` `:201-202`, FIFO-size decode `:506-533` |
| Genuine endpoint setup | same dir `musb_gadget.c` | bulk endpoint enable `:1537-1559`; `MUSB_TYPE_PROTO_SHIFT` comes from `mtk_musb_reg.h:109-110` |
| Genuine role glue | same dir `mtk_dual_role.c` | DFP/UFP vs power/is_host mapping at `:20-52`; not ported, cited only |
| Genuine PHY source | `refs/alps-device-modules-6.12/drivers/phy/mediatek/phy-mtk-tphy.c` | bank layout `:40-42`, USB2 register/bit definitions `:49-167`, `u2_phy_instance_init()` `:1303-1348`, `u2_phy_instance_power_on()` `:1350-1410`, `phy_v2_banks_init()` `:1756-1767`, dispatch `:2231-2281` |
| Device truth | `devices/x6886/evidence/fdt.dts` | live FDT decompilation of this handset |
| Device truth | `devices/x6886/evidence/iomem.txt`, `interrupts.txt`, `modules.txt` | live `/proc/iomem`, `/proc/interrupts`, `/proc/modules` |

Chip identity: `fdt.dts:7458` says `compatible = "mediatek,mt6789-usb20"` — this is **MUSB**, not DWC3. `interrupts.txt:49` names the line `musb-hdrc`; `modules.txt:371-374` shows live `musb_hdrc`, `musb_main`, `phy_mtk_tphy`, `phy_generic`; `modules.txt:340` shows `phy_mtk_ufs` is a separate UFS PHY and was not copied. `modules.txt:193` (`musb_boost`) and `:370` (`usb_meta`) are Transsion/MTK add-ons, not used here. `fdt.dts:7517-7533` is `mediatek,fpga-u3phy` with `status = "disabled"` — deliberately ignored.

## 2. Every constant, with dump file:line

`fdt.dts`, `iomem.txt`, `interrupts.txt`, `modules.txt` below are under `/home/berucrypt00/x6886-hakios/devices/x6886/evidence/`.
Source line references without a prefix are in the two genuine MediaTek files named in §1.

| Driver constant | Value | Verified source |
|---|---|---|
| `X6886_MUSB_PHYS` / `_SIZE` | `0x11200000` / `0x10000` | `fdt.dts:7459` (`reg = <0 0x11200000 0 0x10000 ...>`); `iomem.txt:104` (`11200000-1120ffff : usb0@11200000`); `plan/uboot-availability.md:133` corroborates MUSB at `0x11200000` |
| `X6886_PHY_PHYS` / `_SIZE` | `0x11f40000` / `0x700` | `fdt.dts:7494` (`reg = <0 0x11f40000 0 0x700>`); `iomem.txt:114-115`; `fdt.dts:7459` also lists a second 0x10000 region at the same base for the controller binding |
| `X6886_USB_IRQ` | 160 | `fdt.dts:7460` (`interrupts = <0 0x80 4 0>` = SPI 128 + 32); `interrupts.txt:49` (`GICv3 160 Level musb-hdrc`) |
| `X6886_TOPCKGEN_PHYS` | `0x10000000` | `fdt.dts:1582-1586`, phandle `0x2b` referenced by `ref_clk` at `fdt.dts:7461-7462` |
| `X6886_INFRACFG_PHYS` | `0x10001000` | `fdt.dts:1589-1593`, phandle `0x2c` referenced by `sys_clk` at `fdt.dts:7461-7462` |
| `X6886_APMIXED_PHYS` | `0x1000C000` | `fdt.dts:1639-1643`, phandle `0x3c` referenced by `src_clk` at `fdt.dts:7461-7462` |
| `X6886_CLK_SYS/REF/SRC_IDX` | `0x22` / `0x1d` / `0x0d` | `fdt.dts:7461-7462` (clock phandle/index triples in `sys_clk,ref_clk,src_clk` order) |
| `X6886_CLK_PHY_REF_HZ` | 26 MHz | `fdt.dts:7495-7496` (phy `clocks = <0x54>`, `"ref"`), `fdt.dts:1541-1545` (`clk26m`, `clock-frequency = 0x18cba80`, phandle `0x54`) |
| `X6886_USB_NUM_EPS` | 16 | `fdt.dts:7466` (`num_eps = <0x10>`); `musb_core.h:147-149` also defaults `MUSB_C_NUM_EPS` to 16 |
| `dr_mode` | otg | `fdt.dts:7468`; role-switch at `:7469`; `usb-role-switch` caller alias at `:9318-9319` |
| `X6886_USB_EP` | 1 | first bulk pair after EP0; existence is **read back** at runtime from indexed `FIFOSIZE` (`musb_core.h:506-533`) — not assumed from the dump |
| `X6886_USB_MAX_PACKET` / FIFO code 6 | 64 B | `mtk_musb_reg.h:11` (`MUSB_EP0_FIFOSIZE 64`) and `:72-73` (size code encodes 8..4096 B); EP0 cap is reused as the debug channel's safe cap |
| `X6886_RAMOOPS_PHYS` / `_SIZE` / sig | `0x480af000` / `0x1000` / `0x43474244` | `fdt.dts:92` (`ramoops.mem_address=0x48090000`, `mem_size=0xe0000`) and `fdt.dts:712-715` (pstore region); exact sub-window supplied by the task; ring layout `+0 sig, +4 start, +8 size, +12 data` matches `drivers/power/hakios.x6886_battery.c:38-80` |
| MUSB common/endpoint offsets | `0x00..0x7e`, FIFO `0x20+4*n`, indexed `0x10+off` | `mtk_musb_reg.h:197-258`, `:215`, `:260-298`, `:277-279` |
| `MUSB_POWER` bits | ENSUSPEND/HSENAB/SOFTCONN | `mtk_musb_reg.h:19,20,25`; sequence `musb_core.c:1301-1309` |
| `MUSB_DEVCTL` role bits | BDEVICE `0x80`, HM `0x04`, VBUS `0x18>>3` | `mtk_musb_reg.h:39-46`; decision `musb_core.c:3718-3725` and `:1981` |
| `MUSB_SWRST` reset bits | DISUSBRESET\|SWRST | `mtk_musb_reg.h:308-309`; write `musb_core.c:4218-4220` |
| CSR/FIFOSZ bits used | TXPKTRDY, RXPKTRDY, AUTOCLEAR, DPB, SIZE | `mtk_musb_reg.h:71-73,129,131,156,163,166` |
| `MUSB_TYPE_PROTO_BULK` | `2 << 4` | `mtk_musb_reg.h:109-110` + `USB_ENDPOINT_XFER_BULK` from `linux/usb/ch9.h`; bulk usage `musb_gadget.c:1549` |
| T-PHY COM bank | PHY base + `0x300` | `phy-mtk-tphy.c:42`, `:1766` (`phy_v2_banks_init`, matching `fdt.dts:7486` `generic-tphy-v2`) |
| T-PHY ACR0/4/5/6, DTM0, DTM1 offsets | `0x000/0x020/0x014/0x018/0x068/0x06c` | `phy-mtk-tphy.c:52,107,79,84,123,151` |
| T-PHY init/power bits & delays | FORCE_UART_EN, FORCE_SUSPENDM, XCVRSEL=1, DATAIN, INTR_EN, 100uA off, GPIO mask, BC11 off, OTG VBUSCMP, VBUSVALID/AVALID, SESSEND, REV6; 30 us + 800 us | `phy-mtk-tphy.c:1310-1342`, `:1357-1391` |
| PHY `rx-sqth=5`, `discth=0x0c`, `rev6=2` | written to ACR6 SQTH/DISCTH/REV6 | `fdt.dts:7501,7502,7510`; fields `phy-mtk-tphy.c:86-87,96,99` |
| Regulator `vusb` | always-on, do not touch | `fdt.dts:3498-3504` (`regulator-name = "vusb"`, `regulator-always-on`) |
| Regulator `usb-otg-vbus` | on a disabled node, do not touch | `fdt.dts:4921-4929` (child of the node with `status = "disabled"` at `fdt.dts:4919`) |
| `pericfg` / `infracg` phandles | `0xb9` / `0xba` | `fdt.dts:7472-7473` → `fdt.dts:2370-2373` / `2359-2362`; logged as provenance, no register write |

## 3. What was translated

1. **PHY init**: the genuine `u2_phy_instance_init()` + `u2_phy_instance_power_on()` register sequence, including the suspend-M pulse and both delays, then a bounded 64-iteration readback of DTM1 `VBUSVALID|AVALID` with `SESSEND` clear.
2. **Controller**: HWVERS presence check before any functional write, MTK `MUSB_SWRST` software reset (`DISUSBRESET|SWRST`), DEVCTL-based host/peripheral decision (BDEVICE/HM), the reference `musb_start()` interrupt/POWER setup, and the reference "NOISE_STILL_SOF / BABBLE_CLR_EN" ULPI data tweak.
3. **Bulk transport**: EP1 IN/OUT configured as 64-byte doubled-buffered bulk endpoints using the indexed endpoint window and flat FIFO at `0x20+4*n`; PIO FIFO access follows the exact 32/16/8-bit width strategy of `musb_core.c:401-464`. `x6886_usb_channel_send()` does a bounded TXPKTRDY-clear poll, FIFO write and doorbell; `x6886_usb_channel_poll()` does a bounded RXPKTRDY poll, RXCOUNT validation, FIFO drain into a 4 KB ring, AUTOCLEAR and notification; `x6886_usb_channel_receive()` drains the ring to the caller. This is the iOS debug/data channel: no ADB, no Android frameworks, no `.so`/`.ko`.
4. **Notification**: weak `x6886_usb_notify_data()` — the iOS consumer overrides it; the default only emits an `[X6886-usb]` line. No fabricated Skywalk/IOKit consumer was invented.
5. **Diagnostics**: every failure calls `x6886_usb_fail(stage, status)`, which logs stage, errno, HWVERS, indexed FIFO/CONFIGDATA, DEVCTL, POWER, SWRST, PHY DTM0/DTM1 and a sequence number. All lines go to `printf` and the shared pstore ring at `0x480af000` with the sibling drivers' header format.

## 4. Boot safety

- Opt-in only: `x6886_usb=N` must be present and non-zero; `x6886_usb_spin` is validated against 64..65536 (rejected, not clamped). `x6886_usb>2` is rejected.
- Order: pstore map → clock syscon presence → MMIO map → PHY init/readback → MUSB presence/reset → role detection → (step 2 only) endpoint setup. No MMIO access to the controller before its clocks and PHY report ready.
- Power: no regulator write. `vusb` is already always-on (`fdt.dts:3503`); `usb-otg-vbus` is under a disabled node (`fdt.dts:4919`). VBUS validity is proven from the PHY DTM1 readback and MUSB DEVCTL.
- Every loop is bounded: HWVERS 32, post-reset stable 64, PHY ready 64, TXPKTRDY `x6886_usb_spin`, RXPKTRDY `x6886_usb_spin`, FIFO copies by packet length. No `for(;;)`, no `while(1)`, no `goto`, no `panic()`.
- Every error path unmaps all windows and returns a negative errno. `x6886_usb_stop()` masks INTRUSBE/INTRTXE/INTRRXE, drops SOFTCONN and unmaps everything.
- `x6886_usb_init()` returns 0 when not opted in or on readiness-only (step 1) success.

## 5. Gaps (missing or deliberately not invented)

1. **No XNU interrupt wiring** — IRQ 160 is proven but no XNU IRQ registration API is proven in this tree; the channel is bounded polling. The constant is logged for a future owner.
2. **Clock gate offsets/bits** — `fdt.dts:7461-7462` names the three clocks but the dump has no gate register/bit. Readiness is syscon presence plus controller/PHY readback, not a guessed gate write.
3. **No regulator supply property on `usb0@11200000`** (`fdt.dts:7457-7483`) — the `vusb` and `usb-otg-vbus` nodes exist but are not wired to this node, so the driver never writes a PMIC register.
4. **`mode = <0x02>`** (`fdt.dts:7464`) is not decoded by any available source constant; it is ignored rather than guessed. `multipoint = <0x01>` (`:7465`) is likewise not implemented; the 16-endpoint count is only used as a bound.
5. **Gadget descriptors/enumeration** — this driver configures EP1 and soft-connect, but the device descriptors and UDC/gadget framework are outside the write scope. A host must enumerate the standard MUSB peripheral for bulk traffic to flow.
6. **T-PHY eye/efuse trims** — `mediatek,eye-vrt`, `eye-term`, `rev4`, host-eye settings and `intr_cal` nvmem (`fdt.dts:7498-7507,7510-7513`) are left for the PHY owner; only `rx-sqth`, `discth` and `rev6` are applied. Reading the calibration cell would touch NVRAM, forbidden by the task.
7. **DMA/QMU** — not used; PIO only. The QMU register block (`mtk_musb_reg.h:401-407`) and any DMA table are unproven for X6886.
8. **Host mode data path** — role detection refuses an A-device/host role cleanly; no host transfer code was invented.
9. **FIFOADD** — not written; the hardware FIFO allocation is retained. No per-chip FIFO table is in the dump.
10. **RXCOUNT width** — no proven bit mask in the source; the raw 16-bit value is validated against the 64-byte cap and rejected if larger.
11. **PHY reset bit** — `MUSB_SWRST_PHY_RST` (`mtk_musb_reg.h:302`) is not asserted; only the documented controller software reset is used.
12. **pstore sub-window** — `0x480af000` is supplied by the task and lies inside the proven pstore region; its offset within that region is not independently documented in the dump.
13. **Caller wiring** — `x6886_usb_init()`, `x6886_usb_stop()`, `x6886_usb_channel_send()`, `x6886_usb_channel_poll()` and `x6886_usb_channel_receive()` are defined here; declarations and call sites are outside this task's write scope.
14. **No verification run** — by task constraint there was no build, no flash, no device boot and no runtime log. This manifest is static evidence only.

No flash, no runtime test, no build was performed.
