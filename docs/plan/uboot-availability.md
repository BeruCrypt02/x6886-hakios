# U-Boot availability for X6886 / MT6789 — research report

Date: 2026-11-23
Scope: web + local-file research only. No device access. No flashing. No driver code written.
Only file written is this report (one temporary `strings` scratch file under `/tmp/hakios-x6886` was created during analysis and deleted).

Evidence labels used below:

- **VERIFIED** — I read the actual file/API response/local artifact and quote it.
- **CLAIM** — the upstream author states it in README/commit message; I read the statement but could not reproduce the hardware test.
- **INFERENCE** — my reasoning from verified facts; not confirmed anywhere.

Tool note: `websearch` returned HTTP 402 (out of credits). Web research was done with `webfetch` against GitHub's public REST API and `raw.githubusercontent.com`, plus Firecrawl developer search for two queries before that MCP server was restarted.

---

## 0. Bottom line

A working MT6789 U-Boot **does exist and is buildable**. There are two independent lines:

1. **`mnnoee/u-boot-mtk-g99`** — Infinix **X676B** (same SoC MT6789, same vendor Transsion/Infinix), 219 iterations, prebuilt flashable LK-wrapped image, USB fastboot + ACM console + UMS + display + buttons working. It **replaces** stock LK in `lk_a`/`lk_b`.
2. **`sysretq0/advan-x1-uboot`** + **`sysretq0/loki`** — Advan X1 (MT6789), pristine upstream U-Boot with a 16-patch series, chainloaded from a patched stock LK via a `loki_payload` subpartition. This is the line that actually matches the "keep LK as stage 1, jump to U-Boot" idea.

But **the exact premise "stock LK stays untouched, U-Boot flashed to boot_b" is not supported by any working recipe found**:

- The only proven MT6789 chainload (`sysretq0/loki`) **patches stock LK** (hook in `bootstrap2 -> target_init`, security-policy patches, cert2 hash override) and repacks the LK container. It does not leave LK byte-identical. **CLAIM** (author) / **VERIFIED** (patch and profile files exist).
- The proven Infinix line **overwrites** `lk_a`/`lk_b` with an LK-wrapped U-Boot. **VERIFIED**.
- No repo, doc, thread or local ref describes booting U-Boot by putting it in `boot_b` as if it were a kernel. **VERIFIED** (searches returned nothing relevant; see §1.4). It is technically conceivable but unproven and has specific hazards (§5).

Recommendation to Beru: treat this as "buildable with substantial board bring-up, not a drop-in". The fastest de-risking step is a **local, offline `loki port` dry run against our own `images/lk.img`** — no flashing — to see whether an auto-generated X6886 profile is possible at all. That needs your explicit go-ahead (project rule 1).

---

## 1. Repo search results

### 1.1 GitHub repository search — `u-boot mtk mt6789`

**VERIFIED** — `https://api.github.com/search/repositories?q=u-boot+mtk+mt6789`, `total_count: 4`:

| Repo | URL | Created / last push | Claim |
|---|---|---|---|
| `sysretq0/advan-x1-uboot` | https://github.com/sysretq0/advan-x1-uboot | created 2026-11-21, pushed 2026-11-22 | "Clean patch-series upstream U-Boot port for Advan X1 (MediaTek MT6789 / Helio G99)" |
| `mnnoee/u-boot-x676b` | https://github.com/mnnoee/u-boot-x676b | created/pushed 2026-10-13 | "Flashable LK images, build scripts and project state for the Infinix X676B (MT6789) U-Boot port" |
| `mnnoee/u-boot-x676b_dtbo` | https://github.com/mnnoee/u-boot-x676b_dtbo | 2026-10-13 | DT sources/overlays for X676B |
| `mnnoee/u-boot-x676b_docs` | https://github.com/mnnoee/u-boot-x676b_docs | 2026-10-13 | RE notes / live register dumps |

### 1.2 The repo named in the prior conversation

