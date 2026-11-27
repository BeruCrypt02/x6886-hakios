# X6886 (MT6789) remaining stub drivers — remediation plan

Scope: the four drivers called by `refs/xnu-12377-hakios/bsd/dev/board/drivers.c:181-191` that
the audit (`docs/X6886-DRIVER-AUDIT.md`) found to be scaffolds:

| Driver | Init symbol | Boot gate today | Call site |
|---|---|---|---|
| `kernel/x6886/hakios.x6886_audio.c` | `x6886_audio_init` | `x6886_audio=1` | `drivers.c:181` |
| `kernel/x6886/hakios.x6886_battery.c` | `x6886_battery_init` | `x6886_batt=1`, `x6886_batt_pmic=1` | `drivers.c:182` |
| `kernel/x6886/hakios.x6886_bt.c` | `x6886_bt_init` | `x6886_bt=1` + registered backend | `drivers.c:191` |
| `kernel/x6886/hakios.x6886_camera.c` | `x6886_camera_init` | `x6886_cam=1`, `x6886_cam_pmic=1` | `drivers.c:185` |

Constraints held for this plan and its implementation: read-only research on `otadump` (no
writes there), no change to touch/charger/mali drivers (other owners), no full build, no device
access, no flash. All new work is in `kernel/x6886/` and `docs/`.

## Evidence classes used below

- **DTB/IOMEM-verified** — the address/IRQ/compatible appears in this handset's own
  `device-dump/fdt.dts`, `device-dump/iomem.txt`, `device-dump/interrupts.txt`, `device-dump/modules.txt`,
  or a file path in `otadump/all_files.txt`. Safe to use as a physical base or device address.
- **REF-only** — the register offset/bit/sequence comes from a reference source
  (`refs/alps-device-modules-6.12/...`, `refs/oosexgang-xnu-kernel-mtk-helio/...`) and is **not**
  proven for this handset by the dump. Such values are usable only behind an explicit,
  separately named `x6886_*_ref` / `x6886_*_pmic` / `x6886_*_ral` boot-arg opt-in, and every
  write must have a bounded readback.
- **ABSENT** — named in a spec or another SKU but with no evidence in this dump. Never coded.

Common convention: every diagnostic is `X6886_LOG("[X6886-<name>] ...")`, which prints and mirrors
into the ramoops pstore at `0x480af000` (`sig +0 = 0x43474244`, `start +4`, `size +8`, `data +12`).
Every wait is bounded by a numeric spin/tick cap. Every failure is a clean errno, never a panic.
A driver must not return success unless it actually touched hardware and verified the result.

---

## 1. Audio — `hakios.x6886_audio.c`

### What the stub does now

`x6886_audio_init()` (`audio.c:309-332`) parses `x6886_audio`, maps AFE `0x11210000/0x2000`,
runs `x6886_afe_ready()` (`:202-234`: 64×100 µs settle, PDN-bit and `0xffffffff` dead-bus check),
`x6886_afe_dma_check()` (`:239-265`: read-only cursor-in-window check) and `x6886_route_dump()`
(`:296-307`: declarative DAPM names). It writes **no** register, starts no DMA, registers no IRQ,
and returns 0 with "AFE scaffold online (read-only)" (`:330`). The MT6366 routes are comments
only because the MT6358 codec register map is absent (`:267-270`).

### Real register/I2C work needed

1. Mask and clear stale AFE MCU IRQ (`AFE_IRQ_MCU_EN=0`, `AFE_IRQ_MCU_CLR=0xffffffff`) —
   AFE-local, REF offsets from `mt6789-reg.h:2291-2296`; bounded readback that EN reads 0.
2. Release AFE power/idle gating: clear PDN bits in `AUDIO_TOP_CON0`, set bit 29
   `AHB_IDLE_EN_EXT` — **REF** sequence `mt6789-afe-pcm.c:2967` (the source itself says
   `AUDIO_TOP_CON0` is CCF-controlled at `:2588`), bounded readback of the same bits.
