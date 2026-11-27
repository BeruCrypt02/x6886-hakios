# UFS support assessment — U-Boot-on-boot_b chainload (Infinix X6886 / MT6789)

Scope: READ-ONLY analysis of `/home/berucrypt00/otadump`, the live FDT dumps, and public
upstream sources. No device access, no writes to the phone, no code changes.

---

## VERDICT (top line)

**UFS is NOT a blocker for the U-Boot chainload plan.**

Two independent paths exist, and both are VERIFIED from evidence:

1. **LK already brings UFS up** before any chainloaded payload runs. LK loads `boot.img`
   from UFS, so a U-Boot binary packaged as a `boot.img` in `boot_b` can be loaded and
   entered by LK without U-Boot having any UFS driver at all.
2. **U-Boot has a working MT6789 UFS driver**, both in public upstream master
   (`drivers/ufs/ufs-mediatek.c`, Accepted Oct 2025) and in a field-tested MT6789 fork
   (`mnnoee/u-boot-mtk-g99`) that reports UFS mass-storage/fastboot working on an
   Infinix MT6789 device. Its compatible string is `mediatek,mt8183-ufshci` — **the exact
   string in the X6886 FDT**.
3. **If the payload is delivered over USB (`fastboot boot`), U-Boot needs no UFS driver
   for that step.** The dependency moves from "UFS driver in U-Boot" to "USB gadget
   driver in U-Boot", which the same MT6789 fork already has working.

What remains is integration risk (LK→U-Boot handoff state, secure-world SMC ownership
of the UFS controller, DTB selection), not absence of a UFS stack. Details below.

---

## 1. UFS modules and firmware in the dump — VERIFIED

All paths under `/home/berucrypt00/otadump`. Sizes from `stat`.

| Exact path | Size (bytes) | Identity (modinfo) |
|---|---:|---|
| `images/preloader_ufs.img` | 387,056 | Preloader built for UFS boot; contains `platform/common/storage/ufs/ufs_aio_*.c`, `ufs_aio_block_read/write`, `ufs_aio_get_lu_size`, RPMB/BOOT-LU write-protect handling |
| `vendor_boot/ramdisk/lib/modules/ufs-mediatek-mod.ko` | 244,472 | `name=ufs_mediatek_mod`, "MediaTek UFS Host Driver", authors Stanley Chu / Peter Wang (MediaTek). Vermagic `6.12.38-android16-5-gcc51d883045d-4k ... aarch64` |
| `vendor_boot/ramdisk/lib/modules/ufs-mediatek-dbg.ko` | 93,248 | `name=ufs_mediatek_dbg`, "MediaTek UFS Debugging Facility" |
| `vendor_boot/ramdisk/lib/modules/phy-mtk-ufs.ko` | 20,960 | `name=phy_mtk_ufs`, "MediaTek MPHY"; only DT alias present: `mediatek,mt8183-ufsphy` |
| `vendor_boot/ramdisk/lib/modules/tran-ufshcd-core.ko` | 563,336 | `name=tran_ufshcd_core`, "Generic UFS host controller driver Core" (Transsion-rebranded) |
| `vendor_boot/ramdisk/lib/modules/tran-ufshcd-pltfrm.ko` | 25,984 | `name=tran_ufshcd_pltfrm`, "UFS host controller Platform bus based glue driver" |
| `vendor_boot/ramdisk/lib/modules/tran_scsi_mod.ko` | 434,472 | `name=tran_scsi_mod`, "SCSI core" (Transsion-rebranded) |
| `vendor_boot/ramdisk/lib/modules/cqhci.ko` | 46,928 | Command Queue Host Controller Interface driver |

`ufs-mediatek-mod.ko` `depends=` (verbatim from modinfo):
`tran-ufshcd-pltfrm,ufs-mediatek-dbg,tran-proc-bootdevice-mod,tran-ufshcd-core,irq-dbg,thd,transsion_tranlog,tran_scsi_mod,blocktag,rpmb`

Note: several dependencies are Transsion-specific (`transsion_tranlog`,
`tran-proc-bootdevice-mod`). This only matters if one tried to reuse the **kernel**
module; the U-Boot driver is independent and does not need these.

### 1.1 Details extracted from `ufs-mediatek-mod.ko` strings — VERIFIED

