# UFS controller state at LK handoff — Infinix X6886 / MT6789

Scope: read-only investigation. No device access, no port-tools, no writes to the dump.
Sources: `/home/berucrypt00/otadump` (stock dump), `/home/berucrypt00/hakios-x6886`
(symlink to `/home/berucrypt00/x6886-hakios`), public U-Boot source webfetched for
comparison only. Offsets below are **hex file offsets produced by `strings -a -t x`**
unless a DTS line number is given.

---

## 0. Verdict

**There is no eMMC contract to copy. The equivalent UFS contract is: LK/preloader have
already powered, clocked, reset-deasserted, HCE-enabled, link-started and enumerated the
UFS device, and left the HCI with UTRL/UTMRL descriptor tables programmed in physical
DRAM. A second stage may submit UTRDs against that state — but only after READING the
live registers to prove the state still holds, and after reading the link/block geometry
from the device. It must never re-assert resets, never issue the MediaTek SiP UFS SMC
calls, never toggle HCE, never redo link startup or power-mode change.**

Confidence split:

- **VERIFIED (local, static/dump evidence):** LK contains a full MTK UFS stack; LK's
  boot device is `soc/11270000.ufshci`; both MMC controllers are disabled; the exact
  FDT node; U-Boot's probe order and the destructive steps in it; preloader's UFS init.
- **INFERENCE (strong, but not provable from a binary):** the *order* of LK's calls,
  that HCE/link/PHY are still on at the instant our loader starts, that LK's UTRLBA
  holds a physical address, that no UTRD is in flight, that auto-hibernate is off.
- **UNKNOWN / must be checked read-only on device:** HCE bit, link/HIBERN8 state,
  UTRL contents, LUN map and logical block size, AH8 setting, whether LK called
  `ufs_hba_stop()` before jumping.

Because the last group is not knowable from the dump, the read path in §6 is written
as **gated**: every assumption is a register/response read with an explicit abort.

---

## 1. Evidence inventory

### 1.1 `lk.img` — LK has a complete MediaTek UFS host stack (VERIFIED)

File: `/home/berucrypt00/otadump/images/lk.img`, 3,149,992 bytes. Raw MTK image
(`8816 8858 787c 0e00 6c6b ...`, "lk" at offset 0x08), not ELF; no symbol table.
`strings` total = 25,364 lines; `grep -aiE` UFS-family matches = 738 lines, repeated
in three rodata regions (`0xa2xxx`, `0x182xxx`, `0x24fxxx`) — same strings linked
three times, not three different behaviours.

Embedded source paths (exact strings + offsets):

| Offset | String |
|---:|---|
| `0xaef62`, `0x18d9cf`, `0x258c77` | `platform/mediatek/common/ufs/ufs_interface.c` |
| `0xb76f0`, `0x195376`, `0x25f1b3` | `platform/mediatek/common/ufs/ufs_hcd.c` |
| `0xba9d2`, `0x198457`, `0x2617ef` | `platform/mediatek/common/ufs/ufs_core.c` |

Boot/init/geometry (exact strings + offsets):

| Offset | String |
|---:|---|
| `0xc2b91` | `ufs_init()` |
| `0xa8291` | `ufs_set_bootdevice` |
| `0xa82bd` | `[UFS] err: ufshcd_probe_hba failed` |
| `0xaedef` | `[UFS] err: Host controller enable failed` |
| `0xc0fd9` | `[UFS] info: UFS%u init OK` |
| `0xbed9a` | `[UFS] UFS version %x.%x` |
| `0xaa7c2` | `[UFS] DME_LinkStartup(0x%x) = 0x%x` |
| `0xbfea3` | `[UFS] DebugLinkStartup(0x%x) = 0x%x` |
| `0xc24b6` | `[UFS] DME_LayerEnable(0x%x) = 0x%x` |
| `0xaa7e6` | `[UFS] PowerState(0x%x) = 0x%x` |
| `0xb4338` | `[UFS] Power mode change to HSG1 FASTAUTO: ret=%d` |
| `0xbcb28` | `Failed setting power mode, err = %d` |
| `0xa3cea` | `Failed getting max supported power mode` |
| `0xb0f7e` | `[UFS] info: HS-G%d-%d, %d lane` |
| `0xafe8e` / `0xb8790` | `[UFS] info: RX HS-G%d, setting is %d` / `[UFS] info: TX HS-G%d, setting is %d` |
| `0xb9892` | `link startup fail, retrying the %d times ...` |
| `0xc23f4` | `[UFS] err: retry number exceeded (reinit host)` |
| `0xb3233` | `[UFS] warn: Power on, reset, or bus device reset occupied` |
| `0xbcb4d`, `0xbdab0` | `%s: err: get PA_HIBERN8TIME fail!` / `%s: err: peer get PA_HIBERN8TIME fail!` |

I/O + descriptors + doorbell (exact strings + offsets):