3. Enable AFE (`AFE_DAC_CON0 bit0`) with bounded readback — **REF**
   `mt6789-afe-pcm.c:2975`; only as part of the same REF opt-in. The 24-bit port
   writes at `:2969-2974` need extra offsets and are deliberately not ported.
4. PMIC/amp work (deferred, blocked on source): MT6366/MT6358 codec register map is **ABSENT**
   from `refs/alps-device-modules-6.12/sound/soc/`; `aw87xxx_acf.bin` is a firmware blob with
   no register map. Do not invent amp registers.

### Address evidence

| Item | Value | Class | Proof |
|---|---|---|---|
| AFE MMIO | `0x11210000`, `0x2000` | DTB/IOMEM | `iomem.txt:105`, `fdt.dts:5582-5584` |
| AFE IRQ | DT SPI `0xea`=234 / GICv3 266 | DTB/IOMEM | `fdt.dts:5585`, `interrupts.txt:147` |
| topckgen / infracfg / apmixed / scpsys | `0x10000000`/`0x10001000`/`0x1000c000`/`0x10006000` | DTB | `fdt.dts:1582-1593,1629-1643,1776-1780` |
| pwrap (MT6366) | `0x10026000` | DTB | `fdt.dts:3219-3220,3228-3229` |
| speaker amps | `aw87xxx_pa@58` and `@59` on I2C; also `rt5512@5c` | DTB | `fdt.dts:5172-5192`; `all_files.txt:1056,11913` (`aw87xxx_acf.bin`, `snd-soc-aw87xxx.ko`) |
| I2C bus for amps | `i2c@1101a000` | DTB | `fdt.dts:5149` |
| vaud28 | `0x2ab980` µV = 2.8 V, always-on | DTB | `fdt.dts:3727-3734` |
| AFE register offsets/bits, `afe_clk` names, IRQ clearing | listed at `audio.c:115-161` | REF | `alps.../sound/soc/mediatek/mt6789/mt6789-reg.h:2154-2310,2884-2891,3085-3087`; `mt6789-afe-pcm.c:2585-2591,2835-2926,2953-2975,3335-3404`; `mt6789-afe-clk.c:41-63,241-330` |
| audio routes | `audio_device.xml:26-32,50-56,85-96,109-120,193-204` | DTB/userland config (declarative) | `otadump/vendor/etc/audio_device.xml`; `all_files.txt:8538` |

### alps source giving the sequence

`refs/alps-device-modules-6.12/sound/soc/mediatek/mt6789/mt6789-afe-pcm.c:2953-2975` (power-on:
regcache sync, bit29, 24-bit ports, AFE enable) and `:2835-2926` (IRQ STATUS/EN/CLR handler);
`mt6789-afe-clk.c:241-330` (clock enable order, CCF-only, NOT portable to XNU);
`mt6789-mt6366.c:27-49,116-122` (machine-driver kctl map); `mt6789-reg.h` (registers).

### Boot arg gates

- `x6886_audio=1` — existing top-level gate.
- `x6886_audio_ref=1` — **new** REF-only gate for steps 1–3 above. Without it the driver
  returns `ENOTSUP` and logs that no AFE register was written (no false success).

### Dump evidence for mt6366 / aw87xxx_pa (exact)

Present: `mt6366` PMIC node `fdt.dts:3228-3229`, `mediatek,mt6789-mt6366-sound` at `:7627`,
`mt6789-mt6366.ko` and `snd-soc-mt6366.ko` at `all_files.txt:11839,11916`, AFE at
`iomem.txt:105`/`fdt.dts:5582`, two `awinic,aw87xxx_pa` nodes at `fdt.dts:5172-5184`,
`aw87xxx_acf.bin` at `all_files.txt:1056`, `snd-soc-aw87xxx.ko` at `:11913`, and the
`audio_device.xml` kctl map. Absent: any MT6358/MT6366 codec register map and any aw87xxx
register programming sequence. `rt5512@5c` also exists (`fdt.dts:5187-5192`) — the stub's
single amp comment at `audio.c:110` is incomplete, not wrong.

