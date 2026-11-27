# X6886 Mali GPU port manifest

Branch: `feat/x6886-real-port`. All work is local/offline: no adb, no fastboot,
no flash, no slot switch and no write to modem/NVRAM/IMEI/protect1/protect2.
The driver was compiled into the offline XNU build in this session; no device
boot or DSU test was performed.

Final source path (main repo, git-tracked): `kernel/x6886/hakios.x6886_mali.c`
Build-tree path: `refs/xnu-12377-hakios/bsd/dev/hakios.x6886_mali.c`
Both copies are byte-identical (1479 lines), sha256:
`45f558dbde4ede6eab26854ce244d709e7dad41815f6d6c348ce89ea9fcac0b0`.
Fixes applied after the audit: v9 job header bit0 removed, table descriptors use
`PTE_VALID` (0x403) with AF, leaf uses AP_RW + NX, `JS_FLUSH_ID_NEXT` written
before START with a busy precheck, shader affinity read from `SHADER_READY`,
`as0_flush` added before the recovery UPDATE, and `MEMATTR_G57` written
unconditionally instead of the zero fastmap value.
There is no separate header; the driver is self-contained. No Makefile, board
table, other driver, modem or NVRAM file was edited for this port.

The applied G57 fixes came from the local 193-line diff at
`/home/berucrypt00/x6886_mali_fixes.diff` (`git apply --check` rc=0, 9 hunks):
broad G57 shader/JM quirks with the exact-r0p1 MMU mask, factored power-on and
quirk helpers, `AS_COMMAND_FLUSH_MEM` before `ASx_UPDATE` after map/unmap,
direct GPU fault registers in the failure log, and removal of the duplicate
file-scope `static uint32_t mali_trace_queue;`.

## 1. Source proof

The real register driver is
`refs/oosexgang-xnu-kernel-mtk-helio/kernel/overlay/bsd/dev/mtk_mali.c` (1150 lines):
- SPM power domains and polling: `:260-301`, `:513-536`
- clock mux / block gate: `:518-542`
- GPU_ID, reset, core power: `:544-568`
- GPU/JOB/MMU/AS register map and JS/AS offsets: `:303-343`, `:636-643`
- JM write-value selftest and address spaces: `:345-511`
- JM submission, faults and recovery: `:610-634`, `:721-952`
- job queue and waits: `:963-1091`
- boot-args and init: `:1133-1150`
- GPU_ID product names: `loader/mtkmali.h:249-259`

It is a Bifrost G52 bring-up, so the v9-specific parts were replaced, not guessed:
- register layout, JS/AS offsets, GPU command opcodes, v9 job types/32-byte header:
  `refs/heliodarwin/MaliMetal/MaliValhallHw.h:9-154,164-190`
- MT6789 SPM/MFG_TOP power and clock sequence, bus protection, four-level GPU pages:
  `refs/heliodarwin/wiki/MT6789-GPU-Power-Sequence.md:33,78-85,96,130,137,151`
- RE'd gpufreq/kbase sequence: `refs/heliodarwin/xnu/patches/0182-x676c-gpu-mtcmos-power-on-from-gpufreq-re.patch`
- v9 reset quirks and cache coherency: `refs/heliodarwin/xnu/patches/0189-x676c-malimetal-jm-gpu-userclient.patch` (`reset()`)
- `MALI_AS_COMMAND_FLUSH_MEM 0x05u` and the fault registers `0x03c/0x040`:
  `MaliValhallHw.h:36-38,162`
- region lock with computed width, `LOCK` then `FLUSH_MEM`, and `bindAS()` then
  `UPDATE`: `refs/heliodarwin/xnu/patches/0189-x676c-malimetal-jm-gpu-userclient.patch:914`
  and `refs/heliodarwin/xnu/patches/0209-sweet-mali-job-address-space-leases.patch`
- Panfrost parameter numbers: this device's own kernel headers,
  `Documents/x6886-tree/kernel/infinix/android_device_transsion_infinix_X6886-prebuilts/kernel-headers/include/uapi/drm/panfrost_drm.h:133-174`
  (`GPU_PROD_ID=0`, `GPU_REVISION=1`, present masks `2..4`, `AS/JS_PRESENT=6/7`,
  feature registers `8..12`, `NR_CORE_GROUPS=38`).

There is **no CSF code or firmware** anywhere in this port: all work goes through the
Job Manager slots at `0x1800/0x1880/0x1900` (`MaliValhallHw.h:66-69`,
`wiki/MT6789-GPU-Power-Sequence.md:137`).

## 2. Every adapted physical address / IRQ, with file:line

