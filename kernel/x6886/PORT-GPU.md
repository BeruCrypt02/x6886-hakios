# X6886 Mali GPU port manifest

Branch: `feat/x6886-real-port`. 100% offline: no adb, no fastboot, no kernel/XNU build, no flash.
Only driver/manifest files were written; no Makefile, build file, board table, other driver,
modem or NVRAM file was touched.

Final source path (main repo, git-tracked): `kernel/x6886/hakios.x6886_mali.c`
Pre-path-change copy (also written, for the refs tree integrator):
`refs/xnu-12377-hakios/bsd/dev/hakios.x6886_mali.c`. Both files are byte-identical
(sha256 `fb34b7831ed142763d1e46f94fadc10ea89f3316eb53760b405ba6d038c04e30`).
There is no separate header; the driver is self-contained.

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
`/home/berucrypt00/x6886-hakios/device-dump/` were captured from that same device.
The SoC is pinned by `fdt.dts:5738` (`mediatek,mali` + `arm,mali-valhall`) and
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
| `X6886_TOPCKGEN_PHYS` | `0x10000000` | `fdt.dts:1582-1584`, `:2354-2356`; `iomem.txt:23` | `mtk_mali.c:43`; patch 0182 step 1 |
| `X6886_INFRACFG_PHYS` | `0x10001000` | `fdt.dts:1590-1592`, `:2359-2361`; `iomem.txt:24` | `mtk_mali.c:44,532-533`; patch 0182 MFG1 bus release |
| `X6886_SPM_PHYS` | `0x10006000` | `fdt.dts:1629-1631`; `iomem.txt:26` | `mtk_mali.c:45`; patch 0182 MTCMOS sequence |
| `X6886_MFGCFG_PHYS` | `0x13fbf000` | `fdt.dts:1805-1807` (`mediatek,mt6789-mfg`), `:5750` gpufreq reg[0], `:9011` alias | `mtk_mali.c:46,541`; wiki `:96` |
| `X6886_MALI_SPI_*` | 396, 397, 398, 399, 400 | `fdt.dts:5740-5741` (`0x18c..0x190`, names GPU/MMU/JOB/EVENT/PWR); `interrupts.txt:159-161` shows GICv3 428/429/430 = SPI 396/397/398 | recorded and GPU-side IRQ masks programmed; see gap 4 |

### 2b. Internal offsets — X676C REF, OWNER SIGN-OFF NEEDED (not DTB-verified)

| Constant in driver | Value | Source of the offset (not DTB) | Reference logic kept from |
|---|---|---|---|
| SPM PWR_CON `0x308/0x30c/0x310/0x314`, status bits `0x4/0x8/0x10/0x20` | as listed | X676C wiki `:78-83` + patch 0182 (**owner sign-off needed**) | `mtk_mali.c:272-301` |
| `SPM_PWR_STATUS` / `_2ND` | `0x16c` / `0x170` | X676C wiki `:83` + patch 0182 (**owner sign-off needed**) | `mtk_mali.c:262-263` |
| infracfg bus release `0x718/0x2a4/0x718/0x2ac` | `0x80/0x600000/0x60/0x200000` | X676C wiki `:85-94` + patch 0182 (**owner sign-off needed**) | `mtk_mali.c:531-534` (extended to the full MT6789 sequence) |
| topckgen CLK_CFG_3 clr/status, update | `0x58/0x50`, `0x04` | X676C patch 0182 step 1, wiki `:102-104` (**owner sign-off needed**) | `mtk_mali.c:523-524` (MT6768 CLK_CFG_1 replaced) |
| mfgcfg gate status/clear, external CG | `0x0/0x8`, `0x20/0xb0/0xb4/0x24/0xf60` | X676C wiki `:96` + patch 0182 step 3 (**owner sign-off needed**) | `mtk_mali.c:541` |
| GPU/JOB/MMU register blocks | `0x0000/0x1000/0x2000`, JS `0x1800+n*0x80`, AS `0x2400+n*0x40` | `MaliValhallHw.h:9-15,66-69,135-141` (X676C REF, same MT6789 GPU family; **owner sign-off needed**) | `mtk_mali.c:303-343` |

The `opp-hz`/`opp-microvolt` values are copied verbatim from `fdt.dts:5782-6003`
(exact Hz and uV, no rounding) and are DTB-sourced.

## 3. GPU_ID logic

- `x6886_mali_up()` reads `GPU_ID` at `0x13000000+0x000` **after** clocks, power
  domains, reset and core power-on (`wiki:19` reports `0x90930010` on X676C).
- `0` or `~0` means an unpowered/unmapped bank: the driver logs the value and
  returns `ENXIO`. No panic, no fallback ID, no fake register value.
- Any other non-zero ID is accepted. `0x90930010` is a REF only: it enables the
  X676C r0p1 quirk writes (shader/JM/L2_MMU) and is logged, but is never a hard gate.
- `gpu_name()` decodes the product number (`0x9093` = Mali-G57) from
  `mtkmali.h:249-259`; `kern.hakios.mali_id` exposes the raw ID.

## 4. OPP / clocks

- 45 entries `opp00..opp44` from `opp-table0` (`fdt.dts:5777-6005`, phandle `0x87`
  used by the mali node at `:5742`). `opp15` has `opp-microvolt = "\0\f5"` at
  `:5858` and is stored as an explicitly invalid slot; validation skips it.
