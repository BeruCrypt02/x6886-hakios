# X6886 connectivity bring-up plan (Wi-Fi / Bluetooth / cellular / GNSS)

Date: 2026-11-26. Mode: **read-only research**. No ADB, no fastboot, no flashing, no device
access, no build, no source edits. The only file written is this document.

Status of every subsystem below: **survey-only / NOT-PORTED** on the X6886 XNU side. "Verified"
means verified in the Android dump / live FDT, **not** proven in XNU and not proven on this
device under XNU. All steps in sections 3–6 are future work; none has been performed.

## 0. Safety and forbidden actions

Nothing in this document authorises a write. The following are **FORBIDDEN without explicit
owner approval** (approval must name the exact operation and target):

- **FORBIDDEN** — any write to modem image, `md1img`/`modem` partitions, modem RAM, SMEM,
  DPMAIF descriptor memory, or modem state/registers. Includes the whole alps
  `md_cd_power_on()` / `md_cd_let_md_go()` sequence and `drv1_hw_reset_for_6789()`.
- **FORBIDDEN** — any write to NVRAM (including Wi-Fi/BT calibration, `wifi.cfg`,
  `txpowerctrl.cfg`, `BT_FW.cfg`, `libwifinvram*` data), IMEI, `protect1`, `protect2`,
  efuse, or any AT command that modifies the SIM/modem NVS.
- **FORBIDDEN** — any `fastboot flash`, `fastboot erase`, `--set-active`, GPT/super/preloader/
  lk/vbmeta/tee write, or any device test on slot A (project rule in `README.md`).
- **FORBIDDEN** — A-GPS / carrier AGPS provisioning that writes NVRAM or talks to the modem.
- **FORBIDDEN** — guessing an MT6366 VCN register, a CONNSYS offset, a modem state-word
  offset, or any other constant that is not DTB- or source-proven. The error-return path is
  the correct behaviour until such a constant is supplied by an owner.

Read-only surveys, bounded polls, MMIO reads, and firmware-file inventory are allowed.

## 1. Sources and method

| Source | What it gave |
|---|---|
| `device-dump/fdt.dts` (live FDT, overlays applied) | All bases, IRQ SPI values, pinctrl states, supplies, reserved memory |
| `device-dump/interrupts.txt`, `iomem.txt`, `modules.txt`, `dmesg.txt` | IRQ names/GIC numbers, MMIO labels, loaded modules, live Wi-Fi behaviour |
| `board/x6886-base.dts`, `board/x6886-overlays.dts` | Stock dump DT cross-check |
| `/home/berucrypt00/otadump/all_files.txt`, `vendor/firmware/`, `odm/firmware/`, `images/`, `system_dlkm/`, `vendor_dlkm/` | Exact firmware blob names/sizes, modules, `modules.load` order |
| `/home/berucrypt00/otadump/vendor/etc/init/hw/init_connectivity.rc`, `vendor/etc/init/` | Stock WMT/conmodem boot order and property gates |
| `dump-analysis/firmware.md`, `dump-analysis/kernel-modules.md` | Prior offline firmware/module inventory |
| `refs/alps-device-modules-6.12/arch/arm64/boot/dts/mediatek/mt6789.dts` | Source-side CONNSYS/Wi-Fi/GPS/mddriver nodes and register-window names |
| `refs/alps-device-modules-6.12/drivers/misc/mediatek/eccci/` | The only alps C source for modem power-on present in this tree |
| `refs/oosexgang-xnu-kernel-mtk-helio/kernel/overlay/bsd/dev/mtk_{connsys,wifi,btsk}.c` | Non-alps CONNSYS reference; NOT usable for MT6366 power-on |
| `kernel/x6886/hakios.x6886_{wifi,bt,cell}.c` and `PORT-{WIFI,BT,CELL}.md` | Current XNU stub behaviour |
| `docs/DRIVER-MATRIX.md:14`, `docs/HARDWARE.md` | Existing survey status |

Important scope limit: `refs/alps-device-modules-6.12` is a **partial** snapshot. It contains
MT6789 DTS, the ECCI/CCCI modem driver, battery/charger, GPU, UFS, audio — but **no CONNSYS /
Wi-Fi / BT / GNSS C driver source**. Strings inside the stock `.ko` files name the missing
source paths (e.g. `../vendor/mediatek/kernel_modules/connectivity/gps/gps_pwr/gpspwr.c`).
That absence is reported as a gap, never filled with invented register values.