| Offset | String |
|---:|---|
| `0xa2ba5` / `0xc36bb` | `ufs_block_read` / `ufs_block_write` |
| `0xbb94f` / `0xa8258` | `ufs_scsi_cmd_read10` / `ufs_scsi_cmd_write10` |
| `0xafe7e` / `0xbfe6a` / `0xb97df` | `ufs_get_lu_size` / `ufs_get_device_info` / `ufs_check_lu_cfg` |
| `0xafeb4` / `0xbb9a2` | `ufshcd_host_memory_configure` / `hba->utrdl_base_addr` |
| `0xc49b8` | `[UFS] %d ms timedout waiting for doorbell to clear (tr=0x%x)` |
| `0xb77f7`-area | `[UFS] get lu size: lun=%d, blk_cnt=%d, blk_size_in_byte=%d` (`0xb773f`) |
| `0xa722b` | `[UFS] boot_base: 0x%lx` |
| `0xbed9a`-area | `[UFS] %s: Device Desc:` (`0xc0f91`) |

Handoff strings (exact + offsets):

| Offset | String |
|---:|---|
| `0xa3c64` | `androidboot.boot_devices=bootdevice,soc/%08lx.ufshci,%08lx.ufshci` |
| `0xb0f4d` | `gpt=1 loop.max_part=7 ufshcd_core.poll_queues=0 ` |
| `0xa84be` | `%s:disable mmc0 when ufs boot` |
| `0xbf439` / `0xbfe61` | `UFS_BOOT` / `atag,ufs` |
| `0xc49f6` | `platform_fdt_set_ufs` |
| `0xb5414` | `ufs_hba_stop` |
| `0xc36cb` | `ufs_poweroff_notify` |

DT paths in LK: `0x2d0484` `ufshci@11270000`, `0x2d04a0` `mediatek,mt8183-ufshci`,
`0x2e2250` `/soc/ufshci@11270000`, `0x2e8a3c` `ufshci`.

**What this proves:** LK is not a thin storage shim. It contains the full
`ufs_hcd`/`ufs_core`/`ufs_interface` stack, does HCE enable, DME link startup, layer
enable, power-mode change, descriptor reads, LU enumeration, GPT read, block
read/write, and appends UFS boot-device command line + FDT atag before jumping to the
next stage. The *call order* is reconstruction from API names, not provable from a
string pool (rodata is a linker pool, not source order) — order is INFERENCE.

**"UFFS hba_stop" caveat (INFERENCE):** `ufs_hba_stop` and `ufs_poweroff_notify` exist
in LK. Their rodata neighbours give no call-site ordering. They are most likely on
shutdown/reboot/poweroff paths, because LK must hand a *usable* boot device to the
kernel, but this is **not** locally provable. Gate on HCE at runtime (§6 step 0).

### 1.2 `preloader_ufs.img` — the real PHY/link init happens before LK (VERIFIED)

File: `/home/berucrypt00/otadump/images/preloader_ufs.img`, 387,056 bytes.
Offset 0x00 is literally `UFS_BOOT`. Selected strings (hex offsets):

| Offset | String |
|---:|---|
| `0x4b4fe` | `ufshcd_init` |
| `0x4b524` | `[UFS] DME_LinkStartup(0x%x) = 0x%x` |
| `0x50478` | `[UFS] Power mode change to HSG1 FASTAUTO: ret=%d` |
| `0x4f471` | `[UFS] info: ufshcd_init HOST%d OK` |
| `0x4f44b` / `0x4da06` | `UFSHCI:` / `UFS UTRD:` |
| `0x4f494` | `[UFS] info: legacy IP version - 0x%x, is legacy : %d` |
| `0x4775a` | `ufs_aio_hba_stop` |
| `0x49aed` / `0x4c730` | `ufs_aio_block_read` / `ufs_aio_block_write` |
| `0x47fb2` | `ufs_aio_get_lu_size` |
| `0x55515` / `0x50c0a` | `ufs_aio_poweroff_notify` / `[UFS] notify power-off success` |
| `0x52c9d` | `link startup fail, retrying the %d times ...` |

Consequence: by the time LK starts, UFS has *already* been brought up once (preloader
loads LK from UFS). LK re-probes/reuses it, then loads the boot image. A second stage
is the **third** consumer, not the first. This is why "LK left it ready" is reasonable
— but still needs the runtime gates.

### 1.3 Live FDT — exact controller binding (VERIFIED)

`/home/berucrypt00/hakios-x6886/device-dump/fdt.dts`, lines 5660–5674 (identical content
in `board/x6886-base.dts` lines 4428–4442):

