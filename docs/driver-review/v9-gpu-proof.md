# X6886 Mali v9 — on-device runtime proof (runbook)

**Status: OFFLINE READINESS ONLY.** No boot, no DSU, no flash and no device pstore
capture were performed. This document is the exact evidence checklist to run
*after* a future boot of the existing image. It does not change the driver or
any boot image. Every command below is read-only on the device.

## Evidence used

| Claim | Evidence |
|---|---|
| Driver serialises all diagnostics with `[X6886-mali]` to `printf` **and** pstore | `drivers/gpu/hakios.x6886_mali.c:121-137` (`mali_log()`) |
| Boot args are parsed by XNU `PE_parse_boot_argn()` | `hakios.x6886_mali.c:1431-1439` |
| `reached` stages and `case 1 -> 5`, `case 2 -> 6` | `hakios.x6886_mali.c:783,803,837,892,896-906` |
| Sysctl names | `hakios.x6886_mali.c:172-176,506-507` |
| pstore sub-window and ring header | `hakios.x6886_mali.c:60-119` |
| pstore region provenance | `devices/x6886/evidence/fdt.dts:92` (`ramoops.mem_address=0x48090000`, `mem_size=0xe0000`, `pmsg_size=0x80000`, `console_size=0x40000`) and `devices/x6886/evidence/fdt.dts:712-715` (`mblock-2-pstore`, `reg <0 0x48090000 0 0xe0000>`) |
| Console zone is `0x480af000`, not `0x480b0000` | `devices/x6886/board.toml:28-36` (non-zero `ftrace_size=0x1000` shifts it 4 KiB down) and `docs/HANDOFF-IOS27-BOOT.md:38,51` |
| XNU boot-args assembly and printout | `build/loader/mtkloader.c:150-151,183-185,3000-3021` |
| Stock GPU is real hardware, not a stub | `devices/x6886/evidence/modules.txt:199-203` (`mali_kbase_mt6789_a16w_jm 991232` with 74 users), `devices/x6886/evidence/getprop.txt:333` (`init.svc.gpu: [running]`), `:2363` (`ro.hardware.vulkan: [mali]`) |
| GPU block base/IRQ | `devices/x6886/evidence/fdt.dts:5737-5741` (`mali@13000000`, `arm,mali-valhall`, SPI 396-400), `devices/x6886/evidence/iomem.txt:117`, `devices/x6886/evidence/interrupts.txt:159-161` |

The stock `devices/x6886/evidence/dmesg.txt` is a late-window capture (starts at uptime
31277 s) and contains **no** GPU lines; it cannot be used as proof either way.
The loaded-module and property evidence above is the stock-side proof.

---

## 1. What to set as boot args

The driver is opt-in. `PE_parse_boot_argn("mali", &on, ...)` at
`hakios.x6886_mali.c:1437-1439` returns before any register is touched unless
`mali=1` is present. These values belong in the **XNU** `commandLine`, not in
Linux's `chosen/bootargs` (`fdt.dts:92`).

| Argument | Value to boot with | Why / source |
|---|---|---|
| `mali` | `1` | **Required.** Enables the driver at all (`:1437`). Without it `kern.hakios.mali` stays `0` and no `[X6886-mali]` line appears. |
| `mali_hwctl` | `0` (default) first; `1` only after owner sign-off | With `0` the driver only *reads* the DTB-unproven clock/power gates and returns `ENXIO` if the firmware left them off (`:427-430,764-836`). With `1` it writes the X676C-reference SPM/topckgen/infracfg/mfgcfg offsets. Every such write is behind `if (mali_hwctl)` (`:764,787,815`). |
| `mali_trace` | `1` (optional) | One `[X6886-mali] job ...` line per completed queued job (`:1436,1341-1351`). Useful for proving the queue, not required for stage 6. |
| `mali_fastmap` | any; **not proof** | Parsed at `:1435` but unused in v9: `MEMATTR_G57` is written unconditionally at `:731-732,1078,1089,1283`. Do not claim cached-walk behaviour from this arg. |

Recommended first boot (read-only survey; no unproven gate write):

```
-v debug=0x8 serial=3 cpus=6 mali=1 mali_trace=1
```

If (and only if) the survey logs "gate is closed ... mali_hwctl=1 needed", the
owner-approved second attempt adds `mali_hwctl=1`:

```
-v debug=0x8 serial=3 cpus=6 mali=1 mali_hwctl=1 mali_trace=1
```