## 2. Shared CONNSYS hardware map (DTB-verified)

From the live FDT, `/soc/consys@18002000`, compatible `mediatek,mt6789-consys`
(`device-dump/fdt.dts:6955-6968`; source `mt6789.dts:5357-5385` names each window):

| Window (source name) | Base / size | Evidence |
|---|---|---|
| CONN_MCU_CONFIG_BASE | `0x18002000` / `0x1000` | `fdt.dts:6960` reg[0] |
| TOP_RGU_BASE | `0x10007000` / `0x100` | `fdt.dts:6960` reg[1] |
| INFRACFG_AO_BASE | `0x10001000` / `0x1000` | `fdt.dts:6960` reg[2] |
| SPM_BASE | `0x10006000` / `0x1000` | `fdt.dts:6960` reg[3] |
| CONN_HIF_ON_BASE | `0x18007000` / `0x1000` | `fdt.dts:6960` reg[4] |
| CONN_TOP_MISC_OFF_BASE | `0x180b1000` / `0x1000` | reg[5] |
| CONN_MCU_CFG_ON_BASE | `0x180a3000` / `0x1000` | reg[6] |
| CONN_MCU_CIRQ_BASE | `0x180a5000` / `0x800` | reg[7] |
| CONN_TOP_MISC_ON_BASE | `0x180c1000` / `0x1000` | reg[8] |
| CONN_HIF_PDMA_BASE | `0x18004000` / `0x1000` | reg[9] |
| AP2MD_PCCIF4_BASE | `0x1024c000` / `0x40` | reg[10] |
| INFRA_AO_PERICFG_BASE | `0x10003000` / `0x1000` | reg[11] |

Interrupts (`fdt.dts:6961`): SPI `0x19a` → **GIC 442 `BTIF_WAKEUP_IRQ`**, SPI `0x19b` →
**GIC 443 `CONN_LOG_IRQ`**, SPI `0x6f` → **GIC 143, unnamed in `interrupts.txt`** (gap).
Power domain `<0x2e 1>`; PMIC phandle `0xab` (MT6366); clock `ccif` from phandle `0x2c`
index `0x38`. EMI: `emi-addr 0x7f000000`, `emi-size 0x500000`, alignment `0x1000000`,
max `0x80000000` — reserved `mblock-36-consys_emi_reserved` at `0x7f000000`/`0x500000`
no-map (`fdt.dts:513-516`; live `/proc/iomem` `7f000000-7f4fffff : WIFI-EMI`, `iomem.txt:168-169`).

Wi-Fi data window and DMA (`fdt.dts:7571-7580`; alps source `mt6789.dts:6433-6441` uses
`GIC_SPI 409`, i.e. the same SPI `0x199`):

- `/soc/wifi@18000000`, compatible `mediatek,wifi`, `reg 0x18000000/0x100000`
  (`iomem.txt:141` `18000000-180fffff : AXI-BUS`), IRQ SPI `0x199` → **GIC 441 `wlan0`**
  (`interrupts.txt:175`), `memory-region = <0x24a>` → `mblock-37` at `0xbe000000/0x600000`
  no-map (`fdt.dts:506-511`), `emi-size 0x600000`, alignment `0x1000000`, max `0xc0000000`.
- BT audio `mtk-btcvsd-snd@18050000` at `0x18050000/0x1000` + `0x18080000/0x10000`,
  IRQ SPI `0x197` → GIC 423 (`fdt.dts` around 7583-7591).

Stock boot order (read-only): `init_connectivity.rc` starts `wmt_loader` and
`wmt_launcher -p /vendor/firmware/ -o 1` at `class early_hal`; on
`vendor.connsys.driver.ready=yes`, `init.gps_drv.rc` inserts `${ro.vendor.gps.chrdev}.ko`,
and the meta/factory rc inserts `wmt_chrdev_wifi.ko` then `wlan_drv_${ro.vendor.wlan.gen}.ko`
= `wlan_drv_gen4m_6789.ko`. `persist.vendor.connsys.chipid=-1` (runtime 1d/1e auto-detect;
`BT_FW.cfg` says `# connac1 6631 setting`, so 1d/MT6631 is the likely active set).