- `x6886_mali_opp_check()` validates index order, non-zero frequency, a plausible
  voltage range and strictly decreasing frequency, then reports 44 valid OPPs
  under `kern.hakios.mali_opps`.
- Clock bring-up only ungates the existing MFG reference mux and waits for its
  stable bit; it does **not** claim a frequency or a voltage. DVFS is not
  implemented (gap 2).

## 5. Driver behaviour

- `x6886_mali_init()` is a no-op unless boot-arg `mali=1`; `mali_fastmap=0` uses
  cached page-table walks, `mali_trace=1` logs each completed job.
- Bring-up is synchronous and returns `0` or a negative errno. Each failed stage
  (clock, SPM domain, bg3d gate, GPU_ID, reset, core power, job) logs its own
  line and stops; no write follows a failed read.
- JM selftest runs a real write-value job first in identity mode and then through
  four-level AArch64 4 KiB GPU page tables (`reached` sysctl: 5 = identity job,
  6 = job through tables). AS0 is left on the tables.
- `x6886_mali_submit()` runs one JM chain to completion on slot 0 (fragment),
  1 (vertex/tiler) or 2 (compute), polls JM/MMU status without a busy core, and
  resets + restores the cores and AS0 on a timeout/fault with full fault logging.
- `x6886_mali_enqueue()` / `x6886_mali_wait()` provide the in-order worker queue;
  `x6886_mali_map()` / `unmap()` / `address()` / `parameter()` expose the real
  MMU and register-backed API. `x6886_mali_stop()` masks IRQs and unmaps.
- No safe-idle stubs: there is no function that returns success without hardware
  work, no assumed GPU ID and no fake register value.
- **Log mirror (GATE 4):** all lines now start with `[X6886-mali]`. `mali_log()`
  sends every diagnostic status and every failure register dump to both the
  normal `printf` log and the live ramoops console at physical `0x480af000`
  (`docs/HANDOFF-IOS27-BOOT.md:38`, zone size `0x40000`). Writer header is
  `persistent_ram_buffer { sig=0x43474244, start, size, data at +12 }`; `start`
  is the next write offset and `size` is valid bytes, matching
  `darwin16/shim/plog.c:13-58` and `logview.c:1-10,104-110`. An absent/invalid
  zone disables only the mirror, never the normal log.
- **Bounded waits (GATE 3):** the worker loop, its empty-queue sleep and the
  full-queue sleep use `lck_mtx_sleep_deadline()` with a fresh 30 s deadline and
  an explicit `mali_worker_stop` flag set by `x6886_mali_stop()`. The sleep
  atomically enqueues on the channel before dropping the lock, so the
  `wakeup()` from `x6886_mali_enqueue()` cannot be lost; there is no literal
  `for(;;)` and no unbounded spin.
- Integration step (not done here): the integrator adds this file to the XNU
  `bsd/dev/` build and calls `x6886_mali_init()` from the board init path.

## 6. Gaps (explicit, not papered over with stubs)

1. **Bifrost write-value payload** — the selftest job's word offsets are those
   of `mtk_mali.c:354-385`; the v9 32-byte header (`wiki:151`) is used, but the
   small write-value payload encoding has not been cross-checked against the
   pinned Mesa v9 XML. An offline `find` over `/home/berucrypt00` found no
   `v9.xml` at all (only `panfrost_drm.h`), so the gap stands; a selftest failure
   is reported as stage 5/6, not hidden.
2. **DVFS / PMIC rails** — VGPU and VSRAM_GPU are left as firmware set them.
   The X676C MT6366 PWRAP rail map in `wiki:36-60` is X676C REF, not verified
   for X6886; no regulator write is attempted. The 45 OPPs are reported only.
3. **IRQ lines** — this tree has no driver-facing GIC registration API; SPI
   396-400 are recorded and the device-side masks/status are polled, as in the
   source driver. No GIC handler is claimed.
4. **Per-client address spaces** — the X676C transport leases one hardware AS
   per validated job (`wiki:135-144`); this port uses AS0 with a global lock,
   as the source driver does. Multi-client isolation remains future work.
5. **GPU_ID quirk scope** — the r0p1 shader/JM/L2_MMU quirk values are X676C
   REF and are applied only on an exact ID match. If the X6886 reports a
   different ID, the driver runs the generic Valhall path and logs the ID.
6. **No build/device validation** — offline only: no compile, no boot, no DSU,
   no flash. Correct JM completion on this phone remains to be verified.
7. **Internal offsets need owner sign-off (GATE 3)** — the SPM PWR_CON/status,
   topckgen CLK_CFG offsets, infracfg bus-protection offsets, mfgcfg gate/CG
   offsets and the JM/MMU register map are X676C reference values from
   section 2b, **not** verified in this device's DTB. Beru must sign off on
   them before boot; they are not presented as DTB-proven.

## 7. Verification performed (offline)

- `wc -l` (1382 lines), balanced braces check, no NUL bytes, grep for stale
  source-only symbols (`gMali*`, `HakiosMaliStart`, `HAKIOS_HAS_GPU_MALI_G52`)
  returned none; `mali_trace` parse target corrected to `mali_trace_queue`.
- All addresses/OPPs/IRQs above were read directly from the named dump lines;
  the OPP Hz/µV values were machine-extracted from `fdt.dts` and copied exactly.
- Both final paths compared with `sha256sum` and found identical.
- No changes outside `kernel/x6886/` and the duplicate driver under `refs/`.