```dts
		ufshci@11270000 {
			compatible = "mediatek,mt8183-ufshci";
			reg = <0x00 0x11270000 0x00 0x2300>;
			interrupts = <0x00 0x89 0x04 0x00>;
			clocks = <0x2c 0x2f 0x2c 0x2b 0x2c 0x2a 0x2c 0x29 0x2c 0x30 0x2b 0x27>;
			clock-names = "ufs\0ufs_mp\0unipro_tick\0unipro_sys\0ufs_aes\0ufs_ck";
			freq-table-hz = <0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00>;
			vcc-supply = <0x7e>;
			resets = <0x7b 0x01 0x7b 0x02 0x7b 0x03>;
			reset-names = "hci_rst\0unipro_rst\0crypto_rst";
			bootmode = <0x46>;
			mediatek,ufs-qos;
			mediatek,ufs-disable-mcq;
			phandle = <0x18a>;
		};
```

Reading:

- MMIO base **0x11270000**, span **0x2300**. Standard UFSHCI registers are the low
  0xA0; MediaTek extended registers at 0x140 (`XOUFS_CTRL`), 0x144 (`REFCLK_CTRL`),
  0x160 (`MMIO_OPT_CTRL_0`), 0x2100 (`EXTREG`), 0x2200 (`MPHYCTRL`), 0x2240 (`IP_VER`).