## 3. Wi-Fi

### 3.1 DTB-verified bases / IRQs
See section 2: CONNSYS `0x18002000` (plus 11 windows), Wi-Fi CSR `0x18000000/0x100000`,
CONNSYS EMI `0x7f000000/0x500000`, Wi-Fi DMA `0xbe000000/0x600000`; IRQs `0x199` (wlan0),
`0x19a` (BTIF wake), `0x19b` (CONN log), `0x6f` (unnamed).

### 3.2 Firmware present in the dump
`vendor/firmware/`: `WIFI_RAM_CODE_soc2_2_1d_1.bin` (712,676 B),
`WIFI_RAM_CODE_soc2_2_1e_1.bin` (707,284 B), `soc2_2_ram_wifi_1d_1_hdr.bin` (241,680 B),
`soc2_2_ram_wifi_1e_1_hdr.bin` (243,556 B). ODM configs: `wifi.cfg` (293 B),
`txpowerctrl.cfg` (324 B), `WMT_SOC.cfg`. `vendor/firmware/connfem.cfg` is **0 bytes**.
Modules: `wlan_drv_gen4m_6789.ko`, `wmt_drv.ko`, `wmt_chrdev_wifi.ko`, `connadp.ko`,
`connfem.ko`. Absent: `connsys_mt6835_mt6631.bin` / `connsys_mt6835_mt6635.bin` expected by
`wmt_launcher` (`dump-analysis/firmware.md:299`).

### 3.3 Exact alps source that documents power-on
- `refs/alps-device-modules-6.12/arch/arm64/boot/dts/mediatek/mt6789.dts:5357-5385`
  names every CONNSYS register window; `:6433-6441` declares the Wi-Fi node/IRQ/EMI.
- **No alps C source for CONNSYS power-on is present in this snapshot.** The locally
  available power-on reference is **not alps**:
  `refs/oosexgang-xnu-kernel-mtk-helio/.../mtk_connsys.c` (SPM survey at `:16-26,62-75`,
  MT6358 VCN18/VCN28/VCN33 through PWRAP at `:145-245`). This phone has **MT6366**, not
  MT6358; `fdt.dts:6967` `pmic = <0xab>` and `mt6789-mt6366.ko` is loaded. No verified
  MT6366 VCN register map exists locally. Do not port those writes.

### 3.4 What the XNU stub currently does
`kernel/x6886/hakios.x6886_wifi.c` + `PORT-WIFI.md`: real gen4m logic adapted from
`mtk_wifi.c`/`mtk_connsys.c`, but **survey-only by default**.
`x6886_wifi_init()` returns immediately without `connsys_step=N` (`:750-752`); step 1
surveys SPM/MCU read-only and **returns an error without writing anything** if CONNSYS is
off (`:762-766`, because MT6366 VCN is unverified); step 3 takes driver own and sets up
WPDMA rings; step 4 downloads `WIFI_RAM_CODE_*` from `fw/connsys/x6886/` and waits on
`sw_sync0`. `connsys_chip=1d|1e` selects the blob. Four raw blobs are staged under
`kernel/x6886/`; no build wiring, no GIC handler (events polled from rings), no
scan/join/WPA layer. Diagnostics go to printf and pstore `0x480af000`.

### 3.5 Ordered steps and gate checks
1. **Freeze the map.** Record every window, EMI/DMA range and SPI from section 2 as board
   data. *Gate:* read-only WF CSR/MCU ID reads match the DTB (`CONNSYS_IP_VER_ID`
   `0x10020501` compared only, never required) and IRQ names match `interrupts.txt`.
2. **Resolve the supply path.** Obtain a verified MT6366 VCN18/28/33 register map from an
   owner or stock source. *Gate:* known PMIC registers read back expected defaults through
   the existing PWRAP helper. **FORBIDDEN** to write any VCN/power register before this gate.
   Step 2 is a hard blocker for everything below.
3. **Read-only survey.** Run step 1 (`connsys_step=1`). *Gate:* `wf_survey()` reports
   CONNSYS powered by the stock loader and the read-only reads are stable.