### Ordered fix list (file:line targets)

1. `audio.c:110-113` — add `aw87xxx_pa@58/@59` DTB constants and note `rt5512@5c` separately.
2. `audio.c:98-168` — add REF-only register constants (already present) plus a comment block
   naming the alps source for each one.
3. `audio.c:170-234` — keep the bounded ready gate unchanged; it is the verified-tier probe.
4. **new after `audio.c:265`** — `x6886_afe_program_ref()`: IRQ mask/clear + readback,
   PDN clear/bit29 set + readback, DAC enable + readback; 64×100 µs caps.
5. `audio.c:309-332` — parse `x6886_audio_ref`; return the result of step 4 when enabled,
   otherwise log "read-only, no register written" and return `ENOTSUP`.

---

## 2. Battery — `hakios.x6886_battery.c`

### What the stub does now

`batt_thread()` (`:156-214`) reads PMIC registers **only when** `x6886_batt_pmic=1` through
`pmic_read()` (`:137-154`: WACS2 CMD/RDATA/VLDCLR, 200000-spin FSM waits): `RTC_AL_MTH` for
Android's percent, `AUXADC_ADC0` for voltage, `CHR_CON0` for charger detect, then publishes
`hakios_battery_report()` with `current_ua=0`, temperature fixed 25 °C and a bounded tick count.
`x6886_battery_init()` (`:216-252`) returns 0 with "PMIC reads skipped" when the opt-in is off —
a success return with no hardware work.

### Real register/I2C work needed

1. Keep the read-side gauge path (already real hardware work when opt-in is on).
2. Latch the FGADC once to obtain current and coulomb count — **REF**, transcribed from
   `mtk_battery.c:164-194`: read `FGADC_CON1=0xd0a`, write `READ` bit0, bounded wait for bit15
   `LATCHED`, read `FGADC_CUR_CON0=0xd8a` and `FGADC_CAR_CON0/1=0xd14/0xd16`, then always
   write `CLEAR` bit3 and restore the saved value, each with bounded readback. Publish signed
   current from `0xd8a` using `FG_COUNT_NA=762939` nA/count (5 mOhm sense).
3. Optionally request a fresh BATADC conversion (`AUXADC_RQST0=0x1108` bit0, then wait for
   ADC0 bit15) — **REF** `mtk_battery.c:138-157`; keep it in the same opt-in.
4. Charger chip work (SC8548 primary / SC8950 secondary) is a **separate owner** (`x6886_charger`
   driver); this file must not claim it. Only the DTB-verified charger-detect bit in the PMIC
   is read here.

### Address evidence

| Item | Value | Class | Proof |
|---|---|---|---|
| pwrap | `0x10026000`, `0x1000` | DTB | `fdt.dts:3219-3220` |
| gauge node, `r-fg-value=0x32` (50 mOhm), `fg-meter-resistance=0x64`, channel `pmic_battery_voltage` | mt6366/mt6358-gauge | DTB | `fdt.dts:3868-3887`, `:3874-3875` |
| cell limit `0x10fe` = 4350 mV | — | DTB | `fdt.dts:8378,8488` |
| WACS2 cmd/rdata/vldclr/FSM | `0xc20/0xc24/0xc28`, states 0/6 | REF | `mtk_battery.c:42-46,82-109` |
| `RTC_AL_MTH 0x05aa`, `CHR_CON0 0x0a88`, `AUXADC_ADC0 0x1088` | — | **REF-only — not in the dump** | `mtk_battery.c:48-60`; explicitly noted at `battery.c:18-22` |
| `FGADC_CON1 0xd0a`, `CUR_CON0 0xd8a`, `CAR_CON0/1 0xd14/0xd16`, `AUXADC_RQST0 0x1108` | — | **REF-only** | `mtk_battery.c:47,51-54,60`; independently corroborated by `alps.../drivers/power/supply/mt6358-gauge.c:51,66,69,140,169` |
| SC8548 primary charger | `sc,sc8548-standalone` at I2C `7-006f`, 5 mOhm sense | DTB + module | `fdt.dts:5245-5256`; `all_files.txt:2180,11374` (`tc_sc8548_charger.ko`); not owned here |
| SC8950 secondary | `southchip,sc8950` | DTB + module | `fdt.dts:5125-5133`; `all_files.txt:2181` |