- **Controller register base**: no hardcoded base address in the module strings —
  the base comes from DT (see §2: `0x11270000`, size `0x2300`). The module *does* contain
  register-offset debug labels (verbatim):
  `UFSHCI (0x0):`, `UFSHCI (0x100): `, `UFSHCI (0x170): `, `UFSHCI (0x300): `,
  `UFSHCI (0x380): `, `UFSHCI (0x22F0): `, `UFSHCI (0x2800): `, `Ext Reg (0x2100): `,
  `MPHY Ctrl (0x2200): `, `MMIO_RSV_CTRL (0x2308): `, `Debug Probe (0x22C8): `.
  (INFERENCE: the MPHY is inside the same MMIO block — there is no separate `phys`
  phandle in the DT; offsets ≥0x2300 likely belong to an extended/alternate mapping.)
- **Clock names**: the module exposes scaling via `clkscale_control`,
  `clk-scale-up-vcore-min`, `ufs_mtk_init_host_clk`, `ufs_mtk_setup_ref_clk`,
  `"ufs: clk (%s) scaled %s @ %lld"`; the actual names are the DT ones in §2:
  `ufs`, `ufs_mp`, `unipro_tick`, `unipro_sys`, `ufs_aes`, `ufs_ck`.
- **UFS HCI / IP version**: no fixed version constant in strings. The driver reads it
  at runtime: symbols `ufs_mtk_get_ufs_hci_version`, `ufs_mtk_get_hw_ip_version`, and
  log string `legacy IP version - 0x%x, is legacy : %d`. LK similarly logs
  `[UFS] UFS version %x.%x` (runtime value). So the HCI revision is device-read, not
  hardcoded. INFERENCE: do not assume a specific HCI revision when porting.
- **Initialization / recovery sequences visible as symbols**: `ufs_mtk_bind_mphy`,
  `ufs_mtk_mphy_power_on`, `ufs_mtk_eh_unipro_set_lpm`, `ufs_mtk_pwr_change_notify`,
  `ufs_mtk_vreg_fix_vcc`, `ufshcd_hba_enable`, `ufshcd_make_hba_operational`,
  `ufshcd_init_host_params`, `ufshcd_config_pwr_mode`, `ufshcd_link_recovery`.
  Reset control path: `ufs0:ufs_reset_opt_set ... (1:NORMAL_RESET;2:SPO_RESET)`.
- **Calibration/tuning**: no per-device calibration blob or table found in the modules.
  The strings show runtime DME attribute reads: `unipro_params: granularity=0x%x`,
  `pa_h8time=0x%x`, `pa_tactivate=0x%x`, `peer_pa_h8time=0x%x`, plus
  `ufshcd_dme_configure_adapt` / `ufshcd_quirk_tune_host_pa_tactivate` in LK.
  Calibration is negotiated at link startup, not loaded from a file in this dump.

---

## 2. UFS controller node in the live FDT — VERIFIED

Both `/home/berucrypt00/hakios-x6886/devices/x6886/evidence/fdt.dts` (lines 5660–5674) and
`/home/berucrypt00/hakios-x6886/devices/x6886/prebuilt/device-tree.dts` (lines 4428–4442) contain the
**same node, byte-for-byte in content**:

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

Reading of the fields:

- **reg**: MMIO base `0x11270000`, length `0x2300` (8,960 bytes).
- **interrupt**: `0x89` = 137 (GIC SPI), flags `0x04`.
- **clocks**: 6 entries from two clock controllers (`0x2c` ×5, `0x2b` ×1): indices
  `0x2f, 0x2b, 0x2a, 0x29, 0x30, 0x27` mapped by `clock-names` to
  `ufs`, `ufs_mp`, `unipro_tick`, `unipro_sys`, `ufs_aes`, `ufs_ck`.
- **supply**: `vcc-supply = <0x7e>` resolves to node `ldo-vemc` (fdt.dts line 3650):
  `regulator-name = "vemc"`, min `0x2c4020`, max `0x325aa0`, `regulator-always-on`.
  (The same phandle is reused by `mmc@11230000` as `vmmc-supply` — shared VEMC rail.)
- **resets**: reset controller `0x7b`, indices 1/2/3 → `hci_rst`, `unipro_rst`,
  `crypto_rst`.