4. **Own the radio.** Step 3: WPDMA descriptor rings in the `0xbe000000` DMA window,
   TOP_HVR/TOP_FVR readback. *Gate:* HVR/FVR match the expected gen4m values, no TX yet.
   Owner sign-off needed for the MT6768-derived `CONN_HIF_ON_LPCTL` offsets (`PORT-WIFI.md` §7).
5. **Download firmware.** Step 4: `WIFI_RAM_CODE_soc2_2_1d_1.bin` by default (5-region
   tailer, entry `0x77000`, EMI offsets `0x2f0000`/`0x386000`); `1e` only after chip-id
   confirmation. *Gate:* `sw_sync0` ready bits set and firmware-owned state observed.
   The `_hdr.bin` patch files are staged but not used.
6. **WMT patch stage.** Needs the two absent `connsys_mt6835_mt66{31,35}.bin` patches.
   *Gate:* files supplied by an owner; otherwise BLOCKED — no fake patch stage.
7. **Interrupts / station layer.** Name SPI `0x6f` on-device and decide poll vs GIC;
   add scan/join only with a real `hakios_wpa` layer. *Gate:* named IRQ and a working
   security stack; both are currently absent.

**FORBIDDEN:** Wi-Fi NVRAM/calibration writes (`wifi.cfg`, `txpowerctrl.cfg`,
`libwifinvram*` data), and any CONNSYS power-on write until step 2 passes.

## 4. Bluetooth

### 4.1 DTB-verified bases / IRQs
BT is a CONNSYS function: same 12 windows at `0x18002000` etc. as section 2. BT-relevant
IRQs: `0x19a` → GIC 442 `BTIF_WAKEUP_IRQ` (`interrupts.txt:174`); BT audio
`0x197` at `0x18050000`/`0x18080000` (`fdt.dts` ~7583). Constants `BTIF 0x1100c000` and
`APDMA 0x10217b00` are kept from the reference but are **not DTB-verified and unused**
(`PORT-WIFI.md` §3). No standalone BT DT node exists.

### 4.2 Firmware present in the dump
`soc2_2_ram_bt_1d_blr_1_hdr.bin` (691,052 B), `soc2_2_ram_bt_1e_1_hdr.bin` (692,436 B),
`soc2_2_ram_mcu_1d_blr_1_hdr.bin` (198,012 B), `soc2_2_ram_mcu_1e_1_hdr.bin` (197,924 B);
`BT_FW.cfg` (vendor 999 B / odm 456 B). Modules: `bt_drv_connac1x.ko`, `connadp.ko`,
`wmt_drv.ko`, `mtk-btcvsd.ko`. The four raw blobs are copied to `kernel/x6886/firmware/`.

### 4.3 Exact alps source that documents power-on
- `refs/alps-device-modules-6.12/arch/arm64/boot/dts/mediatek/mt6789.dts:5357-5385` —
  CONNSYS register windows only; there is **no BT node and no BT power-on C source** in the
  alps snapshot.
- `refs/alps-device-modules-6.12/arch/arm64/boot/dts/mediatek/cust_k6886_connfem.dtsi`
  documents eFEM/BT path selection (`epa_elna`/`bypass`) but targets **k6886**, not X6886 —
  reference-only, must not be copied verbatim.
- The real power ops (`power_on`, `function_on`, `send`, `receive`) exist only in the
  non-alps `refs/oosexgang-xnu-kernel-mtk-helio/.../mtk_connsys.c:701,802,827,1931-1956`,
  blocked on the same unverified MT6366 VCN supplies as Wi-Fi. `mtk_bt_skel.c` in that tree
  is an empty skeleton (`:104-106`, `:20`). No complete real MTK BT driver is available.

### 4.4 What the XNU stub currently does
`kernel/x6886/hakios.x6886_bt.c` + `PORT-BT.md`: a real Skywalk kpipe provider
(`com.apple.nexus.hakios_bt`, `com.apple.uart.sk.bluetooth`, H4 TX/RX rings) adapted from
`mtk_btsk.c`. It is **default OFF**: `x6886_bt_init()` returns without touching hardware
unless `x6886_bt=1`, and with the flag but no registered backend returns a clean `ENXIO`
(`:331+`, `:369`). There is **no backend**: TX is dropped unless the CONNSYS owner calls
`x6886_bt_backend_register(fn)`; `x6886_bt_ready_set(true)` only reports controller
readiness. No power-on, WMT or STP implementation, no fake send stub. Bounded 200-line
log counter and 16 KB RX queue. Only the transport is real; a working controller is not
claimed.