### alps source giving the sequence

`refs/alps-device-modules-6.12/drivers/power/supply/mt6358-gauge.c` (FGADC/AUXADC register
definitions at `:42-146,169-171`) and `drivers/power/supply/transsion/tc_gauge.c` (`gauge_get_*`
property path, no register writes). The latch *sequence itself* is only in the osexgang REF
`mtk_battery.c:138-194`. MT6366 chip-specific register programming is **ABSENT** from alps;
`mt6358-gauge.c` is an MT6358 map and is used as corroboration, not as device truth.

### Boot arg gates

- `x6886_batt=1` — top-level.
- `x6886_batt_pmic=1` — existing REF gate for all PMIC reads.
- `x6886_batt_fg=1` — **new** REF gate for the FGADC latch / BATADC request writes.
- `x6886_batt_ticks=N` — existing bounded poll cap (`<=0x00ffffff`).

### Dump evidence for mt6366 gauge / sc8548 (exact)

Present: `mediatek,mt6358-gauge` under the `mt6366` PMIC (`fdt.dts:3872`), its channel names and
resistance properties (`:3874-3887`), `tc_gauge.ko` / `mt6358_battery.ko` / `fuelgauged`
(`all_files.txt:2166,2146,8347`), and the SC8548 node plus `tc_sc8548_charger.ko`
(`fdt.dts:5245-5256`; `all_files.txt:2180,11374`). Absent: PMIC register numbers in the DTB, an
MT6366-specific gauge register map, and any SC8548 register programming sequence in the allowed
refs. The DTB proves the *device*, not the offsets.

### Ordered fix list (file:line targets)

1. `battery.c:18-24,99-116` — mark every PMIC offset as REF and cite `mtk_battery.c` + alps
   `mt6358-gauge.c`.
2. `battery.c:136-154` — add `pmic_write()` with the same WACS2 FSM states and the same
   200000-spin bound as `pmic_read()`.
3. **new after `battery.c:154`** — `gauge_latch_ref()` (`mtk_battery.c:164-194`) and
   `batadc_request_ref()` (`:138-157`), both bounded and with mandatory restore.
4. `battery.c:156-214` — when `x6886_batt_fg=1`, call step 3 and publish latched current/charge;
   otherwise keep `current_ua=0` and log that the latch was not enabled.
5. `battery.c:216-252` — without `x6886_batt_pmic` return `ENOTSUP`, not 0.

---

## 3. Bluetooth — `hakios.x6886_bt.c`

### What the stub does now

The file is a verbatim adaptation of the real Skywalk H4 kernel-pipe provider in
`mtk_btsk.c`: TX (`:200-244`) hands H4 bytes to `x6886_bt_send_backend`, RX (`:247-317`)
queues controller bytes into a bounded 16 KB ring and notifies the channel. With `x6886_bt=1`
it registers the domain provider/controller/instance (`:330-415`). It performs **no** CONNSYS
or BTIF hardware access: `x6886_bt_init()` returns `ENXIO` until a CONNSYS owner registers a
backend (`:368-371`), and `x6886_bt_ready_set()` only flips a gate (`:431-438`).

### Real register/I2C work needed

1. Map and probe the two **DTB-verified** blocks owned by BT: CONNSYS MCU window
   `0x18002000/0x1000` and BTIF `0x1100c000/0x1000`, with bounded reads until a non-dead
   value is seen (64×100 µs). This is the minimum honest hardware work before claiming the
   controller path exists.