**VERIFIED** — `https://api.github.com/repos/mnnoee/u-boot-mtk-g99` exists (1 star, branch `mt6789-stable`, created 2026-10-13, last push 2026-10-13). Description:

> "U-Boot 2025.10-rc5 for Infinix Note 12 Pro (X676B, MediaTek MT6789 / Helio G99). Replaces stock LK bootloader. Display, USB fastboot, USB ACM console, UMS and hardware buttons work; SD card is the current blocker."

Fork chain **VERIFIED** from the API `parent`/`source` fields:

- `mnnoee/u-boot-mtk-g99` ← fork of `ellyq/u-boot-mtk-g99` ("Das U-Boot with patches for MT6789/MT8781 SoCs (Helio G99)", pushed 2026-04-17)
- ← fork of `MT6878-Mainline/u-boot` ("das u-boot with patches for mt6878 soc", 12 stars, pushed 2026-08-28) — https://github.com/MT6878-Mainline/u-boot

Note the `mnoee` (one `n`) in one prebuilt-image link in the README is a typo; the account is `mnnoee`.

### 1.3 The Loki chainload toolchain

**VERIFIED** — `https://api.github.com/repos/sysretq0/loki`, Python GPL-3.0, created 2026-11-17, last push 2026-11-22. Contains `devices/mt6789/advan_x1_v4.json`, `advan_x1_v5.json`, `devices/mt6768/infinix_x689b.json`, `docs/`, `payload/`, `src/`, `tests/`, `socs/`, and git submodules pinned to `advan-x1-uboot` and `advan-x1-mainline`.

### 1.4 Searches that found nothing usable

**VERIFIED** — GitHub API:

- `q=mt6789+uboot+fastboot` → `total_count: 0`
- `q=mt6789+lk+bootloader+patch` → `total_count: 0`
- `q=u-boot+mtk+mt6789` → 4 repos, all listed in §1.1 (no "U-Boot in boot partition" project).