### 4.5 Ordered steps and gate checks
1. **Gate on Wi-Fi step 2/3.** BT cannot power on until the CONNSYS VCN map is verified and
   the combo chip is up. *Gate:* Wi-Fi steps 2–3 pass; no independent power writes.
2. **Confirm chip variant.** 1d/MT6631 vs 1e/MT6635 from `persist.vendor.connsys.chipid`;
   *Gate:* owner-confirmed value; default to 1d only if confirmed by `BT_FW.cfg` evidence.
3. **Register the H4 backend.** CONNSYS owner supplies `x6886_bt_backend_register()`.
   *Gate:* non-NULL send function and `x6886_bt=1`; until then ENXIO is the correct result.
4. **Patch download.** MCU + BT blobs via WMT/STP (`soc2_2_ram_mcu_*`, `soc2_2_ram_bt_*`).
   *Gate:* `vendor.connsys.driver.ready=yes` equivalent and a WMT ack; missing
   `connsys_mt6835_mt66{31,35}.bin` patches remain a blocker.
5. **Expose to bluetoothd.** *Gate:* HCI reset + `READ_LOCAL_VERSION_INFORMATION` succeed
   over the kpipe with real controller responses; then add SCO/BT-voice via the already
   mapped `mtk-btcvsd-snd` windows.
6. **Power management.** STP sleep/wake only after steps 3–5. *Gate:* no HCI timeouts.

**FORBIDDEN:** BT address/calibration NVRAM writes and any CONNSYS power write before
step 1 passes.

## 5. Cellular (modem / CCCI)

### 5.1 DTB-verified bases / IRQs
- `dpmaif@10014000` (`fdt.dts:4159-4171`): `0x10014000`, `0x1022d000`, `0x1022c000`,
  `0x1022e000` (each `0x1000`); IRQ SPI `0xf1` → **GIC 273 `DPMAIF_AP_RX0`**
  (`interrupts.txt:133`); clock `infra-dpmaif-clk`; `dpmaif-ver 1`, `cap 0x42`.
- `ccifdriver@10209000` (`:4172-4186`): `0x10209000` + `0x1020a000` (`0x1000` each);
  IRQs `0xe2` → **GIC 258 `CCIF_AP_DATA0`**, `0xe3` → **GIC 259 `CCIF_AP_DATA1`**
  (`interrupts.txt:131-132`).
- `mddriver` (`:4188-4215`): `reg 0x0d0cf000/0x1000`; IRQs `0x6e` (edge), `0xe2`, `0xe3`;
  `mediatek,mdhif-type 6` (DPMAIF), `mediatek,ap-plat-info 0x1a85`,
  `mediatek,md-generation 0x1897` (**6295**), `mediatek,md-sub-version 1`,
  `boot-status-value 0x5443000c`, `power-flow-config 0x22`, `srclken-o1 0x200000`,
  `ccci,modem_info_v2` table at `:4189`, supplies `md-vmodem` (MT6366 `buck-vmodem`,
  phandle `0x5c`) and `md-vsram` (`0x5d`); power domain index `0`.
- `md-power-o1` (`syscon`, `0x10006000/0x1000`, phandle `0x5b`); `ccci-scp` at
  `0x1023c000`/`0x1023d000`; four `infra_dpmaif@1022c000..f000` aliases (`:5401-5419`).
- Reserved memory: AP/MD shared cached SMEM `0x8c000000/0x16a0000` (`:549-553`),
  non-cached `0x8e000000/0xe20000` (`:585-589`), `ccci_tag_mem` `0xbf5f0000/0x10000`
  (`:561-565`), `md_mem_usage` blocks at `0xd6000000`, `0xd7c00000`, `0xd8800000`,
  `0xdd200000`, DPMAIF cache/nocache pools (`:734-749`).