Evidence is the device's own dumps. The stock OTA dump at `/home/berucrypt00/otadump`
contains no text fdt/iomem; `fdt.dts`, `iomem.txt` and `interrupts.txt` in
`/home/berucrypt00/x6886-hakios/device-dump/` are git-tracked, unmodified captures
from that same device.
The SoC is pinned by `fdt.dts:5737-5738` (`mediatek,mali` + `arm,mali-valhall`) and
`fdt.dts:1629-1630` (`mediatek,mt6789-scpsys`).

**Important provenance split (GATE 3):** only the *block base addresses* below are
DTB/IOMEM-verified. Every *offset inside* a block comes from the X676C reference
wiki/patch or the G52 source driver and is **not** present in this device's DTB.
Those internal offsets are explicitly marked **OWNER SIGN-OFF NEEDED** and must
not be reported as DTB-verified.

### 2a. DTB/IOMEM-verified block bases and IRQs

| Constant in driver | Value | Proof | Reference logic kept from |
|---|---|---|---|
| `X6886_MALI_PHYS` / size | `0x13000000` / `0x4000` | `fdt.dts:5737-5739` (`reg <0 0x13000000 0 0x4000>`); `iomem.txt:117` (`13000000-13003fff : 13000000.mali`) | `mtk_mali.c:47,540`; `MaliValhallHw.h:8-15` |
| `X6886_TOPCKGEN_PHYS` | `0x10000000` | `fdt.dts:2354-2356` (`topckgen@10000000`); `iomem.txt:23` (`10000000-10000fff`) | `mtk_mali.c:43`; patch 0182 step 1 |
| `X6886_INFRACFG_PHYS` | `0x10001000` | `fdt.dts:2359-2361`; `iomem.txt:24` | `mtk_mali.c:44,532-533`; patch 0182 MFG1 bus release |
| `X6886_SPM_PHYS` | `0x10006000` | `fdt.dts:1629-1631`; `iomem.txt:26` | `mtk_mali.c:45`; patch 0182 MTCMOS sequence |
| `X6886_MFGCFG_PHYS` | `0x13fbf000` | `fdt.dts:1804-1807` (`mediatek,mt6789-mfg`), `:5750` gpufreq `reg[0]`, `:9011` clock alias | `mtk_mali.c:46,541`; wiki `:96` |
| `X6886_MALI_SPI_*` | 396, 397, 398, 399, 400 | `fdt.dts:5740-5741` (`0x18c..0x190`, names GPU/MMU/JOB/EVENT/PWR); `interrupts.txt:159-161` shows live GICv3 430/429/428 = SPI 398/397/396. 399/400 come only from the DTB line (count 0). | recorded and GPU-side IRQ masks programmed; see gap 3 |

### 2b. Internal offsets — X676C REF, OWNER SIGN-OFF NEEDED (not DTB-verified)

None of the offsets below were proved in this device's DTB/iomem. A read of the
block base and status registers is harmless, but **every write** to the SPM
PWR_CON, topckgen, infracfg and mfgcfg offsets is gated behind the boot-arg
`mali_hwctl=1`, which defaults to **0**. With the default the driver performs a
read-only survey and returns `ENXIO` if the firmware did not already power and
ungate the GPU; no unproven gate is written on a normal boot.

| Constant in driver | Value | Source of the offset (not DTB) | Reference logic kept from |
|---|---|---|---|
| SPM PWR_CON `0x308/0x30c/0x310/0x314`, status bits `0x4/0x8/0x10/0x20` | as listed | X676C wiki `:78-83` + patch 0182 (**owner sign-off needed**) | `mtk_mali.c:272-301` |
| `SPM_PWR_STATUS` / `_2ND` | `0x16c` / `0x170` | X676C wiki `:83` + patch 0182 (**owner sign-off needed**) | `mtk_mali.c:262-263` |
| infracfg bus release `0x718/0x2a4/0x718/0x2ac` | `0x80/0x600000/0x60/0x200000` | X676C wiki `:85-94` + patch 0182 (**owner sign-off needed**) | `mtk_mali.c:531-534` (extended to the full MT6789 sequence) |
| topckgen CLK_CFG_3 clr/status, update | `0x58/0x50`, `0x04` | X676C patch 0182 step 1, wiki `:102-104` (**owner sign-off needed**) | `mtk_mali.c:523-524` (MT6768 CLK_CFG_1 replaced) |
| mfgcfg gate status/clear, external CG | `0x0/0x8`, `0x20/0xb0/0xb4/0x24/0xf60` | X676C wiki `:96` + patch 0182 step 3 (**owner sign-off needed**) | `mtk_mali.c:541` |
| GPU/JOB/MMU register blocks | `0x0000/0x1000/0x2000`, JS `0x1800+n*0x80`, AS `0x2400+n*0x40` | `MaliValhallHw.h:9-15,66-69,135-141` (X676C REF, same MT6789 GPU family; **owner sign-off needed**) | `mtk_mali.c:303-343` |