`-v debug=0x8 serial=3` are the loader's standard XNU flags
(`build/loader/mtkloader.c:183,3000-3007`; `docs/HANDOFF-IOS27-BOOT.md:80`).
On this tree the extra args come from the loader's `XNU_EXTRA_ARGS` compile
define (default `cpus=1`, `mtkloader.c:150-151`) or the board `[boot] bootargs`
(`refs/oosexgang-xnu27/docs/boot-args.md:7-11`). The loader prints the exact
command line as `boot-args: ...` on the panel and into the same pstore zone
(`mtkloader.c:3020-3021`) — **confirm the boot args from that line before
interpreting any sysctl**. This offline task did not rebuild the loader or
alter boot args.

---

## 2. Sysctls to read, and what `reached` 5 and 6 mean

On the booted XNU shell, read all five in one command:

```
sysctl kern.hakios.mali kern.hakios.mali_id kern.hakios.mali_jobs kern.hakios.mali_failed kern.hakios.mali_opps
```

| Sysctl | Backing variable | Pass criterion |
|---|---|---|
| `kern.hakios.mali` | `reached` (`:172-173`) | **must be `6`** |
| `kern.hakios.mali_id` | `gpu_id` (`:174`) | non-zero and not `0xffffffff`; `0x90930010` is the X676C reference, other non-zero IDs are accepted (`:841-846`) |
| `kern.hakios.mali_jobs` | `jobs_done` (`:175`) | `>= 2` after bring-up (the two selftest jobs), and increasing as queued work completes |
| `kern.hakios.mali_failed` | `jobs_failed` (`:176`) | stays `0`; any increment means a job/fault-recovery path ran |
| `kern.hakios.mali_opps` | `valid_opp_count` (`:506-507`) | `44` (45 DT entries minus the corrupt `opp15`) |

`reached` is a one-way stage counter during init (`:783,803,837,892,898-899`)
and is reset to `0` by `x6886_mali_stop()` (`:1455`). Its exact meaning:

| Value | Meaning | Source |
|---|---|---|
| `0` | No opt-in (`mali=1` absent), or driver stopped. No register touched. | `:1437-1439,1455` |
| `1` | MFG reference clock survey passed. | `:770-783` |
| `2` | Four MFG power domains ready (or surveyed ready with `mali_hwctl=0`). | `:785-803` |
| `3` | bg3d block gate ready. | `:805-837` |
| `4` | GPU detected, reset done, quirks applied, L2/tiler/shader cores powered. | `:841-892` |
| **`5`** | The **identity-mode** write-value job completed, but the second job through the GPU's own four-level page tables did **not**. `x6886_mali_ready()` is still false; init returns `ENXIO` and logs `no job completed through page tables (stage 5)`. **Not a pass.** | `:727-741,896-906,920-924` |
| **`6`** | Both selftest jobs completed: the GPU executed the write-value job *through GPU page tables* (`FIRST_JOB_GPU`, AArch64 4 KiB, `MEMATTR_G57`). Only now is `x6886_mali_ready()` true and real JM submission accepted. **This is the runtime proof.** | `:730-735,897-907,920-924` |

A stable `kern.hakios.mali=6` with `mali_failed=0` is the minimum evidence that
the GPU is really working. Sysctls alone do not prove the *path*; the log lines
in section 3 prove which path was taken.

---

## 3. Exact `[X6886-mali]` lines expected

`mali_log()` prefixes every line with `[X6886-mali] ` and sends it to both the
console and pstore (`:121-137`). On a successful `mali=1` boot the lines appear
in this order (values in `<...>` are read from the hardware, not asserted):

1. `:532-533`
   ```
   [X6886-mali] 44 valid OPPs from opp-table0, 390000000..1100000000 Hz; opp15 skipped
   ```
2. Only when `mali_hwctl=0` (`:768`):
   ```
   [X6886-mali] mali_hwctl=0: read-only survey, no unproven clock writes
   ```
3. `:852-854` (GPU_ID is read, never assumed; r0p1 shown for the reference ID):
   ```
   [X6886-mali] GPU_ID 0x90930010 (Mali-G57 r0p1), L2 <hex>, shader <hex>, MMU <hex>, AS_PRESENT <hex>, JS_PRESENT <hex>, MFG clock <hex>
   ```
4. `:871-873` (`G57 set: yes` for product `0x9093`; the MMU mask is applied only
   to exact `0x90930010`):
   ```
   [X6886-mali] quirks applied (G57 set: yes), SHADER_CONFIG <hex>, JM_CONFIG <hex>, L2_MMU_CONFIG <hex>, MMU features <hex>
   ```
5. `:893-894`:
   ```
   [X6886-mali] all cores powered, shader mask <hex> (JS slots <hex>, AS slots <hex>)
   ```