### 5.2 Firmware and modules present in the dump
`images/md1img.img` (55,494,608 B), `images/modem.img` (54,434,080 B), plus `dpm.img`,
`mcupm.img`, `scp.img`, `sspm.img`, `gz.img`, `tee.img` loaded by preloader/LK (not
`request_firmware`). `modules.load`: `ccci_util_lib.ko`, `ccci_auxadc.ko`,
`ccci_md_all.ko`, `ccci_ccif.ko`, `ccci_dpmaif.ko`, `ccci_fsm_scp.ko`, `ccmni.ko`,
`mtk_mdpm.ko`, `md_cooling_all.ko`, `mddp.ko`, `c2k_usb_f_via_modem.ko`.
NVRAM infrastructure exists (`vendor/bin/nvram_daemon`, `libnvram*.so`,
`vendor.mediatek.hardware.nvram-impl.so`) — **read-only inventory only, never write**.

### 5.3 Exact alps source that documents power-on
`refs/alps-device-modules-6.12/drivers/misc/mediatek/eccci/fsm/md_sys1_platform.c`:
- `:2374` `md_cd_power_on()` — the ordered sequence: (1) `md1_pmic_setting_on()` or
  `md1_dpsw_pmic_setting_on()` when `MD_DPSW_SETTING` is set; (2) `md_cd_topclkgen_on()`;
  (3) `md_cd_srcclkena_setting()`; (4) `mtk_ccci_cfg_srclken_o1_on()` (bit from
  `mediatek,srclken-o1`); bonding fix `md_cd_vmodem_ext_iso_en_control(md,1)`;
  `flight_mode_set_by_atf(md,false)`; (5) MTCMOS ON via `ccci_md_mtcmos_on()` or
  `clk_prepare_enable()`/`pm_runtime_get_sync()`; (6) `md_cd_dpsw_setting()`;
  (7) `md_pll_setting()` (`md_gen >= 6295`, gated by `MD_PLL_SETTING` bit; `set_pll_wa_flow()`).
- `:2502` `md_cd_let_md_go()` — for `md_gen == 6295` (this device, `0x1897`) it is
  `ioremap(0x20000024, 4)` + `ccci_write32(...,1)` + `md_cd_get_md_bootup_status()`;
  the SMC `MTK_SIP_KERNEL_CCCI_CONTROL / MD_POWER_CONFIG / MD_KERNEL_BOOT_UP` branch is
  only for `md_gen > 6295`. Other steps use `arm_smccc_smc(MTK_SIP_KERNEL_CCCI_CONTROL,
  MD_POWER_CONFIG, ...)` for DPSW/status queries (`:668`, `:298`, `:1323`).
- `:2694-2721` documents `power_flow_config` bits (bit0 srcclkena, bit1 srclken_o1,
  bit2 revert-sequencer) and reads `mediatek,power-flow-config` / `mediatek,srclken-o1`.
- `:2555-2563` `md_cd_plat_ptr` binds `power_on`, `power_off`, `soft_power_on/off`,
  `get_md_bootup_status`; `fsm/modem_sys1.c:185-186` calls `power_on`.
- `drivers/misc/mediatek/ccci_util/ccci_util_lib_load_img.c` — MD image header/size/platform
  verification (`:100-208`).
- `hif/ccci_dpmaif_v1_6789.{c,h}`, `hif/ccci_dpmaif_reg_v1.h`, `hif/ccif_hif_reg.h`,
  `hif/ccci_hif_ccif.c`, `hif/ccci_ringbuf.h`, `port/port_cfg.c:41` — MT6789 DPMAIF/SMEM
  rings, CCIF doorbell, `ttyC0` AT channel (these are the files the current XNU stub read).

### 5.4 What the XNU stub currently does
`kernel/x6886/hakios.x6886_cell.c` + `PORT-CELL.md`: **survey-only** control channel.
Without `cell_step=N` it logs and returns 0 (`:290-293`). With a step it maps DPMAIF/CCIF
windows with `ml_io_map` and **only reads** modem state (four EMI words + AP CCIF registers),
polls at most `100 × 1000 us`, allocates UL/DL rings in `kalloc`'d AP memory using the
`ccci_ringbuf.h` layout, and exposes `x6886_cell_at_bind/send/recv`; those AT calls return
an error until a real `/dev/ttyC0` node is bound. Only MMIO writes are `APCCIF_BUSY` /
`APCCIF_TCHNUM` doorbells, gated behind `cell_doorbell=1`. **Not translated:** modem image
loading, NVRAM/IMEI/protect access, HW reset/bus-protect writes, any SMEM write, any
workqueue/CCCI IRQ handler. No GIC handler is claimed; no CommCenter/RIL bridge exists
in `refs/` (only null XPC stubs). Diagnostics go to pstore `0x480af000`.