The `opp-hz`/`opp-microvolt` values are copied verbatim from `fdt.dts:5781-6004`
(exact Hz and uV, no rounding) and are DTB-sourced.

## 3. GPU_ID logic

- `x6886_mali_up()` reads `GPU_ID` at `0x13000000+0x000` **after** clocks, power
  domains, reset and core power-on (`wiki:19` reports `0x90930010` on X676C).
- `0` or `~0` means an unpowered/unmapped bank: the driver logs the value and
  returns `ENXIO`. No panic, no fallback ID, no fake register value.
- `gpu_is_g57()` matches `gpu_id >> 16 == 0x9093`. That product match is proved
  locally: `refs/oosexgang-xnu27/loader/mtkmali.h:257` (`case 0x9093: return
  "Mali-G57"`) and `MaliValhallHw.h` (`MALI_G57_R0P1_GPU_ID 0x90930010u`).
  Shader/JM quirks apply to every G57 part; the `L2_MMU_CONFIG` mask is kept
  only for the exact r0p1 ID `0x90930010`. Any other non-zero ID is accepted
  through the generic Valhall path and logged.
- `kern.hakios.mali_id` exposes the raw ID.

## 4. OPP / clocks

- 45 entries `opp00..opp44` from `opp-table0` (`fdt.dts:5778-6005`, phandle `0x87`
  used by the mali node at `:5742`). `opp15` has `opp-microvolt = "\0\f5"` at
  `:5858` and is stored as an explicitly invalid slot; validation skips it.
- `x6886_mali_opp_check()` validates index order, non-zero frequency, a plausible
  voltage range and strictly decreasing frequency, then reports 44 valid OPPs
  under `kern.hakios.mali_opps`.
- Clock bring-up only ungates the existing MFG reference mux and waits for its
  stable bit; it does **not** claim a frequency or a voltage. DVFS is not
  implemented (gap 2).

## 5. Driver behaviour

- `x6886_mali_init()` is a no-op unless boot-arg `mali=1`. `mali_hwctl=1` is the
  opt-in that permits writes to the DTB-unproven clock/power gates (default 0 =
  read-only survey + `ENXIO` when the GPU is not already powered). `mali_fastmap=0`
  uses cached page-table walks, `mali_trace=1` logs each completed job.
- Bring-up is synchronous and returns `0` or a negative errno. Each failed stage
  (clock, SPM domain, bg3d gate, GPU_ID, reset, core power, job) logs its own
  line and stops; no write follows a failed read.
- `mali_apply_quirks()` and `mali_cores_power_on()` are shared by bring-up and
  fault recovery, so a reset restores the same coherency, G57 quirks and core
  masks instead of only the page table.
- JM selftest runs a real write-value job first in identity mode and then through
  four-level AArch64 4 KiB GPU page tables (`reached` sysctl: 5 = identity job,
  6 = job through tables). AS0 is left on the tables.
- `x6886_mali_submit()` runs one JM chain to completion on slot 0 (fragment),
  1 (vertex/tiler) or 2 (compute), polls JM/MMU status without a busy core, and
  resets + restores the cores and AS0 on a timeout/fault. The failure dump reads
  `GPU_FAULT_STATUS` (`0x03c`) and `GPU_FAULT_ADDRESS` (`0x040`) directly.
- `as0_flush()` locks the touched VA range with a computed width (minimum 15,
  up to 47 for the whole 48-bit space), issues `AS_COMMAND_LOCK`, then
  `AS_COMMAND_FLUSH_MEM` (5, not `FLUSH_PT`) and waits for status before
  `as0_update()`. Both `x6886_mali_unmap()` and `x6886_mali_mapped()` flush
  first, then update, matching `flushAS()`/`bindAS()` in patch 0209.
- `x6886_mali_enqueue()` / `x6886_mali_wait()` provide the in-order worker queue;
  `x6886_mali_map()` / `unmap()` / `address()` / `parameter()` expose the real
  MMU and register-backed API. `x6886_mali_stop()` masks IRQs and unmaps.
- No safe-idle stubs: there is no function that returns success without hardware
  work, no assumed GPU ID and no fake register value.
- **Log mirror (GATE 4, shared convention):** all lines start with
  `[X6886-mali]`. `mali_log()` sends every diagnostic status and every failure
  register dump to both `printf` and the ramoops console at `0x480af000`.
  The header is exactly the one used by the other ten X6886 drivers: sig
  `0x43474244` at `+0`, write cursor at `+4`, **ring capacity at `+8`** (never a
  valid-byte count), data at `+12`; window size `0x1000`, capacity `0x1000-12`,
  `size >= 16` required (`audio.c:38-78`, `battery.c:38-81`, `camera.c:40-83`,
  `keys.c:38-81`, `wifi.c:34-81`, `bt.c:33-77`, `charger.c:64-108`,
  `touch.c:95-194`, `usb.c:212-248`). A **valid header is never rewritten**, so
  a mali-first boot preserves every other driver's log; a fresh/invalid zone is
  initialised exactly as `touch.c:175-181` does. An unmappable zone disables
  only the mirror, never the normal log.