Firecrawl developer search for `check_payjoy_flag Transsion LK fastboot disabled by payjoy` returned generic PayJoy-removal/MDM pages and a Transsion init guide, **no LK source-level write-up**. Firecrawl developer search for `MTK Loki loki_payload chainload U-Boot stock LK 0x210000` found `sysretq0/loki` and upstream `doc/README.mediatek` (https://github.com/u-boot/u-boot/blob/master/doc/README.mediatek), which documents `mkimage -T mtk_image` with `lk=1`, `lkname=`, `media=` — i.e. MTK-style image headers, not Android boot images.

### 1.4.1 Credibility assessment of `sysretq0/*`

**VERIFIED** observations that make it look like real work, not an empty scaffold:

- All commits inspected are GPG-signed and GitHub reports `verification.verified: true`.
- `advan-x1-uboot` has 16 concrete patch files in `patches/` with real `diff --git` content (I read `0014-board-mediatek-Add-Advan-X1-MT6789-board-support.patch` — 367 insertions, full DTS/board code, signed-off).
- Commit messages contain hardware-specific detail (OVL0 `0x14005F40`, 1088px stride, `UFSHCD_QUIRK_KEEP_LINK_ACTIVE`, SMC `0xC2000115`) and a CI workflow is referenced.
- `loki` has pytest suite, device JSON profiles with pinned SHA-256, and docs with concrete memory maps.

**INFERENCE / caution:**

- Both repos are **2–6 days old**, 0 stars, 0 forks, no independent corroboration and no public hardware logs. The "hardware-verified" wording is the author's **CLAIM**.
- All MT6789 memory addresses in the Advan DTS (`0x60000000..0x6EFFFFFF`, framebuffer `0xfe007000`, BL31 `0x48200000`) are **Advan X1 specific**. Nothing in these repos mentions Infinix X6886 or Transsion LK. Do not transplant addresses.

---

## 2. Local refs at `/home/berucrypt00/x6886-hakios/refs/`

All refs are shallow/blobless clones; HEADs **VERIFIED** with `git log -1`:

| Ref | HEAD | Date | Origin |
|---|---|---|---|
| `hakios-mtk` | `a962798418fa1ebaa06554a69955803860907c6b` | 2026-11-10 | `oosexgang/hakios-xnu-kernel` |
| `hakios-common` | `62e09f850f1f713192228cb9031da256033ede93` | 2026-11-22 | `oosexgang/hakios-xnu-kernel` |
| `heliodarwin` | `856f3fbc4daf094fa1436697d81c38d352e5cb8b` | 2026-11-18 | `squeeeezy/HelioDarwin` |
| `klios716` | `318f265a7e975cc12ef4bfe063ad029026e90105` | 2026-11-05 | `lib-fluff-so/KLiOS716` |
| `unixnu-mt6789` | `cad9262392dd589b75c7c9bf38f3f6c256be6c31` | 2026-10-30 | `woaphone/UniXNU-mt6789-common` |
| `ios-jellystar` | `e75dac8f8e035145c8446a570ed0be567895c9a3` | 2026-11-21 | `maxexcloo/ios-jellystar` |

**VERIFIED** — a recursive grep for `u-boot|uboot|chainload|fastboot boot|lk.bin` over `*.md/*.sh/*.py/*.txt` in these six refs found **no U-Boot port, no U-Boot source tree, no U-Boot boot images**. The hits are about the XNU boot path and are relevant as negative/adjacent evidence:

- `heliodarwin/x676c/README.md` (X676C, MT6789 family) says: stock LK enters its shim with MMU off, Image at `0x40000000`, DTB at `0x47c80000`; **"fastboot boot was rejected by this LK, so deployment writes boot_a from rooted Android B."** This is direct local evidence that a Transsion-family LK rejects `fastboot boot` (X676C, not X6886 — **INFERENCE** that X6886 behaves the same).
- `hakios-mtk/docs/mtk-porting.md` describes the MT6768 `mtkloader` path: LK loads an MTK boot image, the loader is placed by patching/repacking, `fastboot boot` used only for one-off images, and the default is Android with a DRAM marker to switch. Same pattern conceptually as Loki: stage-1 LK stays, stage-2 loader decides what boots.
- `klios716/README.md` and `ios-jellystar` only mention fastboot in the context of stock recovery/TWRP — no chainload.
- `unixnu-mt6789` had no `lk|fastboot|bootloader|uboot` hits in its markdown at HEAD.

Conclusion: **no local ref contains a U-Boot image or a ready X6886 chainload recipe.**

---

## 3. What U-Boot needs on MT6789

### 3.1 DRAM — no DRAM init in U-Boot; it is a second-stage loader

**VERIFIED** — `sysretq0/loki/docs/BOOTCHAIN_LIFECYCLE.md`: BROM → preloader (BL2) calibrates LPDDR4X at base `0x40000000` → `bl2_ext` (BL2_EXT, secure EL3) copies BL33 LK to `0x40000000` and ATF BL31 to `0x48200000` → BL31 drops to non-secure EL1 → LK 2 runs → LK decompresses the kernel and jumps via SMC FID `0xC2000115` (`MTK_SIP_KERNEL_BOOT_AARCH64`).

**VERIFIED** — `sysretq0/advan-x1-uboot/README.md`: U-Boot is `CONFIG_POSITION_INDEPENDENT=y`, `CONFIG_TEXT_BASE=0x60000000`, runs with ATF BL31 resident in EL3 for PSCI and MTK SiP services; relocation clamped inside `0x60000000..0x6F000000`.

**VERIFIED** — the patch set adds clocks (`clk-mt6789.c`) and serial, but **no DRAM controller / DDR calibration driver**. `mnnoee/u-boot-mtk-g99` PROJECT_STATE likewise says the clock driver was rewritten from scratch because without it display/SD/USB did not work; DRAM is assumed initialized.

=> **VERIFIED: both ports depend on preloader/LK-initialized DRAM.** U-Boot never trains DDR itself.

**INFERENCE for X6886:** our DRAM base/size, EMI-MPU mblocks and BL31 location must be read from *our* dump/DT/live reserved-memory. Copying Advan's `0x60000000..0x6EFFFFFF` clean window is unsafe — an EMI-MPU overlay or secure carveout at a different address would give immediate synchronous aborts. Note also `mnnoee` found a real discrepancy on X676B: phone has 8 GB but DT declared 4 GB (memory map doc). Our amount is not established by this research.

### 3.2 UFS controller

**VERIFIED:**

- `sysretq0` defconfig: `CONFIG_UFS=y`, `CONFIG_UFS_MEDIATEK=y`, `CONFIG_SCSI=y`, `CONFIG_CMD_UFS=y`; patch series includes `0010-ufs-mediatek-Fix-PA_SCRAMBLING...`, `0011-ufs-Guard-unallocated-UTMRL...`, `0012-ufs-Add-UFSHCD_QUIRK_KEEP_LINK_ACTIVE-for-pre-initia...`. The commit message for `182fe9be` explicitly describes "pre-initialized UFSHCI handoff", `UTRLDBR` wait, 128 KiB max transfer cap and UNIT ATTENTION retry.
- `mnnoee` memory-map doc: UFS `0x11270000`, MUSB `0x11200000`, T-PHY `0x11f40000`; UFS LUNs exposed to fastboot through the SCSI layer (`fastboot flash block interface scsi`, device id 2).
- `heliodarwin/x676c/README.md` (local): UFS host `0x11270000`, named HFS+ root `iosroot`.

=> **Second-stage UFS works by taking over an already-initialized UFSHCI**. No from-scratch UFS PHY init is demonstrated.

**Scatter caution (VERIFIED):** `/home/berucrypt00/otadump/mtk/MT6789_Android_scatter.txt` labels `lk_a`/`lk_b` as `region: EMMC_USER`, `storage: HW_STORAGE_EMMC`, `partition_size: 0x400000`. That is the MTK scatter tool's naming convention and does **not** by itself prove eMMC; the boot-chain docs and all three local refs say UFS. **INFERENCE:** do not let this label change the driver choice without checking the GPT/LUN layout.

### 3.3 UART console

**VERIFIED:** both ports use UART0 at `0x11002000`; `sysretq0` defconfig sets `CONFIG_BAUDRATE=921600`, `CONFIG_DM_SERIAL=y`, `CONFIG_MTK_SERIAL=y`, plus `CONFIG_USB_FUNCTION_ACM=y`. `mnnoee` also provides USB CDC-ACM as a console (`usbacm`) so no UART cable is needed. Loki's AArch64 profile doc confirms LK uses UART0 at `0x11002000 @ 921600`.

### 3.4 Can U-Boot be packed as an Android boot image and loaded by stock LK as "kernel"?

**Short answer: no working recipe found; high risk.**

**VERIFIED facts pointing against it:**

- LK's kernel path (from Loki lifecycle doc) does: read `boot_<slot>` → decompress kernel to `0x40000000` → append cmdline → merge `lk_main_dtb` + `dtbo` → inject 31 mblock reservations → `boot_linux_fdt()` stages Kernel `0x40000000`, Ramdisk `0x66F00000`, DTB `0x47C80000` → MMU off + SMC `0xC2000115`. U-Boot is a position-independent ELF-derived binary, not a gzip/LZ4 Linux `Image`; nothing in either repo wraps it as one.
- Our stock `images/boot.img` header is MTK-modified, not a clean standard header: `magic = "ANDROID!"`, `kernel_size = 0x01202D0E = 18,885,902`, `kernel_addr = 0`, `ramdisk_size = 0x630`, `ramdisk_addr = 0`, `tags_addr = 0x400`, `page_size = 0`, header-version field at the standard offset reads 0. **INFERENCE:** `mkbootimg` defaults would produce a header LK may not accept.
- `heliodarwin/x676c` local doc: `fastboot boot` was rejected by that LK — so even the *temporary* route of streaming a boot image is unreliable on Transsion.
- AVB/verify strings in our LK (§5) mean `boot_b` content is verified before execution unless the device is unlocked / vbmeta disabled.
- U-Boot expects to be at `0x60000000` with BL31 present; LK would hand it `x0 = DTB at 0x47c80000` and the Linux-merged DTB, which is not the DTB U-Boot's drivers bind against.

**INFERENCE (possible but unproven):** a position-independent U-Boot *could* be wrapped in an LK-compatible boot header, decompressed to `0x40000000`, and start because AArch64 Linux boot protocol also passes `x0 = dtb`. It would need a custom shim to move to a safe DRAM window, re-point ATF/PSCI, and avoid the BL31/TEE carveouts. No one has published this. Treat as research, not a recipe.

---

## 4. Working build and packaging recipes

### 4.1 Line A — Infinix X676B (`mnnoee`), proven on the same vendor/SoC

**VERIFIED** from https://raw.githubusercontent.com/mnnoee/u-boot-mtk-g99/mt6789-stable/README.md:

```bash
git clone https://github.com/mnnoee/u-boot-mtk-g99.git
cd u-boot-mtk-g99
make CROSS_COMPILE=aarch64-linux-gnu- mt6789_infinix_x676b_defconfig
make CROSS_COMPILE=aarch64-linux-gnu- -j$(nproc)
python3 gimme-uboot.py lk_a_dump.img u-boot.bin uboot-lk-x676b.img
adb reboot bootloader
fastboot flash lk_a uboot-lk-x676b.img
fastboot flash lk_b uboot-lk-x676b.img
fastboot reboot
```

**VERIFIED** — `gimme-uboot.py` (read in full) uses `liblk.image.LkImage`, finds the `lk` subpartition in the stock container, creates a new `LkPartition` with the U-Boot bytes and the **original header and certificates**, pads to 16 bytes, recomputes `end_offset`/`image_list_end`, and saves a new LK container. Config: `CONFIG_USB_MUSB_PIO_ONLY=y`, fastboot on `scsi` LUN 2 via `CONFIG_FASTBOOT_FLASH_BLOCK_INTERFACE_NAME="scsi"`, `CONFIG_FASTBOOT_BUF_ADDR=0x60000000`, `CONFIG_FASTBOOT_BUF_SIZE=0x10000000`.

Prebuilt: release `x676b-v219`, `uboot-lk-x676b-v219.img` (~2.6 MB). README warning: **stock LK is mandatory for rollback and for the packing base**.

Status **CLAIM** (author's README/PROJECT_STATE): fastboot, ACM console, UMS, AMOLED text, volume keys, poweroff, A/B flashing work; SD card blocked; Linux boot panics `Attempted to kill init!` due to wrong DTB.

### 4.2 Line B — upstream U-Boot + Loki chainload (`sysretq0`), closest to the target design

**VERIFIED** from https://raw.githubusercontent.com/sysretq0/advan-x1-uboot/main/README.md and `tools/build.sh`:

```bash
# 1. verify patches apply cleanly on pristine upstream U-Boot
./tools/build.sh verify

# 2. build; pins upstream base commit c1958b5f6cce03956d309ce422b5d9f819955d4a
./tools/build.sh            # CROSS_COMPILE=aarch64-linux-gnu- by default

# 3. pack into a composite LK image (Loki repo, separate checkout)
./loki port /path/to/stock/lk.img --soc mt6789 --name "..." --fw v1 -o devices/mt6789/my_device.json
./loki pack devices/mt6789/advan_x1_v5.json \
    --lk /path/to/stock/lk.img \
    --payload /path/to/u-boot.bin \
    --boot-mode stealth \
    -o build/lk_loki_advan_x1_v5.img
./loki verify devices/mt6789/advan_x1_v5.json \
    --lk /path/to/stock/lk.img \
    --packed build/lk_loki_advan_x1_v5.img
```

`build.sh` enforces `MAX_UBOOT_BYTES=1966080` (payload must fit `0x210000..0x400000`), runs `checkpatch.pl`, and writes `sha256sums.txt`. Defconfig `mt6789_advan_x1_defconfig` enables `CONFIG_FIT`, `CONFIG_BOOTSTD_FULL`, `CONFIG_CMD_BOOTMENU`, `CONFIG_USB_FUNCTION_FASTBOOT`, `CONFIG_FASTBOOT_FLASH`, `CONFIG_FASTBOOT_FLASH_BLOCK` (scsi id 2), `CONFIG_USB_FUNCTION_ACM`, `CONFIG_UFS_MEDIATEK`, `CONFIG_VIDEO_SIMPLE`, `CONFIG_WDT_MTK`. `include/configs/mt6789.h` has a `bootmenu_2=USB Fastboot Mode` entry running `fastboot usb 0`, plus `kernel_addr_r=0x61000000`, `fdt_addr_r=0x60400000`, `fit_addr_r=0x68000000`, `fdt_high/initrd_high=0x6e000000`.

**VERIFIED** — the Advan Loki profile `devices/mt6789/advan_x1_v5.json` pins `stock_lk_sha256 = a11516b640cfdadfc616dcca9db0160ad72dd473a035830b08ed5802883ae834`, `container_size = 4194304`, `subpart_offset = 0x210000`, LK load `0xFFFF000050F00000`, cave `DEADCODE_MSDC_EMMC` at `0xFFFF000050F1F240` size 27552, hook `bootstrap2 -> target_init` at caller `0xFFFF000050F04A2C`, and a list of `security_patches`: `aee:sec_get_vfy_policy`, `aee:spoof_lock_state`, `aee:spoof_sboot_state: 2`, `bl2_ext:*`, `lk:bypass_security_control`, `lk:dont_relock_seccfg`, `lk:force_green_state`, `lk:sec_get_vfy_policy`, `lk:spoof_lock_state`, `lk:spoof_sboot_state: 2`.

**VERIFIED** — the same tool has an **Infinix** AArch32 profile `devices/mt6768/infinix_x689b.json` (MT6768, not our SoC) with `force_vfy_policy_zero` and `force_fastboot_unlocked` static patches and both liblk cert2 bypass modes (`override|wrap`) — useful precedent for Transsion LK, not directly reusable.

### 4.3 `fastboot boot` from RAM

**VERIFIED** — upstream U-Boot docs https://raw.githubusercontent.com/u-boot/u-boot/master/doc/android/fastboot.rst: standard commands include `boot`; default behaviour is `bootm <CONFIG_FASTBOOT_BUF_ADDR>`; if env `fastboot_bootcmd` is set it is executed instead. Client usage: `fastboot -b <addr> -c "<cmdline>" boot zImage`.

**INFERENCE:** for XNU or a raw ARM64 Linux `Image` you must set `fastboot_bootcmd` to something like a `booti`/FIT path with our DTB; neither MT6789 port documents doing this. `sysretq0` exposes the fastboot server (`fastboot usb 0`) and has the buffer config, so the transport is there; the payload format is the open question.

### 4.4 About mkbootimg header version / DTB / ramdisk

**VERIFIED:** no recipe in either repo uses `mkbootimg`. Line A uses `liblk` container repacking; line B uses `loki pack` with a raw `u-boot.bin` payload plus a compiled Stage-1 cave. Upstream `doc/README.mediatek` documents `mkimage -T mtk_image ... lk=1` for MTK header generation. **INFERENCE:** a `mkbootimg`-based "U-Boot as kernel" recipe would be new work; the stock header's zero page-size/address fields make it non-trivial (§3.4).

---

## 5. Transsion LK risks — evidence from our own dump

Read-only inspection of `/home/berucrypt00/otadump/images/lk.img` (3,149,992 bytes; MTK header magic `0x58881688`, first subimage size field `0x000E7C78`) with `strings`:

### 5.1 PayJoy

**VERIFIED** literal strings present in our LK:

- `check_payjoy_flag`
- `[tran] tran_tkv_get_payjoy_flag is failed`
- `INFO: fastboot disabled by payjoy!!!`

Impact: on a PayJoy-flagged unit, **fastboot in stock LK can be disabled outright**. That blocks the initial `fastboot flash lk_a/lk_b` needed to install U-Boot at all. Recovery path would be BROM / mtkclient (as both repos document as fallback), which is a separate decision. **INFERENCE:** if fastboot is disabled but LK still boots normally, the Loki stealth menu is actually the better delivery vehicle — but you still need one write primitive to get there.

### 5.2 Anti-rollback / unlock policy

**VERIFIED** strings:

- `[ANTI_ROLLBACK] magic == 0`, `[ANTI_ROLLBACK] magic mismatch, tran_devctrl.magic = 0x%x`
- `[ANTI_ROLLBACK] read %s partition err =%d`, `[ANTI_ROLLBACK] write %s partition err =%d`
- `[ANTI_ROLLBACK] opt update error`, `anti_rollback_status=%x`, `anti_rollback_ver`
- `[ANTI_ROLLBACK] sbc is not enable`
- `[UNLOCK_VFY] auth fail, unlock is forbidden!`, `is_unlock_allowed = 0x%x`, `unlock_ability is true/false`
- `oem tran_unlock`, `Prohibit unlock operation`, `flashing unlock`, `Failed to unlock the device! Will return to fastboot in 3s`
- `[SEC_UNLOCK]`

Impact: Transsion LK has its own TKV-backed rollback and unlock authorization beyond stock Android fastboot. **INFERENCE:** do not assume `fastboot flashing unlock` or a disabled vbmeta is sufficient; reflashing an older LK version or an unauthorised LK container may be refused. Never downgrade LK. The Loki security patches (`spoof_lock_state`, `spoof_sboot_state`, `force_green_state`, `sec_get_vfy_policy`) exist precisely because of this class of check — but they are from an Advan profile, not validated for X6886.

### 5.3 AVB / signature verification

**VERIFIED** strings include:

- `[AVB] recovery pubk hash in lk partition =`, `[AVB] vbmeta_avb_key =`, `[AVB] lk_avb_key =`
- `Error getting rollback index from AVB_GROUP` / `RECOVERY_GROUP`, `: Invalid rollback_index_location.`
- `SIGNATURE_MISMATCH`, `[SEC] verify '%s' (%d bytes in %d ms)`
- `dm-verity corruption`, `[FASTBOOT] Start reset dm-verity status.`
- `UNLOCK_VFY] rsa_oaep pass`, `[UNLOCK_VFY] auth ok, unlock is allow!`

Our master notes record that `fstab` uses `avb=vbmeta_system` and a disabled vbmeta is enough for the *system* partitions. **INFERENCE:** that does not automatically cover the `boot_b` payload — LK may still run `[SEC] verify` on the image it decompresses, and there are separate LK-embedded pubkey hashes. Any "U-Boot in boot_b" design must account for the LK verify gate, not just Android AVB.

### 5.4 Specific to the "flash U-Boot to boot_b" premise

1. **Slot selection:** LK chooses the active slot via `get_suffix()`. Flashing only `boot_b` does nothing unless B is active. Loki's stealth TUI has an explicit `Switch Slot` option; stock fastboot needs `fastboot set_active b`.
2. **Partition geometry (VERIFIED):** `lk_a` and `lk_b` are each `0x400000` (4 MiB) in the scatter. Our dumped `lk.img` is 3,149,992 bytes, i.e. **less than the full 4 MiB** and with a different payload size than the Advan profile. `loki port` must derive a fresh X6886 profile; the pinned Advan SHA-256 and offsets are useless here.
3. **AVB / [SEC] verify** as above.
4. **DRAM / EMI-MPU / BL31** as in §3.1 — the boot path hands off to Linux at `0x40000000` with a Linux DTB; U-Boot needs a different environment.
5. **No rollback path if boot_b is used as the only U-Boot copy:** stock LK must remain flashable. Both repos insist on keeping a stock LK backup.

---

## 6. Open questions / next steps (no action taken)

1. Does `loki port` succeed against our `images/lk.img` (auto-discover hook, cave, symbols)? Pure offline analysis; needs Beru's approval.
2. What are X6886's clean DRAM window, BL31 address, framebuffer base/pitch and EMI-MPU mblocks from our dump/DT? Needed before any address in Advan's DTS can be reused.
3. Is the stock LK on this unit PayJoy-flagged at runtime? Only a device check can answer; if yes, initial flashing needs BROM/mtkclient.
4. Does U-Boot's `fastboot boot` need a `fastboot_bootcmd` override for the XNU payload, and what DTB does it use?
5. Do we want the Loki approach at all? It requires patching stock LK (hook + security patches + cert2 override), which conflicts with the literal "stock LK untouched" requirement as currently written.

## 7. Source index (exact URLs read)

- https://api.github.com/search/repositories?q=u-boot+mtk+mt6789
- https://api.github.com/search/repositories?q=mt6789+uboot+fastboot
- https://api.github.com/search/repositories?q=mt6789+lk+bootloader+patch
- https://api.github.com/repos/mnnoee/u-boot-mtk-g99
- https://api.github.com/repos/mnnoee/u-boot-x676b/contents/
- https://raw.githubusercontent.com/mnnoee/u-boot-x676b/main/README.md
- https://raw.githubusercontent.com/mnnoee/u-boot-x676b/main/PROJECT_STATE.md
- https://raw.githubusercontent.com/mnnoee/u-boot-mtk-g99/mt6789-stable/README.md
- https://raw.githubusercontent.com/mnnoee/u-boot-mtk-g99/mt6789-stable/docs/hardware/memory-map.md
- https://raw.githubusercontent.com/mnnoee/u-boot-mtk-g99/mt6789-stable/docs/hardware/usb.md
- https://raw.githubusercontent.com/mnnoee/u-boot-mtk-g99/mt6789-stable/gimme-uboot.py
- https://api.github.com/repos/mnnoee/u-boot-mtk-g99/commits?sha=mt6789-stable&per_page=3
- https://api.github.com/repos/sysretq0/advan-x1-uboot
- https://api.github.com/repos/sysretq0/advan-x1-uboot/contents/
- https://api.github.com/repos/sysretq0/advan-x1-uboot/contents/patches
- https://api.github.com/repos/sysretq0/advan-x1-uboot/commits?per_page=20
- https://raw.githubusercontent.com/sysretq0/advan-x1-uboot/main/README.md
- https://raw.githubusercontent.com/sysretq0/advan-x1-uboot/main/tools/build.sh
- https://raw.githubusercontent.com/sysretq0/advan-x1-uboot/main/configs/mt6789_advan_x1_defconfig
- https://raw.githubusercontent.com/sysretq0/advan-x1-uboot/main/patches/0014-board-mediatek-Add-Advan-X1-MT6789-board-support.patch
- https://api.github.com/repos/sysretq0/loki
- https://api.github.com/repos/sysretq0/loki/contents/
- https://api.github.com/repos/sysretq0/loki/contents/devices/mt6789
- https://api.github.com/repos/sysretq0/loki/contents/docs
- https://api.github.com/repos/sysretq0/loki/commits?per_page=8
- https://raw.githubusercontent.com/sysretq0/loki/main/README.md
- https://raw.githubusercontent.com/sysretq0/loki/main/docs/BOOTCHAIN_LIFECYCLE.md
- https://raw.githubusercontent.com/sysretq0/loki/main/devices/mt6789/advan_x1_v5.json
- https://raw.githubusercontent.com/u-boot/u-boot/master/doc/android/fastboot.rst
- https://github.com/u-boot/u-boot/blob/master/doc/README.mediatek
- local: `/home/berucrypt00/otadump/images/lk.img`, `/home/berucrypt00/otadump/images/boot.img`, `/home/berucrypt00/otadump/mtk/MT6789_Android_scatter.txt`
- local refs: paths and HEADs listed in §2