### 5.5 Ordered steps and gate checks
1. **Freeze the map and survey.** Record bases/IRQs/SMEM above; run `cell_step=1`.
   *Gate:* bounded poll sees a readable modem boot-status word at `0x0d0cf000`; raw values
   logged, no decode from an unproven offset.
2. **Inventory firmware read-only.** Parse `md1img.img`/`modem.img` headers in memory only.
   *Gate:* header/size/platform fields match `mediatek,ap-plat-info 0x1a85` and md-gen 6295.
   **FORBIDDEN** to load/push these images to the modem.
3. **Owner approval gate (hard blocker).** Every action below this line is **FORBIDDEN
   without explicit owner approval**: the PMIC/`srclken_o1`/MTCMOS/PLL/let-MD-go power-on
   writes, image download, any SMEM/DPMAIF descriptor write, the CCIF doorbell in
   production, and any NVRAM/IMEI/protect1/protect2 access.
4. **If approved, port power-on in alps order.** PMIC → topclkgen → srcclkena → srclken_o1 →
   MTCMOS → DPSW → PLL → `md_cd_let_md_go()` (6295 direct-MMIO branch). *Gate after each
   sub-step:* read back the corresponding status register; stop and unmap on any mismatch;
   final gate is a valid boot status from `md_cd_get_md_bootup_status()`.
5. **Transport, if approved.** SMEM DRB + DPMAIF rings and CCIF doorbell. *Gate:* link
   beats/polls succeed with bounded retries; no unbounded wait; no SMEM writes before step 4.
6. **Userspace.** Bind a real `/dev/ttyC0` char node and supply a CommCenter/RIL-capable
   bridge. *Gate:* an AT command round-trips over the bound node; until then AT returns
   an error. Such a bridge does not exist in `refs/` today — BLOCKED.

**FORBIDDEN:** modem power-on, image load, NVRAM/IMEI/protect writes, SMEM writes, and
`fastboot` device writes, all without explicit owner approval.

## 6. GNSS

### 6.1 DTB-verified bases / pinctrl
`/gps@18c00000`, compatible `mediatek,gps`, `status = "okay"` (`fdt.dts:8689-8704`;
source `mt6789.dts:6931-6938`). It has **no `reg` and no interrupt**: GNSS is a CONNSYS
function reached through WMT/STP and shares the `0x18002000` windows and CONNSYS EMI.
Declared: `emi-region = <0x1d>`, `emi-offset = 0x400000`, `emi-size = 0xfffff`,
`emi-domain-ap = 0`, `emi-domain-conn = 2` (source uses `<29>`). Pinctrl states
(`fdt.dts:8697-8703`, nodes at `:2769-2822`): `gps_default` (phandle `0x1e8`),
L1 LNA `@0` `0x1e9` pinmux `0x1900` output-low, `@1` `0x1ea` `0x1902`, `@2` `0x1eb`
`0x1900` output-high; L5 LNA `@0` `0x1ec` `0x1a00` output-low, `@1` `0x1ed` `0x1a02`,
`@2` `0x1ee` `0x1a00` output-high (pin 25 L1, pin 26 L5). **Gap:** in the decompiled
live FDT the numeric phandle `0x1d` also labels `mali@13000000` (`:5745`); treat the
GPS EMI region as unresolved until reconstructed from source DTS, not as proven Mali memory.

### 6.2 Firmware present in the dump
There is **no standalone GNSS `.bin`**. GNSS code lives in the CONNSYS MCU firmware
`soc2_2_ram_mcu_1d_blr_1_hdr.bin` / `_1e_1_hdr.bin` (strings include `GPSL5 MCUB handler`)
and the Wi-Fi MCU ROM blobs. Driver/config artifacts: `system_dlkm/lib/modules/gnss.ko`
(generic GNSS char core), `vendor_dlkm/lib/modules/gps_drv_stp.ko` (`mediatek,gps`,
`gps_emi_get_reserved_memory`, `WMT turn on/off GPS OK`, `gps_l1_lna_*`),
`vendor_dlkm/lib/modules/gps_pwr.ko`, `vendor/etc/gnss/agps_profiles_conf2.xml`,
`system/etc/gps_debug.conf`, `vendor/bin/hw/vendor.mediatek.hardware.gnss-service`,
`gps.default.so`.