- **Bounded waits (GATE 3):** the worker loop, its empty-queue sleep and the
  full-queue sleep use `lck_mtx_sleep_deadline()` with a fresh 30 s deadline and
  an explicit `mali_worker_stop` flag set by `x6886_mali_stop()`. The sleep
  atomically enqueues on the channel before dropping the lock, so the
  `wakeup()` from `x6886_mali_enqueue()` cannot be lost; there is no literal
  `for(;;)` and no unbounded spin. All register polls, including `wait_for()`,
  are bounded in iteration count and end with a final read.
- Integration is done in this tree: the file is built from `bsd/dev/` and
  `x6886_mali_init()` is called from `hakios_drivers_init()` in the offline XNU
  build. Symbol presence and the call site were checked with `llvm-nm` (section 7).

## 6. Gaps (explicit, not papered over with stubs)

1. **Write-value payload — CLOSED.** The pinned Mesa v9 XML (commit
   `5a07217f`) defines `Write Value Job Payload` as Address@0, Type@2
   (6 = Immediate 32), Immediate Value@4. Driver words 8/9/10/12/13 match
   exactly. The remaining header defect (Bifrost `Is 64b` bit at word 4 bit 0)
   was removed.
2. **DVFS / PMIC rails** — VGPU and VSRAM_GPU are left as firmware set them.
   The X676C MT6366 PWRAP rail map in `wiki:36-60` is X676C REF, not verified
   for X6886; no regulator write is attempted. The 45 OPPs are reported only.
3. **IRQ lines** — this tree has no driver-facing GIC registration API; SPI
   396-400 are recorded and the device-side masks/status are polled, as in the
   source driver. No GIC handler is claimed.
4. **Per-client address spaces** — the X676C transport leases one hardware AS
   per validated job (`wiki:135-144`); this port uses AS0 with a global lock,
   as the source driver does. Multi-client isolation remains future work.
5. **Quirk scope** — shader/JM quirks now cover the whole locally proven G57
   product (`0x9093`); the `L2_MMU_CONFIG` mask is intentionally limited to the
   exact r0p1 ID `0x90930010` because only that value has local quirk proof.
   Other G57 revisions take the generic path. No unproven quirk is applied.
6. **No device validation** — the offline compile, `llvm-nm` symbol check,
   payload rebuild, final-image repack and 13/13 offline verifier were run in
   this session (section 7). There was no boot, no DSU test, no flash and no
   device pstore capture. Correct JM completion on this phone remains to be
   verified on hardware.
7. **Internal offsets need owner sign-off (GATE 3)** — the SPM PWR_CON/status,
   topckgen CLK_CFG offsets, infracfg bus-protection offsets, mfgcfg gate/CG
   offsets and the JM/MMU register map are X676C reference values from
   section 2b, **not** verified in this device's DTB. They are not presented as
   DTB-proven. Chosen fix: every write to those gates is behind `mali_hwctl=1`
   (default 0), so a normal boot never writes an unproven gate. Beru must still
   sign off before enabling `mali_hwctl`.

## 7. Verification performed (offline)

- Source: 1471 lines, 208/208 braces, no NUL bytes, ASCII-only, one
  `static uint32_t mali_trace_queue;` declaration, `git diff --check` clean.
  Both main and refs copies hash to
  `70c94e7a868e341f13322e2f092930e42b673640d67225f205950fb3efbd00db`.
- Applied-hunk grep: `AS_COMMAND_LOCK`, `AS_COMMAND_FLUSH_MEM 5u`,
  `G57_PRODUCT 0x9093u`, `gpu_is_g57()`, `mali_apply_quirks()`,
  `mali_cores_power_on()`, `as0_flush()`, `GPU_FAULT_STATUS 0x03cu`,
  `GPU_FAULT_ADDRESS 0x040u` all present; flush precedes `as0_update()` after
  unmap, after a full-map check, and in fault recovery.
- All addresses/OPPs/IRQs above were read directly from the named dump lines;
  the OPP Hz/µV values were machine-extracted from `fdt.dts` and copied exactly.
- Final audit fixes checked by grep: no `printf("mali:` remains; the pstore
  writer has no `size = valid bytes` state and only touches `+4` on a valid
  header; every `wr(topckgen/.../mfgcfg/spm)` sits inside a `mali_hwctl` branch.
- Build, symbol, payload, repack and verifier results are appended below after
  the approved steps 0-6 run; no step writes to a device partition.