2. Read the CONNSYS hardware/firmware ID registers at MCU `+0x00/+0x04` and, **REF-only**,
   `CONSYS_IP_VER +0x10` / `CONSYS_CONF_ID +0x1c` (`mtk_connsys.c:69-75`). Do not hardcode the
   X676C expected ID `0x10020501` as X6886 truth; log what is read.
3. Only then register the Skywalk provider; TX remains tied to the registered backend. Power-on,
   WMT function-on, STP full mode and firmware download stay with the CONNSYS owner
   (`mtk_connsys.c:274,701,802,827,1931-1956`) — outside the allowed sources and owned by the
   Wi-Fi/CONNSYS agent. Do not duplicate them here.

### Address evidence

| Item | Value | Class | Proof |
|---|---|---|---|
| CONNSYS MCU window | `0x18002000`, `0x1000` (first of 11 windows) | DTB | `fdt.dts:6955-6970`; `iomem.txt` |
| BTIF | `0x1100c000`, `0x1000`, IRQ 0xbe/0xb8/0xb9 | DTB | `fdt.dts:4477-4483`, `:9182` |
| BT PMIC/antenna supplies | CONNSYS power domain `<0x2e 0x01>`, EMI `0x7f000000/0x500000` | DTB | `fdt.dts:6960-6966` |
| `bt_drv_connac1x.ko`, `btif_drv.ko`, `wmt_drv.ko` | modules | DTB/userland | `all_files.txt:11764,11765,11945`; `modules.txt:1-4,112,166-167` |
| BT firmware `soc2_2_ram_bt_1d_blr_1_hdr.bin`, `_1e_`, `soc2_2_ram_mcu_*` | blobs | dump present | `all_files.txt:9514-9517`; copied to `kernel/x6886/firmware/` |
| `CONSYS_HW_ID +0x00`, `CONSYS_FW_ID +0x04`, `CONSYS_IP_VER 0x10`, `CONSYS_CONF_ID 0x1c` | offsets | REF | `mtk_connsys.c:15,69-75` |
| H4 send / power-on / WMT / STP ops | — | REF, other owner | `mtk_connsys.c:274,701,802,827,1931-1956`; `mtk_bt_skel.c:20,104-106` |

### alps source giving the sequence

**None in the allowed alps snapshot**: `grep -ril connac1x|bt_drv|wmt_drv` over
`refs/alps-device-modules-6.12` returns no source file (only unrelated DTS `btif` nodes). The
transport sequence is from `refs/oosexgang-.../bsd/dev/mtk_btsk.c:123-215,250-325`; the
controller-side register conventions are REF-only in `mtk_connsys.c`. No alps file gives a
CONNSYS power-on sequence, which is why power-on stays with the CONNSYS owner.

### Boot arg gates

- `x6886_bt=1` — existing transport gate.
- `x6886_bt_connsys=1` — **new** REF/verified hardware-probe gate; without it the driver
  returns `ENOTSUP` (not a false success).
- Backend registration (`x6886_bt_backend_register`) and readiness
  (`x6886_bt_ready_set`) remain separate runtime gates (`:418-438`).

### Dump evidence for bt_drv_connac1x (exact)

Present: `vendor_dlkm_b/lib/modules/bt_drv_connac1x.ko` (`all_files.txt:11764`), `btif_drv.ko`
(`:11765`), `wmt_drv.ko` (`:11945`), `init.bt_drv.rc` (`:8799`), `init.wmt_drv.rc` (`:8830`),
the four RAM firmware blobs (`:9514-9517`), DTB `consys@18002000` (`:6955`) and `btif@1100c000`
(`:4477`). Absent: driver source, symbol table, register map, and any proof that the X676C
`CONSYS_IP_VER_ID 0x10020501` applies to this handset.

### Ordered fix list (file:line targets)

1. `bt.c:32-39` — add DTB-verified CONNSYS/BTIF base+size constants and REF offset constants.
2. **new after `bt.c:95`** — `x6886_bt_hw_probe_ref()`: map both windows, bounded non-dead
   readback of MCU ID and REF IP/CONF registers, unmap on all paths, return 0/ENXIO/EBUSY.