- IRQ `0x89` = **137** (SPI), flags `0x04`.
- 6 clocks: `ufs`, `ufs_mp`, `unipro_tick`, `unipro_sys`, `ufs_aes`, `ufs_ck`.
- `vcc-supply = <0x7e>` = `ldo-vemc` / regulator `vemc`, always-on (fdt.dts line 3650).
- Resets: `hci_rst`, `unipro_rst`, `crypto_rst` (indices 1/2/3 of controller `0x7b`).
- `bootmode = <0x46>` (70). Meaning is **UNKNOWN from local evidence** (no decoder in
  the modules' strings). Do not depend on it.
- `mediatek,ufs-disable-mcq` → vendor firmware uses the **legacy UTRL path**, not MCQ.
  That is the path a minimal driver must use too.
- **No `phys` phandle and no `ufsphy@` node.** The field-tested MT6789 U-Boot fork
  explicitly comments `/* MT6789 doesn't have mphy */` in its `ufs_mtk_bind_mphy()`.
  A minimal driver should not try to drive a separate PHY.

Handoff properties in the same FDT:

- line 75: `atag,ufs = <0x1000000 0x1000000>;` (two 16 MiB values — likely boot-region
  geometry from preloader/LK; INFERENCE, do not rely on it).
- line 92 (`chosen/bootargs`): contains `gpt=1 ... ufshcd_core.poll_queues=0`.
- line 9231: `ufshci = "/soc/ufshci@11270000";` (alias).

No separate U-Boot DT binding document was found at the guessed path (HTTP 404); the
authoritative binding comparison available is the driver's `of_match` table:

```
{ .compatible = "mediatek,mt6878-ufshci" },
{ .compatible = "mediatek,mt8183-ufshci" },   <-- exact match to X6886
{ .compatible = "mediatek,mt8195-ufshci" },
```

Source: `https://raw.githubusercontent.com/u-boot/u-boot/master/drivers/ufs/ufs-mediatek.c`
(webfetched this session). **The X6886 node binds to mainline U-Boot's MediaTek UFS
driver with no compatible-string change (VERIFIED).**

### 1.4 Live Android properties — LK selected UFS as boot device (VERIFIED)

`/home/berucrypt00/x6886-hakios/device-dump/getprop.txt` line 1922:

```
[ro.boot.boot_devices]: [bootdevice, soc/11270000.ufshci, 11270000.ufshci]
```

That is the runtime form of LK's `androidboot.boot_devices=...` string in §1.1.

`/home/berucrypt00/x6886-hakios/device-dump/fdt.dts`:

- `mmc@11230000` (MSDC0/eMMC) has `status = "disabled";`
- `mmc@11240000` (MSDC1/SD) has `status = "disabled";`

`/home/berucrypt00/x6886-hakios/device-dump/cmdline.txt` contains `gpt=1` and
`ufshcd_core.poll_queues=0`.

`device-dump/dmesg.txt` contains **no UFS lines** (kernel log excerpt is unrelated) —
no runtime HCI version/LUN data available there.

**Conclusion (VERIFIED):** this board boots *only* from UFS. LK had to initialize UFS
to load `boot.img`, and the second stage is packed into that boot image.

### 1.5 Kernel modules — what the next consumer does (VERIFIED)

Under `/home/berucrypt00/otadump/vendor_boot/ramdisk/lib/modules/`:

`ufs-mediatek-mod.ko`, `ufs-mediatek-dbg.ko`, `phy-mtk-ufs.ko`,
`tran-ufshcd-core.ko`, `tran-ufshcd-pltfrm.ko`.

Strings in `ufs-mediatek-mod.ko`: `bootmode`, `mediatek,ufs-disable-ah8`,
`ufs_mtk_auto_hibern8_disable`, `ufshcd_uic_hibern8_exit`,
`%s: ufshcd_wait_for_doorbell_clr timeout!`, `mediatek,ufs-disable-mcq`,
`ufs_mtk_pwr_change_notify`, `ufs_mtk_bind_mphy`.

**Important:** the live FDT does **not** contain `mediatek,ufs-disable-ah8`. The kernel
driver can therefore leave AH8 enabled. Nothing in LK's strings shows an AH8 disable
either — LK only reads `PA_HIBERN8TIME`. So "link is awake at handoff" is INFERENCE,
not verified, and gets an explicit gate in §6.

No per-device UFS calibration/firmware file exists in the dump: the only `*ufs*` files
are `images/preloader_ufs.img` and the kernel modules listed above. Calibration is
negotiated at link startup (LK strings: `unipro_params: pa_h8time=0x%x`,
`pa_tactivate=0x%x`, `peer_granularity=0x%x`, `ufshcd_quirk_tune_host_pa_tactivate`),
not loaded from a file. **Nothing to copy, nothing to write.**

### 1.6 mtkloader contract — what we are replacing (VERIFIED)

`/home/berucrypt00/x6886-hakios/refs/oosexgang-xnu27/loader/msdc.h`, lines 5–7:

```
 * LK has already powered the card, set the bus up and read partitions of its own by the time we
 * run, so this does not initialise anything: it drives MSDC0 (0x11230000) in PIO mode and issues
 * plain block reads (CMD17/CMD18) to the card LK left in the transfer state. Registers follow
 * Linux's drivers/mmc/host/mtk-sd.c.
```

`docs/porting.md` lines 123–126 (MTK variant:
`refs/hakios-mtk/docs/mtk-porting.md` lines 103–106):

```
2. **The loader's own log.** Build `mtkloader` with `-DMSDC_TEST=1` ...
3. **The flash.** `tools/msdc-test.sh` (`-DMSDC_TEST=1`): the loader reads the GPT through `msdc.h`, in PIO mode, with the card as LK left it:
   nothing is initialised. *Check:* `eMMC read test` in the log with your partition names. On a UFS phone this step is a driver you write
   (`bsd/dev/redwood_ufs.c` is the Qualcomm phone's and is a place to start reading, not a fit).
```

`mtkloader.c` `MSDC_TEST` self-test (lines 2498–2689) proves the pattern: log what is
about to happen, arm a watchdog, read only known sectors (GPT header at LBA 1, `boot_a`
at LBA `0xf5000`), dump bytes, and never write.

`refs/hakios-mtk/docs/mtk-porting.md` line 82: "**UFS phones need another reader**".
There is no existing UFS contract in these refs — this report defines it.

---

## 2. What LK does at boot regarding UFS (reconstructed)

INFERENCE on ordering (string pool cannot prove call order); each item's *existence* is
VERIFIED from §1.1.

1. **Preloader (before LK):** clock/power/reset deassert, MPHY init, `ufshcd_init`,
   `DME_LinkStartup`, power-mode change to HS-G1 FASTAUTO, LU check, read GPT/LK from
   UFS. VERIFIED from `preloader_ufs.img` strings (`0x4b4fe`, `0x4b524`, `0x50478`,
   `0x4f471`).
2. **LK start:** parse `/soc/ufshci@11270000`, call `platform_fdt_set_ufs()` and
   `ufs_set_bootdevice()`; strings `0xc49f6`, `0xa8291`, `0x2d0484`, `0x2d04a0`.
3. **Controller init:** `ufshcd_probe_hba()` / `ufshcd_hba_enable()` — HCE enable, error
   strings `0xa82bd`, `0xaedef`.
4. **Link:** `DME_LinkStartup()` with retries, `DME_LayerEnable()`, `PowerState()`,
   power-mode change to HS-G1 FASTAUTO; strings `0xaa7c2`, `0xc24b6`, `0xaa7e6`,
   `0xb4338`, `0xb9892`. Unipro parameters logged (`granularity`, `pa_h8time`,
   `pa_tactivate`, peer values).
5. **Enumeration:** device/config/geometry/RPMB/unit descriptors, `ufs_get_lu_size()`,
   `ufs_check_lu_cfg()` — strings `0xc0f91`, `0xaee77`, `0xb773f`, `0xafe7e`, `0xb97df`.
   This is where the **real logical block size** becomes known (LK logs
   `blk_size_in_byte=%d`).
6. **Boot I/O:** GPT read + `boot.img` load through `ufs_block_read()` /
   `ufs_scsi_cmd_read10()` (strings `0xa2ba5`, `0xbb94f`). `ufshcd_host_memory_configure`
   (`0xafeb4`) has already pointed UTRL/UTMRL at DRAM tables; the doorbell-timeout
   string (`0xc49b8`) shows LK waits for completion synchronously.
7. **Handoff:** append `androidboot.boot_devices=...,soc/11270000.ufshci,...` and
   `gpt=1 ... ufshcd_core.poll_queues=0`, set `atag,ufs`, jump to the payload.
   Strings `0xa3c64`, `0xb0f4d`, `0xbfe61`; live proof in `getprop.txt:1922`.
   **`ufs_hba_stop` / `ufs_poweroff_notify` are not provably on this path** — see §1.1
   caveat. The kernel re-probes from scratch, so LK need not leave a pristine HCI, but
   a second stage that wants to skip init must check.

**Net state we expect at our entry (INFERENCE, gated in §6):** clocks on, vemc rail on,
resets deasserted, MPHY/link up, HCE = 1, UTRL/UTMRL run/stop = 1 and base registers
pointing at LK's synchronous descriptor tables in physical DRAM, no requests in flight,
LUN 0 (boot LU) enumerated.

---

## 3. What U-Boot's driver does — and what we may skip

Source webfetched this session:
`https://raw.githubusercontent.com/u-boot/u-boot/master/drivers/ufs/ufs-mediatek.c`,
`ufs-mediatek-sip.h`, `ufs-mediatek.h`, `drivers/ufs/ufs-uclass.c`, `drivers/ufs/ufshci.h`
(and the field-tested MT6789 fork `mnnoee/u-boot-mtk-g99`, branch `mt6789-stable`,
`drivers/ufs/ufs-mediatek.c`).

Exact order in `ufshcd_probe()` (`ufs-uclass.c:2201`):

1. map DT MMIO base, set default descriptor sizes.
2. `ufshcd_ops_init()` → vendor `ufs_mtk_init()`:
   `ufs_mtk_advertise_quirks()`, `ufs_mtk_init_reset()` (gets `hci_rst`/`unipro_rst`/
   `crypto_rst` handles), `clk_get_bulk()` + **`clk_enable_bulk()`**,
   `generic_phy_power_on()`, **`ufs_mtk_setup_ref_clk(hba, true)`** (SMC
   `MTK_SIP_UFS_CONTROL` = `0x276`, `UFS_MTK_SIP_REF_CLK_NOTIFICATION = BIT(3)`),
   read IP version at vendor offset `0x2240`.
3. read `REG_CONTROLLER_CAPABILITIES` (0x00), `REG_UFS_VERSION` (0x08).
4. allocate descriptor memory; fill first UTRD's UCD base / response offset / PRDT
   offset (`ufshcd_host_memory_configure`).
5. clear `REG_INTERRUPT_STATUS` (0x20), write 0 to `REG_INTERRUPT_ENABLE` (0x24).
6. **`ufshcd_vops_device_reset()`** → mainline `ufs_mtk_device_reset()` issues SMC
   `0x276` with `UFS_MTK_SIP_DEVICE_RESET = BIT(1)`, low 13 µs, high, then 13 ms delay.
   The MT6789 fork additionally calls `arm_smccc_smc(MTK_SIP_BL_UFS_CONTROL /* 0x8200040D */,
   1<<1, ...)` in `ufs_mtk_pre_link()` "to run in non-secure world ... reset the UFS
   controller".
7. **`ufshcd_hba_enable()`** (`ufs-uclass.c:596`): if HCE is not active it first calls
   `ufshcd_hba_stop()` (writes 0 to 0x34); then PRE_CHANGE hook writes `REG_UFS_XOUFS_CTRL
   |= 0x80`, sets HCE, waits, enables UIC interrupts.
8. `ufs_start()` (`ufs-uclass.c:2151`): `ufshcd_link_startup()` →
   `ufs_mtk_pre_link()` (DME writes), `DME_LINKSTARTUP`, link checks, `POST_CHANGE`
   clock gating, `ufshcd_make_hba_operational()` (**writes UTRLBA @0x50/0x54,
   UTMRLBA @0x70/0x74, then UTRLRSR @0x60 / UTMRLRSR @0x80**); then
   `ufshcd_verify_dev_init`, `ufshcd_complete_dev_init`, device descriptor,
   `ufshcd_get_max_pwr_mode`, `ufshcd_change_power_mode` (HS-G gear change).

### Skip / mandatory table

| U-Boot step | For our read-only path | Reason |
|---|---|---|
| clock bulk enable | **SKIP** (gate: MMIO readable, clocks evidently on) | LK/preloader enabled them; re-enabling is harmless but unnecessary. No register write needed. |
| reset deassert / reset handling | **SKIP — NEVER assert** | `hci_rst`/`unipro_rst` would erase the live link state. |
| `generic_phy_power_on` | **SKIP** | No `phys` property in our FDT; fork says MT6789 has no mphy. |
| refclk SMC (`0x276`, BIT(3)) | **SKIP — do not call SMC at all** | Changes ATF/secure-world refclk ownership and writes `REG_UFS_REFCLK_CTRL` (0x144). |
| device reset SMC (`0x276` BIT(1), or `0x8200040D` BIT(1)) | **SKIP — most destructive step in the driver** | Resets the device, drops link, invalidates everything LK left. |
| `hce_enable_notify` PRE_CHANGE (`0x140 |= 0x80`), HCE toggle | **SKIP** | Only do this if HCE = 0 — which means the read-only path is impossible anyway; abort instead. |
| UIC interrupt masking | **SKIP** (we poll, no IRQ handler) | Writing 0x20/0x24 is not destructive but is unnecessary. |
| `link_startup_notify` pre/post DME writes, `DME_LINKSTARTUP` | **SKIP if link is up** | Destroys/renegotiates the link. Mandatory only if link is down — at which point we are doing a full driver, not a minimal reader. |
| `ufshcd_make_hba_operational` (write UTRLBA/UTMRLBA, run/stop) | **PREFERABLY SKIP** — read and reuse LK's tables. Fallback: program our own tables (U-Boot proves this is safe while HCE is on) only if LK's base pointer is unusable. | Avoids touching controller configuration. |
| device descriptor / LU discovery, power-mode change | **SPLIT**: read-only descriptor/SCSI inquiry is MANDATORY; power-mode change is **SKIP**. | We need block size and LUN; gear changes are pointless and risky. |
| UTRD build + doorbell poll | **MANDATORY** | This is the actual read. |

Note for the U-Boot path generally (not our driver): mainline `ufs_mtk_init()` calls
`generic_phy_power_on()` unconditionally after a `bind_mphy()` that returns 0 on
`-ENOENT`; the MT6789 fork patches this to handle "no mphy". Do not blindly reuse
mainline U-Boot on this DT.

---

## 4. MSDC contract vs UFS contract

| | eMMC / MSDC (existing, VERIFIED) | UFS (this report) |
|---|---|---|
| What LK leaves | card in **transfer state**, RCA assigned, bus width/timing set | HCI enabled, link up, descriptor rings programmed, device enumerated |
| Second stage issues | CMD17 / CMD18 in PIO | one UTRD ( SCSI READ(10)/(16) UPIU + PRDT), ring `UTRLDBR`, poll OCS + doorbell clear |
| Geometry | 512-byte blocks, LBA from GPT known (`0xf5000` for `boot_a`) | **block size must be read** (UFS devices commonly use 4096); LUN map must be read |
| Registers touched | `MSDC_CFG`, `SDC_CMD`, `SDC_ARG`, `SDC_INT`, FIFO only | standard UFSHCI 0x00–0xA0 only; never vendor 0x140/0x144/0x2200 |
| Reset/init | none | none — same rule, but more ways to break it (SMC!) |
| Failure mode | command timeout | HCE=0, link down/HIBERN8, wrong UTRD/PRDT layout, wrong block size, stale doorbell |

**Can we submit a UTRD directly?** **Yes in principle, conditionally in practice.**
Direct submission is valid only if all four gates pass: HCE=1, UTRLRSR=1, UTRLDBR
clear, link active. Unlike eMMC's "transfer state", UFS has a *wearable* link layer:
after idle the device may enter HIBERN8, in which case a UTRD will time out until a
DME hibern8-exit is issued. This is the single biggest difference from the MSDC case
and is an UNKNOWN on our device (§1.5).

---

## 5. Register map used by the read path (from U-Boot `drivers/ufs/ufshci.h`)

All offsets relative to `0x11270000`:

| Offset | Name | Use |
|---:|---|---|
| `0x00` | `REG_CONTROLLER_CAPABILITIES` | read only (queue depth, 64-bit addr support) |
| `0x08` | `REG_UFS_VERSION` | read only (pick UTRD format expectations) |
| `0x18` | `REG_AUTO_HIBERNATE_IDLE_TIMER` | read only — AH8 clue |
| `0x20` / `0x24` | `REG_INTERRUPT_STATUS` / `ENABLE` | read; W1C only our own bit if needed |
| `0x30` | `REG_CONTROLLER_STATUS` | UCRDY/UTMRLDY/UTRLRDY (U-Boot `ufshcd_get_lists_status`) |
| `0x34` | `REG_CONTROLLER_ENABLE` | **HCE bit — gate, never write** |
| `0x50` / `0x54` | `REG_UTP_TRANSFER_REQ_LIST_BASE_L/_H` | read LK's UTRL base |
| `0x58` | `REG_UTP_TRANSFER_REQ_DOOR_BELL` | read in-flight slots; write our slot to submit |
| `0x5C` | `REG_UTP_TRANSFER_REQ_LIST_CLEAR` | avoid unless recovering |
| `0x60` | `REG_UTP_TRANSFER_REQ_LIST_RUN_STOP` | gate (UTRLRSR) |
| `0x70` / `0x74` | `REG_UTP_TASK_REQ_LIST_BASE_L/_H` | read only (task ring, unused) |
| `0x80` | `REG_UTP_TASK_REQ_LIST_RUN_STOP` | gate |
| `0x90`–`0x9C` | `REG_UIC_COMMAND` + ARG_1..3 | read-only DME_GET liveness probe / hibern8 exit |
| `0x2240` | `REG_UFS_MTK_IP_VER` (vendor) | read only, IP version |

UTRD layout (`ufshci.h:419`, 8 dwords = 32 bytes):
DW0–3 `request_desc_header` (contains OCS after completion), DW4–5 UCD base address
(lo/hi), DW6 `response_upiu_length | response_upiu_offset`, DW7 `prd_table_length |
prd_table_offset`. LK's `ufshcd_host_memory_configure` filled these offsets — read the
first UTRD at UTRLBA to learn the exact layout LK used before writing any of our own.

---

## 6. Exact order of operations for a read-only UFS path

No code. Each step has a gate; "abort" means fall back to the full U-Boot/LK-style
driver (or stop) — never guess past a failed gate.

**Step 0 — log first, touch nothing.** Print MMIO identity and the six gate registers:
`REG_UFS_VERSION` (0x08), `REG_CONTROLLER_STATUS` (0x30), HCE (0x34 bit0), UTRLBA
(0x50/0x54), UTRLDBR (0x58), UTRLRSR (0x60). This mirrors `msdc_self_test()`'s
"print before anything that can hang" rule. All reads.

**Step 1 — HCE gate.** If `0x34 bit0 == 0`: **abort the read-only path.** LK did not
leave the controller enabled (or called `ufs_hba_stop`). Do not set HCE yourself as a
shortcut; enabling HCE starts the controller init sequence and requires the whole probe.

**Step 2 — ring gate.** Require UTRLRSR (0x60 bit0) = 1 and UTMRLRSR (0x80 bit0) = 1,
and UCRDY/UTRLRDY/UTMRLDY in `0x30` set. Require UTRLBA (0x50/0x54 combined) to be a
nonzero, 1 KiB-aligned physical address inside known DRAM. If any fails: **abort**.

**Step 3 — no in-flight work.** Require UTRLDBR (0x58) == 0. If nonzero, LK has a
request outstanding; do not disturb it — abort (or wait with a timeout and re-check,
but never clear someone else's doorbell bit).

**Step 4 — liveness / link gate.** Issue a harmless **UIC DME_GET** through `REG_UIC_COMMAND`
(0x90) + ARG_1..3 (0x94–0x9C), e.g. a PA-layer attribute such as `PA_LOCAL_VER_NUMBER`
or `PA_GRANULARITY` (LK itself reads `PA_GRANULARITY`, string `0xb546d`-area). Poll for
UIC command completion and check UIC error. Success ⇒ link is up.
If it fails: issue a **DME hibern8-exit** (the only intentional state change in this
design) and retry once. If still failing: **abort** — there is no read-only path through
a down link. Do **not** run `DME_LINKSTARTUP`.

**Step 5 — learn LK's descriptor layout.** Read the first UTRD at UTRLBA: DW4/5 = UCD
base, DW6 = response UPIU offset/length, DW7 = PRDT offset/length. If these are sane
(nonzero, inside DRAM), **reuse LK's ring** — this is the minimal-intervention option:
no writes to any controller base/run-stop register. If they are not sane, the fallback
is to allocate our own UTRL/UCD/PRDT memory and program `0x50/0x54` + `0x60` (exactly as
`ufshcd_make_hba_operational()` does) — documented safe while HCE is on, but it is a
configuration write, so log it explicitly.

**Step 6 — discover geometry read-only.** Using a UTRD with a SCSI **INQUIRY** then
**READ CAPACITY(10/16)** on candidate LUN 0 (and the boot LUN if different), read vendor
string, logical block count and **logical block size in bytes**. Do not assume 512 —
LK explicitly logs `blk_size_in_byte=%d` (`0xb773f`). This single round trip also proves
the whole UTRD→PRDT→completion path works before any bulk read.

**Step 7 — locate the GPT.** Read LBA 1 of the boot LUN through the same path and check
for `EFI PART` at offset 0, exactly as `msdc_self_test()` checks the eMMC GPT header.
Compare the partition names with what LK used (`androidboot.boot_devices`, `gpt=1`).
This validates LUN choice and byte order before reading the real payload.

**Step 8 — bulk reads.** For each range: one UTRD at a time (LK used synchronous
completion; keep one request in flight), SCSI READ(10)/(16) UPIU with LBA + count,
PRDT pointing at the physical destination buffer, request slot's doorbell bit in
`0x58`, poll that UTRD's OCS to success **and** the doorbell bit clear (LK's own
timeout string `0xc49b8` warns about this), then check the response UPIU status/sense
before consuming data. If D-cache is on with the MMU off, clean/invalidate the buffer
around the transfer; with caches off (mtkloader's normal mode) nothing is needed.

**Step 9 — leave it as found.** Do not write HCE (0x34), any reset, `0x144`, `0x140`,
`0x2240`, `0x60`/`0x80`, or the SMC. Clear only the interrupt bit(s) we actually
observed in `0x20` (W1C), and only the doorbell bits we set. No `ufs_hba_stop`, no
`ufs_poweroff_notify`. The Android kernel will re-probe and reinitialize normally
after we hand back.

**Never, in this path:** ARM SMC `0x8200040D` (fork's BL UFS control / device reset),
SMC `0x276` with BIT(1) or BIT(3), any `reset_assert` of `hci_rst`/`unipro_rst`/
`crypto_rst`, `CLK` enable/disable, `DME_LINKSTARTUP`, `ufshcd_change_power_mode`,
RPMB key/write-protect operations (LK has `rpmb_ufs_set_wp`, `ufs_set_write_protect` —
those are write paths and must stay untouched).

---

## 7. Claim register

| # | Claim | Status | Evidence |
|---:|---|---|---|
| 1 | LK contains full MTK UFS host stack | **VERIFIED** | `ufs_hcd.c`/`ufs_core.c`/`ufs_interface.c` strings + offsets §1.1 |
| 2 | LK calls `ufshcd_probe_hba`, HCE enable, link startup, power-mode change | **VERIFIED (existence) / INFERENCE (order)** | strings `0xa82bd`, `0xaedef`, `0xaa7c2`, `0xb4338` |
| 3 | This board boots from UFS, not eMMC | **VERIFIED** | `getprop.txt:1922`, both `mmc@` nodes `status="disabled"`, `UFS_BOOT` in preloader |
| 4 | Preloader already did PHY/link init before LK | **VERIFIED** | `preloader_ufs.img` strings `0x4b4fe`, `0x4b524`, `0x50478`, `0x4f471` |
| 5 | Controller at 0x11270000, size 0x2300, IRQ 137 | **VERIFIED** | FDT lines 5660–5674 |
| 6 | UTRL/UTMRL rings are left programmed by LK | **INFERENCE** | `ufshcd_host_memory_configure`/`hba->utrdl_base_addr` strings + LK's synchronous I/O; gate §6 steps 2–3 |
| 7 | HCE=1 at our entry | **INFERENCE** | LK must hand off a bootable device; `ufs_hba_stop` exists but call site unknown; gate §6 step 1 |
| 8 | Link is active (no HIBERN8) at our entry | **UNKNOWN** | no AH8-disable in LK strings, no `mediatek,ufs-disable-ah8` in FDT; gate §6 step 4 |
| 9 | LK's UTRLBA holds a physical (MMU-off) address | **INFERENCE** | LK/MTK boot runs flat physical; gate §6 step 2 address sanity |
| 10 | Logical block size is 4096-like, not 512 | **UNKNOWN** | LK logs `blk_size_in_byte`; must be read per §6 step 6 |
| 11 | Mainline U-Boot driver binds this DT without changes | **VERIFIED** | of_match `mediatek,mt8183-ufshci`; webfetch |
| 12 | U-Boot device-refresh SMC calls are destructive here | **VERIFIED (behaviour) / INFERENCE (effect on our live state)** | `0x276` BIT(1)/BIT(3), fork `0x8200040D` BIT(1) |
| 13 | No UFS calibration blob exists in the dump | **VERIFIED** | file search; only `preloader_ufs.img` + modules |
| 14 | `bootmode = <0x46>` semantics | **UNKNOWN** | no local decoder; do not use |

---

## 8. Open checks that require the device (read-only, small, safe)

All are register/response reads or log observations; none writes:

1. Dump HCE (0x34), controller status (0x30), UTRLBA (0x50/0x54), UTRLDBR (0x58),
   UTRLRSR (0x60), AH8 idle timer (0x18), UFS version (0x08), MTK IP ver (0x2240) as
   the very first lines of a `UFS_TEST` diagnostic, same style as `MSDC_TEST`.
2. Read the first UTRD at UTRLBA and print DW4–DW7 to learn LK's UCD/PRDT offsets.
3. DME_GET `PA_LOCAL_VER_NUMBER` / `PA_GRANULARITY` to test link liveness; if it fails,
   try one hibern8-exit and report — do not loop.
4. SCSI INQUIRY + READ CAPACITY on LUN 0; print block size and count.
5. Read LBA 1 of the boot LUN, print 16 bytes and check `EFI PART`.

Only after all five checks print sane values should the bulk-read path be trusted. No
flash, no vendor write, no SMC call.

---

## 9. Bottom line for the driver author

- Treat LK as the **owner** of UFS state, exactly as `msdc.h` treats it for eMMC.
- The minimal driver is a **UTRD submitter**, not an initializer: ~10 MMIO reads, one
  liveness probe, one geometry query, then ordinary READ commands against LK's ring.
- The three things that would "break the phone" — and which the eMMC driver never had
  to worry about — are the **SiP SMC calls**, **reset deassert/assert**, and
  **HCE/link startup**. Exclude all three.
- Everything we cannot prove from the dump becomes a **gate with an abort**, not an
  assumption. That is the only safe analogue of "the card is in transfer state".

No files in `/home/berucrypt00/otadump` were modified. No device commands were issued.