6. `:702-704` — the identity-mode selftest. `header 0x10004` is
   `(JOB_TYPE_WRITEVALUE << 1) | (JOB_INDEX << 16)` with **no** Bifrost Is64b
   bit0; `word 0xc0ffee01` is the immediate the GPU wrote to page+0x800:
   ```
   [X6886-mali] first job, identity mode: JOB_INT_RAWSTAT 0x2, JS1_STATUS 0x0, AS_FAULTSTATUS 0x0, header 0x10004, word 0xc0ffee01 (GPU ran it)
   ```
7. `:702-704` — the same job through GPU page tables. This line is the actual
   proof of MMU translation:
   ```
   [X6886-mali] second job, through GPU page tables: JOB_INT_RAWSTAT 0x2, JS1_STATUS 0x0, AS_FAULTSTATUS 0x0, header 0x10004, word 0xc0ffee01 (GPU ran it)
   ```
   `JOB_INT_RAWSTAT 0x2` = slot-1 done bit set, bit 17 (fault) clear;
   `0x20002` with bit 17 set, or `word 0x0` with `(NOT WRITTEN)`, is a failure.
8. `:1445` — the final line. This is the single best grep target:
   ```
   [X6886-mali] bring-up returned 0 at stage 6 of 6
   ```

With `mali_trace=1`, later queued jobs add (`:1347-1350`):

```
[X6886-mali] job <seq> f|v q <us> b <us> e <us> us
```

`f` = fragment slot 0, `v` = vertex/tiler slot 1.

### Stop lines — any of these means "not working", with its stage

| Exact format string (after the `[X6886-mali] ` prefix) | Line |
|---|---|
| `GPU OPP table from fdt.dts failed validation; stopping` | `:1441` |
| `mali_hwctl=0: read-only survey, no unproven clock writes` followed by `power domain %s is off (status %#x/%#x); mali_hwctl=1 needed` | `:768,287` |
| `topckgen MFG clock not stable (CLK_CFG_3 %#x)` | `:779` |
| `power domain %s did not come up (status %#x/%#x)` | `:790` |
| `bg3d clock gate is closed (status %#x); mali_hwctl=1 needed` / `bg3d clock gate did not open (status %#x)` | `:832,818` |
| `cannot map topckgen/infracfg/spm` / `cannot map mfgcfg or GPU registers` | `:756,809` |
| `GPU_ID %#x, GPU bank unpowered; stopping (no assumed ID)` | `:843` |
| `GPU_ID %#x did not finish its reset` | `:862` |
| `no L2 or shader cores present (L2 %#llx, shader %#llx)` / `cores did not power up (ready L2 %#llx, tiler %#llx, shader %#llx)` | `:879,885` |
| `no page for the first jobs` / `no memory for page tables` | `:724,737` |
| `no job completed through page tables (stage %u)` → `reached=5` | `:903` |
| `JM slot %u busy (NEXT %#x, CURRENT %#x)` (busy precheck returned `EBUSY`) | `:1235` |
| `JM slot %u did not finish: JOB_INT_RAWSTAT %#x, JS_STATUS %#x, MMU %#x, fault %#x at %#llx` plus `  GPU_FAULT_STATUS %#x at %#llx` and the buffer description lines | `:1267-1272` |
| `recovery: cores did not power up again` | `:1280` |

`bring-up returned <errno> at stage <n> of 6` is always the last init line;
match it against stage 6 / return 0 before declaring success.

---

## 4. How to read pstore zone `0x480af000`

The driver maps a **0x1000-byte sub-window** at physical `0x480af000`
(`:68-69,78-83`). That address is inside the DTB-proven pstore region
`0x48090000 + 0xe0000` (`fdt.dts:92,712-715`); its exact offset is the
task/board-supplied console zone, confirmed by the live ramoops parameters
(`board.toml:28-36`, `docs/HANDOFF-IOS27-BOOT.md:38`). It is **not**
`0x480b0000`: `ftrace_size=0x1000` shifts the console 4 KiB down.

### Header contract (shared by all ten X6886 drivers; `:60-119`)

| Offset | Content | Value / rule |
|---|---|---|
| `+0` | signature | `0x43474244` ("DBGC"; little-endian bytes `44 42 47 43`). A valid header is **never reinitialised**, so a mali-first boot keeps other drivers' logs (`:100-108`). |
| `+4` | write cursor | next write position, modulo capacity |
| `+8` | **ring capacity** | `0x1000 - 12 = 0x1ff4`. This word is **never a valid-byte count**. |
| `+12` | data | newline-terminated lines, wrapped with `(start + i) % size` (`:113-117`) |