3. `bt.c:330-371` — parse `x6886_bt_connsys`; run step 2 before any nexus registration; return
   `ENOTSUP` when disabled, propagate probe errors, keep the `ENXIO` no-backend gate.
4. `bt.c:418-438` — unchanged API; document that owner-supplied backend/ready are still required.

---

## 4. Camera — `hakios.x6886_camera.c`

### What the stub does now

`x6886_cam_probe()` (`:190-251`) maps three MMIO windows (`:97-105`), checks pwrap FSM idle
(`:147-163`), optionally reads two rail registers with `x6886_cam_pmic=1` (`:214-228`), logs
the two sensor identities (`:230-232`), reads one GPIO word (`:236-242`) and unmaps everything.
No sensor register, no SENINF/CAMSV setup, no stream, no capture. The rail offsets are REF-only
and never written. `x6886_camera_init()` returns the probe result, i.e. 0 for "inspect-only".

### Real register/I2C work needed

1. Keep the bounded window/pwrap/GPIO reads as the verified tier.
2. Turn the `x6886_cam_pmic=1` rail path into a real, reversible register operation:
   write bit0 of `CAM_VCAMIO_EN 0x1cb0` and `CAM_VCAMD_EN 0x1c9c` (**REF**,
   `mtk_camera.c:192-203`) through WACS2, bounded readback that each bit holds (64×100 µs),
   then restore the saved values before unmapping, again with bounded readback. This is real
   register work with a clean exit, not a declarative log.
3. Sensor-ID read over the DTB-verified I2C2/I2C4 windows (`0x11eb0000/0x11eb1000`) and
   DTB-verified addresses `0x2a`/`0x1a` is the *next* step, but the I2C controller programming
   sequence and the sensor ID registers are **ABSENT** from both alps and osexgang
   (`mtk_camera.c` bit-bangs GPIO105/106 for a different sensor). Do not invent them here.

### Address evidence

| Item | Value | Class | Proof |
|---|---|---|---|
| camsys / seninf1..6 / seninf_top / camsv2..7 | `0x1a000000`, `0x1a004000..0x1a009000`, `0x1a092000..0x1a097000` | DTB | `fdt.dts:1841,3165-3196,7087-7136`; `iomem.txt` |
| topckgen / infracfg / gpio / pwrap | `0x10000000`/`0x10001000`/`0x10005000`/`0x10026000` | DTB | `fdt.dts:2354-2361,2376,3219-3220` |
| I2C2 / I2C4 | `0x11eb0000` / `0x11eb1000` | DTB | `fdt.dts:45,47,4668,4827` |
| rear `imx882_mipi_raw` | camera0, `camera_main@2a`, addr `0x2a` | DTB/userland | `fdt.dts:1912,4690-4697`; `all_files.txt:2046,1265+` |
| front `gc13a0ff_mipi_raw` | camera1, `camera_sub@1a`, addr `0x1a` | DTB/userland | `fdt.dts:1913,4848-4856`; `all_files.txt:2044-2045,1174+` |
| MIPI TX | `mipi_tx_config@11f60000` | DTB | `fdt.dts:6515-6520` |
| MT6366 VCAMIO/VCAMD/VCAMA1 | regulator nodes | DTB | `fdt.dts:3507-3517,9106-9107,9121` |
| `CAM_VCAMIO_EN 0x1cb0`, `CAM_VCAMD_EN 0x1c9c` | rail offsets | **REF-only** | `mtk_camera.c:192-193`; absent from the dump (noted at `camera.c:117-118`) |
| pwrap WACS2 `0xc20/0xc24/0xc28`, FSM 0/6 | — | REF | `mtk_camera.c:125-190` |
| sensor register tables / SENINF/CAMSV values | — | ABSENT | `mtk_camera.c:51-57` `__has_include` fails; no `mtk_camera_jn1.h`/`mtk_camera_rx.h` |

### alps source giving the sequence