- **bootmode = 0x46** (70). boolean props: `mediatek,ufs-qos`,
  `mediatek,ufs-disable-mcq` (MCQ disabled in stock — matches the LK string
  `mediatek,ufs-disable-mcq` and the module's MCQ-related code).
- **No `phys = <...>` / PHY phandle**, and no `ufsphy@` node found in either DTS.
  The MPHY is therefore accessed inside/alongside the controller by the vendor driver
  (`phy-mtk-ufs.ko`, compatible `mediatek,mt8183-ufsphy` is present as a module but not
  referenced by a DT node in these dumps).

Related LK→kernel handoff property in the same FDT (line 75):
`atag,ufs = <0x1000000 0x1000000>;` (two 16 MiB values). INFERENCE: UFS Boot-LU /
boot-region geometry handed from LK/preloader; do not overwrite.

---

## 3. Does stock LK have a UFS driver? — VERIFIED: YES, full stack

LK binary: `/home/berucrypt00/otadump/images/lk.img`, 3,149,992 bytes (`file` says
generic `data` — raw/uncompressed payload, strings readable).

`strings` counts: `UFS` 361, `ufs` 152, `stor` 114, `scsi` 6. Direct evidence:

- Source paths embedded in LK:
  `platform/mediatek/common/ufs/ufs_core.c`,
  `platform/mediatek/common/ufs/ufs_hcd.c`,
  `platform/mediatek/common/ufs/ufs_interface.c`.
- DT binding consumed by LK: `mediatek,mt8183-ufshci`, `/soc/ufshci@11270000`,
  `platform_fdt_set_ufs`, `ufs_set_bootdevice`, `androidboot.boot_devices=bootdevice,soc/%08lx.ufshci,%08lx.ufshci`.
- Storage operations: `ufs_block_read`, `ufs_block_write`, `ufs_scsi_cmd_read10`,
  `ufs_scsi_cmd_write10`, `ufs_get_lu_size`, `ufs_get_device_info`, `ufs_check_lu_cfg`,
  `ufs_hba_stop`, `ufs_poweroff_notify`, `ufs_switch_part`, `ufs_set_write_protect`,
  `rpmb_ufs_clr_wp`, `rpmb_ufs_set_wp`, `ufs_is_rpmb_key_programmed`.
- Link/PHY/PMIC: `DME_LinkStartup`, `DME_LayerEnable`, `DME_ErrorPHYInd`,
  `[UFS] info: HS-G%d-%d, %d lane`, `[UFS] UFS version %x.%x`, `unipro_params: pa_h8time=0x%x`,
  `PMIC_STS_UFS_OC/OV/PG`, `MPHY Ctrl:`, `PA_INIT (3.0 FATAL ERROR)`, `[UFS] PHY error on Lane 0..3`.
- Boot from UFS confirmed by `%s:disable mmc0 when ufs boot`, `UFS_BOOT`, `atag,ufs`,
  `ufs_aio_block_read/write` also in `preloader_ufs.img`.

**Fastboot in LK**: strings show `app/fastboot/fastboot.c`, `fastboot_init`, `fastboot_buffer`,
`download:`, `flash:`, `erase:`, and standalone command tokens `boot`, `continue`, `reboot`,
`reboot-bootloader`. Also `fastboot: %s[len:%d]`, `[FASTBOOT] ...` OEM commands.

VERIFIED conclusion: LK initializes UFS and can read/write boot partitions and expose
fastboot. It therefore must be treated as the owner of UFS state at the moment a
chainloaded U-Boot starts.

INFERENCE (needs bench validation, not blocking): whether LK's `fastboot boot` handler
is compiled in and unrestricted on this Transsion build. Standard LK ships it; the
`boot`, `download:` and `continue` strings plus an unlocked/vbmeta-disabled configuration
make it very likely, but the only proof is running `fastboot boot <boot.img>` on device.

---

## 4. U-Boot UFS driver status for MT6789 — VERIFIED

### 4.1 Mainline U-Boot (public, checked this session)

Patch series `[PATCH 0/4] mtk: ufs: ...` by Igor Belwon, posted 2025-10-11:

- patch 3/4 `ufs: Add MediaTek UFS driver` — **State: Accepted**, commit
  `6cca3db2bd91ce1e5d812a61c13f1f62a2c2b82d`
  (https://patchwork.ozlabs.org/project/uboot/patch/20251011-mtk-ufs-uboot-v1-3-a05f991ee150@mentallysanemainliners.org/)
- patch 4/4 `MAINTAINERS: Add UFS to MediaTek section` — **Accepted**, commit
  `928269494c8495eee0a74c9685cc6c222e8ee0b7`
- patch 1/4 `phy: Add MediaTek UFS PHY Driver` — shown as **Deferred** on patchwork
  (INFERENCE: the generic PHY path may have been merged separately; check before relying on it).

Actual files present in `u-boot/u-boot` master `drivers/ufs/` (GitHub API listing):
`ufs-mediatek.c` (9,291 B), `ufs-mediatek.h`, `ufs-mediatek-sip.h`, `Kconfig`, `Makefile`,
plus the generic `ufs.c`, `ufs-uclass.c`, `unipro.h`, `ufshcd.c`-family files.
`drivers/ufs/Kconfig` contains `config UFS_MEDIATEK ... select PHY_MTK_UFS`.

`drivers/ufs/ufs-mediatek.c` `of_match` table (verbatim strings from master):
```c
	{ .compatible = "mediatek,mt6878-ufshci" },
	{ .compatible = "mediatek,mt8183-ufshci" },
	{ .compatible = "mediatek,mt8195-ufshci" },
```
**`mediatek,mt8183-ufshci` is exactly the X6886 compatible** (§2). No board-specific
quirk is needed just to bind.

### 4.2 Field-tested MT6789 fork — VERIFIED (source/docs, not our device)

Repo: https://github.com/mnnoee/u-boot-mtk-g99 (branch `mt6789-stable`; U-Boot
2025.10-rc5 for **Infinix X676B, MT6789 / Helio G99, UFS 2.2, 3 LUNs**).

- `docs/compatibility.md` states, verbatim in substance: device UFS compatible
  `mediatek,mt8183-ufshci`; their DTS uses `mediatek,mt8183-ufshci`; driver
  `drivers/ufs/ufs-mediatek.c`; "✅ matches real DT. Driver accepts both
  `mt6789-ufshci` and `mt8183-ufshci` — same code (`ufs_mtk_probe`)".
- `drivers/ufs/Makefile`: `obj-$(CONFIG_UFS_MEDIATEK) += ufs-mediatek.o`;
  `obj-$(CONFIG_UFS) += ufs.o ufs-uclass.o`.
- `drivers/ufs/Kconfig`: `config UFS_MEDIATEK` (depends on `UFS && ARCH_MEDIATEK`,
  selects `PHY_MTK_UFS`); `config UFS_MEDIATEK_EL3` — "should only be selected if
  U-Boot is used as BL2 bootloader. Such example would be replacing LK in smartphones".
- `drivers/ufs/ufs-mediatek.c` is the same Igor Belwon Linux-derived driver. It calls
  `mtk_ufs_bootloader_smc_reset()` via `arm_smccc_smc(MTK_SIP_BL_UFS_CONTROL, ...)`
  **when `CONFIG_UFS_MEDIATEK_EL3` is NOT set**, to take UFS ownership from secure world
  ("As we are the primary bootloader, and the first thing to run in non-secure world,
  we need to reset the UFS controller via the SMC").
- README `Status → Working` explicitly lists: `💾 UMS / fastboot block по UFS` —
  "Mass storage via `scsi`, phone storage visible on PC", plus `🔌 USB fastboot`
  and `🖥 USB ACM console` working.
- That fork **replaces** stock LK (flashes an LK-wrapped U-Boot into `lk_a`/`lk_b`),
  i.e. it is a different integration from our chainload, but it proves the driver,
  clock/reset handling and UFS I/O work on MT6789 hardware.

### 4.3 Integration caveats specific to our chainload (INFERENCE)

- In our plan LK runs *before* U-Boot. The upstream driver's default path performs the
  `MTK_SIP_BL_UFS_CONTROL` SMC reset at `ufs_mtk_pre_link()`. Whether that SMC is
  available in our ATF/TF-A build, and whether a reset here is safe after LK already
  brought the link up, must be tested. `CONFIG_UFS_MEDIATEK_EL3` exists precisely to
  skip that reset — evaluate both variants on device.
- The X6886 DT has `mediatek,ufs-disable-mcq`; the upstream driver sets
  `UFSHCD_QUIRK_MCQ_BROKEN_INTR` and related quirks. Verify MCQ stays off in U-Boot.
- The stock kernel module has Transsion-specific dependencies; do **not** try to load
  `.ko` files in U-Boot. Use the U-Boot driver + our DT node.

---

## 5. Minimum needed to boot an APFS/ext4 image or raw iOS payload — VERIFIED facts + reasoning

### Verified facts

- U-Boot's documented fastboot protocol (https://docs.u-boot.org/en/latest/android/fastboot-protocol.html)
  defines:
  - `download:%08x` — "Write data to memory which will be later used by `boot`, `ramdisk`, `flash`"
  - `boot` — "The previously downloaded data is a boot.img and should be booted according
    to the normal procedure for a boot.img"
  - `continue`, `flash:`, `erase:`, `getvar:`, etc.
  U-Boot therefore implements a receive-and-boot path over USB that never touches storage.
- The same capability is proven on MT6789 in the `mnnoee` fork (USB fastboot working).
- LK itself already speaks fastboot with `download:`/`boot`/`continue` (§3).

### Assessment

**(a) If the payload can be presented as an Android `boot.img`:**
No UFS driver is required in U-Boot for the payload step. Flow:
`host: fastboot download <boot.img>` → `fastboot boot` → U-Boot (or even LK) boots the
contained kernel/ramdisk from RAM. The APFS/ext4 image need not be on UFS at all; it can
be carried in the ramdisk/initramfs or mounted from a RAM-backed block device.

**(b) If the payload must be a raw image read from UFS:**
Then U-Boot needs the full UFS chain — generic UFS uclass + ufshcd core +
`ufs-mediatek` + MPHY PHY + SCSI disk + ext4/FS support. **This stack exists** (§4),
so it is a porting/config job, not a research blocker. Expected config surface:
`CONFIG_UFS`, `CONFIG_UFS_MEDIATEK`, `CONFIG_PHY_MTK_UFS`, `CONFIG_SCSI`,
`CONFIG_BLK`, `CONFIG_CMD_UFS`/`scsi scan`, `CONFIG_FS_EXT4` (APFS is not a standard
U-Boot filesystem — INFERENCE: an APFS volume would need a separate reader or be
converted/extracted to ext4/raw before U-Boot consumes it).

**(c) Cheapest architecture for the stated goal:**
Let LK own UFS exclusively. Chainload U-Boot from `boot_b` (LK reads it from UFS with
its own driver). Then deliver the payload to U-Boot over USB with `fastboot boot`, or
skip U-Boot in the payload step and use LK's own `fastboot boot`. UFS driver in U-Boot
then becomes optional hardening/fallback rather than a prerequisite.
Prerequisite that moves into focus: a working **USB gadget** driver in U-Boot for
MT6789 — VERIFIED present/working in the `mnnoee` fork, unverified for our board.

### What is NOT needed
- No need to reuse Transsion `.ko` modules in U-Boot.
- No need to reverse-engineer LK's UFS register programming to write a new driver —
  mainline already has one for this exact compatible.
- No need to modify UFS partition/GPT/layout for the chainload itself.

---

## 6. UFS calibration / EEPROM / preserved data (do NOT write) — VERIFIED + INFERENCE

- **No UFS calibration file exists in this dump.** Searches across `all_files.txt` for
  `ufs|calib|eeprom|efuse|nvram|tuning|trim` returned only: `images/preloader_ufs.img`,
  `images/efuse.img`, camera/audio NVRAM/tuning blobs, `nvmem-mt635x-efuse.ko`,
  `phy-mtk-ufs.ko`, and the five UFS modules. None is a UFS MPHY calibration blob.
  VERIFIED.
- `images/efuse.img` is 512 bytes of opaque data — no UFS-related strings. Do not write
  it. VERIFIED (content inspection).
- `preloader_ufs.img` contains UFS RPMB and BOOT-LU write-protect logic
  (`rpmb_ufs_set_wp`, `Clear UFS_LU_BOOT1/BOOT2/USER write protect fail`,
  `ufs_poweroff_notify`) and DRAM calibration. It is the component that owns early
  UFS provisioning. VERIFIED.
- `atag,ufs = <0x1000000 0x1000000>` in the live FDT is LK/preloader handoff data;
  treat as read-only. VERIFIED (presence), INFERENCE (semantic meaning).
- UFS device descriptor/geometry, MPHY tuning attributes (`pa_h8time`, `pa_tactivate`,
  `granularity`, HS-Gx lane settings) and RPMB keys live **inside the UFS device**, not
  in the dump. They are read at link startup by preloader/LK and re-read by the U-Boot
  driver. INFERENCE from runtime log strings in both LK and the kernel module;
  no evidence of an external per-device file.
- **NEVER write**: UFS RPMB, `UFS_LU_BOOT1/BOOT2/USER` write-protect regions,
  `efuse.img`/OTP, `atag,*` DT regions, preloader/LK partitions. A U-Boot stage should
  be read-only on UFS until a deliberate, separately-approved flash step.

---

## Evidence legend

- **VERIFIED** — directly observed in the dump, the two DTS files, LK `strings`/`modinfo`,
  or fetched public source/docs with URL/commit cited.
- **INFERENCE** — reasoned from observed strings/structure; explicitly flagged and to be
  confirmed on device before acting on it.

No code was written, no files in `/home/berucrypt00/otadump` were modified, and no
device command was executed.