### 6.3 Exact alps source that documents power-on
`refs/alps-device-modules-6.12/arch/arm64/boot/dts/mediatek/mt6789.dts:6931-6938` is the
only alps source present for GNSS in this snapshot: it documents the EMI region/offset/
domains but **no power sequence**. The exact C source paths are embedded in the stock
modules but **absent from the local alps tree**: `gps_pwr.ko` →
`../vendor/mediatek/kernel_modules/connectivity/gps/gpspwr.c` with
`conn_pwr_drv_pre_on()` / `conn_pwr_drv_post_off()`; `gps_drv_stp.ko` → GPS EMI/WMT
control (`WMT turn on GPS OK`, `gnss_pmic update`). The real power path is therefore
WMT/STP "turn on GPS" through CONNSYS plus `gps_pwr` pre-on/post-off — not translated,
documented here only from binary strings. Obtain the matching alps source before any
power work.

### 6.4 What the XNU stub currently does
**Nothing exists.** There is no GNSS/GPS stub, char device, HAL or AGPS client in
`kernel/x6886/` or the XNU reference trees. The only GPS references found are comments in
`mtk_connsys.c` (`:2`, `:879`, `:2087-2100`, GPS log region `0x24100`) and a Qualcomm
reference `mpss.c`. No XNU code claims the GPS EMI window or any LNA pinctrl state.

### 6.5 Ordered steps and gate checks
1. **Freeze the GNSS map.** Record EMI offset/size/domains and the six LNA pinctrl states.
   *Gate:* the `0x1d` phandle ambiguity is resolved against source DTS and the pinmux
   values `0x1900/0x1902/0x1a00/0x1a02` are confirmed; no writes.
2. **Confirm firmware provenance.** *Gate:* MCU blobs are confirmed as the GNSS carrier
   (build tags/strings), and the absence of a standalone GPS blob is recorded.
3. **Acquire the exact alps source** for `gpspwr.c` and the GPS WMT/STP ops from an owner.
   *Gate:* source in hand; otherwise BLOCKED — do not invent power registers.
4. **Read-only CONNSYS/WMT survey.** Reuse the Wi-Fi step 1 survey; *Gate:* CONNSYS is
   powered and WMT reports the GPS function available.
5. **GNSS char device.** Port a minimal NMEA/MTK raw device modelled on `gnss.ko`, fed by
   the WMT GPS channel. *Gate:* a known NMEA sentence is received; LNA states remain
   untouched until this passes.
6. **LNA control.** Only after step 5, switch `gps_l1_lna_*` / `gps_l5_lna_*` pinctrl states.
   *Gate:* signal quality improves with the LNA enabled; no power register writes outside
   the owner-approved WMT path.
7. **AGPS / network positioning.** **FORBIDDEN** — needs modem/NVRAM/IMEI writes and a
   network layer that does not exist on XNU. Requires explicit owner approval and a
   working cellular stack (section 5).

**FORBIDDEN:** GNSS NVRAM writes, AGPS provisioning that writes NVRAM or talks to the
modem, and any CONNSYS power write without owner approval.

## 7. Open gaps (all subsystems)

- No XNU driver, IRQ handler, firmware loader or userspace bridge for any connectivity
  subsystem is running on X6886; `docs/DRIVER-MATRIX.md:14` marks connectivity NOT-PORTED.
- MT6366 VCN supply registers are unverified; this blocks Wi-Fi/BT/GNSS power-on.
- The WMT patch blobs `connsys_mt6835_mt6631.bin` / `_mt6635.bin` are absent from the dump.
- SPI `0x6f` (CONNSYS MCU) is unnamed in `interrupts.txt`; needs on-device naming.
- The alps snapshot has no CONNSYS/GPS C source; only binary strings name those paths.
- No XNU API for driver-facing GIC handler registration; all events are polled.
- No `/dev/ttyC0` char node and no CommCenter/RIL bridge in `refs/`; cellular AT is inert.
- The GPS `emi-region` phandle `0x1d` is ambiguous in the decompiled FDT.
- `connfem.cfg` is 0 bytes; no verified FEM configuration for this board.
- All modem firmware, NVRAM and IMEI operations are out of scope until owner approval.