`refs/alps-device-modules-6.12/arch/arm64/boot/dts/mediatek/cust_mt6789_camera.dtsi` and
`mt6366_mt6789.dtsi` only supply DTS wiring; a full-tree search for `gc32e1`, `gc32e1x6850`,
`gc13a0` and `imx882` in alps returns **no source file**. There is no alps sensor driver or
register table for this handset. The rail-enable and pwrap sequence is REF-only in
`refs/oosexgang-.../bsd/dev/mtk_camera.c:125-190,192-263`.

### Boot arg gates

- `x6886_cam=1` — top-level.
- `x6886_cam_pmic=1` — existing REF gate for rail reads (now: read-modify-write with restore).
- `x6886_cam_rails=1` — **new** REF gate for actually driving the rail enables; without it,
  the driver returns `ENOTSUP` rather than "inspect-only success".

### Dump evidence for gc32e1 / gc32e1x6850_mipi_raw (exact)

- `gc32e1x6850_mipi_raw_*` — **present but HAL/tuning only**:
  `vendor_b/lib64/mt6789/gc32e1x6850_mipi_raw_IdxMgr.so`,
  `..._tuning.so` (`all_files.txt:10693-10694`) and 20
  `libCamera_gc32e1x6850mipiraw_*.so` feature libraries (`:10719-10740`); likewise
  `gc32e1x6853_*` (`:10695-10696,10739-10760`). No kernel `.ko`, no DTB node.
- `gc32e1frontmipiraw` — present as 158 `odm_b/lib64/libCamera_gc32e1frontmipiraw_*.so`
  entries (`all_files.txt:1248-1249, ...`), again userland HAL only.
- **Fitted sensors are different**: DTB selects rear `imx882` and front `gc13a0ff`
  (`fdt.dts:1912-1913,4690-4697,4848-4856`). `X6886-specs.txt:118-119` lists
  `gc32e1x6850`/`gc32e1x6853` as *other-board SKUs*; `docs/X6886-DRIVER-AUDIT.md:144-146`
  records that the spec's "Supported" line is not the fitted part.
- Conclusion: gc32e1/gc32e1x6850 evidence is **HAL residue for a different SKU**, not a
  device-proven sensor. It cannot justify a kernel register sequence. The stub is correct to
  report `imx882`/`gc13a0ff`; the fix must not swap them for gc32e1 from the spec sheet.

### Ordered fix list (file:line targets)

0. `camera.c:97-105` — **verified-tier bug found during implementation**: the old
   `0x10000000/0x20000` window ends at `0x1001ffff` but pwrap is at `0x10026000`, so
   every `cam_reg(pwrap + ...)` returned NULL and the old PMIC path could never answer.
   A dedicated DTB-proven `0x10026000/0x1000` window is now mapped.
1. `camera.c:117-125` — mark rail offsets REF and cite `mtk_camera.c:192-203`.
2. `camera.c:165-188` — add `pwrap_write()` with the same FSM 0/6 bound as `pwrap_read()`.
3. **new after `camera.c:188`** — `cam_rail_ref(reg, on)`: save, write bit0, bounded readback,
   and a matching restore with bounded readback.
4. `camera.c:190-251` — with `x6886_cam_rails=1`, enable both rails and restore them before
   `out:`; propagate failure as `EIO`. Keep `x6886_cam_pmic=1` read-only otherwise.
5. `camera.c:253-272` — without `x6886_cam_rails`, return `ENOTSUP` instead of "inspect-only
   success"; log that no rail/write work was done.

---

## Verification (no build, no device)

- `git diff --check` on all four files: no whitespace errors.
- Balanced `{`/`}` counts and terminating NUL/newline for each C file; no new `__has_include`
  or missing-symbol references.
- Confirm no edits outside `kernel/x6886/{audio,battery,bt,camera}.c` and this document, and
  that `otadump` is unmodified (`git -C /home/berucrypt00/otadump status --porcelain` empty).
- Every new write path has a numeric spin cap and a readback; every new boot arg defaults OFF.