Live console channel (before reboot):

```
dmesg | grep -a '\[X6886-mali\]'
```

After a warm reset into Android, the loader/XNU log survives as pstore
(`build/loader/mtkloader.c:1-17`, `build/loader/mtkdbg.h:1-7`):

```
adb shell 'cat /sys/fs/pstore/console-ramoops-0 | grep -a "\[X6886-mali\]"'
adb shell 'cat /sys/module/ramoops/parameters/mem_address /sys/module/ramoops/parameters/record_size /sys/module/ramoops/parameters/console_size /sys/module/ramoops/parameters/ftrace_size /sys/module/ramoops/parameters/pmsg_size'
```

Expected ramoops parameters from the live device capture
(`docs/HANDOFF-IOS27-BOOT.md:38`): console base `0x480af000`, size `0x40000`,
`record_size=0x1000`, `console_size=0x40000`, `ftrace_size=0x1000`,
`pmsg_size=0x80000`, `ecc=0`. The first 16 raw bytes of the console zone show
sig, cursor and capacity (optional; only if the device permits `/dev/mem`):

```
adb shell 'dd if=/dev/mem bs=1 skip=$((0x480af000)) count=16 2>/dev/null | xxd'
```

Interpret the `+8` word as capacity `0x1ff4`, not as "bytes written". If the
signature is absent, the XNU driver's `ml_io_map()` of the zone failed and only
the console path exists — `mali_log()` still prints to `dmesg`.

---

## 5. Acceptance checklist

All of these must be true; a single miss means the GPU is not yet proven:

- [ ] Loader `boot-args:` line contains `mali=1` (and `mali_hwctl=1` only if approved).
- [ ] `kern.hakios.mali == 6` (not 5; 5 explicitly fails the page-table job).
- [ ] `kern.hakios.mali_id` non-zero, not `0xffffffff`.
- [ ] `kern.hakios.mali_jobs >= 2`, `kern.hakios.mali_failed == 0`, `kern.hakios.mali_opps == 44`.
- [ ] Both `first job, identity mode` and `second job, through GPU page tables` lines show `(GPU ran it)` with `JOB_INT_RAWSTAT 0x2`, `AS_FAULTSTATUS 0x0`, `header 0x10004`, `word 0xc0ffee01`.
- [ ] Final line is `[X6886-mali] bring-up returned 0 at stage 6 of 6`.
- [ ] Same lines are present in `/sys/fs/pstore/console-ramoops-0` after warm reboot (header sig `0x43474244`, capacity word `0x1ff4`).
- [ ] With `mali_trace=1`, queued `job ... f|v` lines advance and `mali_failed` stays 0.

If `reached=5`, the MMU/page-table path is the suspect: check `AS_FAULTSTATUS` in the
second job line and the `GPU_FAULT_STATUS` / `GPU_FAULT_ADDRESS` failure lines.
(The driver also snapshots the four-level walk in `last_failure.walk[]` at
`:1266`, but that snapshot is not printed.)
If `reached` is 1-4, match the last `[X6886-mali]` line against the table above;
do not enable `mali_hwctl=1` without owner sign-off on the X676C-reference
gate offsets (`drivers/gpu-driver.md` sections 2b and 6, gap 7).

---

## 6. Offline proof of readiness (what was actually run here)

```
cd /home/berucrypt00/hakios-x6886
./tools/check-mali-v9.py
```

Result: **15/15 PASS**, exit 0, source SHA256
`45f558dbde4ede6eab26854ce244d709e7dad41815f6d6c348ce89ea9fcac0b0`
(the same hash as `refs/xnu-12377-hakios/bsd/dev/hakios.x6886_mali.c`). The
script checked: no Is64b bit; `PTE_VALID 0x403` table descriptors; leaf NX;
`JS_FLUSH_ID_NEXT` before START; busy precheck; recovery flush before UPDATE;
`LOCK` then `FLUSH_MEM(5)`; unconditional `MEMATTR_G57`; affinity from
`SHADER_READY`; every unproven gate write under `mali_hwctl`; G57 quirk scope;
pstore header contract; sysctl/`reached` semantics; JM-only/no fake ID; bounded
waits. Each of the 15 rules was also mutated once in a temporary copy and
correctly returned a non-zero exit with that rule marked `FAIL`.

**This is static evidence only.** It proves the v9 fixes are present in the
source; it does **not** prove the GPU executed a job. Only the on-device
checks in sections 2-5 can do that. No driver, boot image or device file was
modified by this work.
