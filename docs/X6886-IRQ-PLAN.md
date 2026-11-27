# X6886 (MT6789) — real interrupt handlers plan (GPU, touch, audio, charger, UFS)

Read-only research document. No code was edited, no build was run, no device was accessed, nothing was flashed. All line numbers are against the working tree at `/home/berucrypt00/hakios-x6886` (symlink to `/home/berucrypt00/x6886-hakios`) and the checked-in XNU reference at `refs/xnu-12377-hakios`.

## 0. Bottom line

* The XNU tree **does** have a real driver-facing GICv3 API: `gic_v3_spi_attach()` / `gic_v3_spi_mask()` / `gic_v3_spi_unmask()` / `gic_v3_spi_detach()` / `gic_v3_spi_count()` in `refs/xnu-12377-hakios/osfmk/arm64/gic_v3_irq.c`. Claim in the task context ("no driver-facing GIC registration") is true of the **X6886 drivers**, not of this XNU tree: no X6886 driver calls these functions.
* `IPE` **does not exist** in `refs/xnu-12377-hakios` (searched `osfmk/`, `iokit/`, `libkern/`, `bsd/`, `pexpert/` for `IPE`, `IPE_*`, `IPE[A-Z]`; no interrupt-related symbol). Do not plan around it.
* `IOInterruptEventSource` **exists** (`iokit/IOKit/IOInterruptEventSource.h:132-180`, `iokit/Kernel/IOInterruptEventSource.cpp`) but needs an `IOService` nub with `IOInterruptControllers` and a concrete `IOInterruptController` (`iokit/IOKit/IOInterruptController.h:80-108`). This tree has only the abstract framework plus `IOCPUInterruptController` (IPIs) and `PassthruInterruptController` (IPIs); there is **no GIC IOKit controller**. The event-source path is therefore not usable for these BSD-style drivers without first writing a new controller service.
* `cdev` APIs exist (`cdevsw_add()` in `bsd/dev/board/ufs.c`, `cdevsw_add_with_bdev()` there and in `bsd/dev/board/ufshc.c`), but none of the five target drivers registers a cdev today. `wakeup()` + `lck_mtx_sleep_deadline()` are the currently used in-kernel wakeup idiom (`kernel/x6886/hakios.x6886_mali.c:1169-1190, 1301-1400`).
* Touch, charger and PMIC/battery lines are **MTK EINT (GPIO) interrupts, not GIC SPIs**. There is no EINT controller driver in this XNU tree, so they cannot be attached with `gic_v3_spi_attach()` directly; a small chained EINT layer over the parent SPI is required first.

## 1. Hardware → GIC mapping, from this device's own dumps

GICv3 INTID passed to `gic_v3_spi_attach()` = DTB SPI + 32 (`GIC_INTID_SPI_MIN 32`, `gic_v3_irq.c:79`). All GIC-listed lines below are Level (DTB flags `0x4` = IRQ_TYPE_LEVEL_HIGH); pass `edge = false`.

| Driver | DTB node / property | SPI | GIC INTID (hwirq) | Observed in `device-dump/interrupts.txt` | Handler needed? |
|---|---|---|---|---|---|
| GPU `mali@13000000` | `fdt.dts:5740-5741`: `<0 0x18c 4 0>` … `<0 0x190 4 0>`, names `GPU MMU JOB EVENT PWR` | 396,397,398,399,400 | **428,429,430,431,432** | 430 (irq 339, 9.8M), 429 (irq 340, 250), 428 (irq 341, 1.18M) at lines 159-161; 431/432 absent (never fired) | Yes: 428/429/430 first; 431/432 optional |
| Audio `mt6789-afe-pcm@11210000` | `fdt.dts:5585`: `<0 0xea 4 0>` | 234 | **266** | line 147, `GICv3 266 Level Afe_ISR_Handle` (1.55M) | Yes |
| UFS `ufshci@11270000` | `fdt.dts:5663`: `<0 0x89 4 0>` | 137 | **169** | line 108, `GICv3 169 Level ufshcd` (5.43M on CPU3) | Yes, but see loader handoff warning below |
| Touch (FTS panel) | `fdt.dts:4495-4504` `focaltech,irq-gpio = <0x49 0x0c 0x00>`; framework node `fdt.dts:1620` `interrupts = <0x09 0x02>` | none — **EINT 12** (edge) | no direct GIC INTID; parent `apirq@1000b000` `fdt.dts:3105-3113` `interrupts = <0 0xf4 4 0>` → SPI 244 / INTID **276** | line 11, `mtk-eint 12 Edge mtk-tpd` (87,795); parent 276 has no line of its own | Yes, after an EINT layer exists |
| Charger SC8548 | `fdt.dts:5245-5249`: `sc8548,intr-gpios = <0x49 0x00 0x00>` | none — **EINT 0** (edge) | parent 276 (same apirq) | line 8, `mtk-eint 0 Edge sc8548 charger irq` (11) | Yes, via EINT layer |
| Charger SC8950 | `fdt.dts:5125-5127`: `sc8950,intr_gpio = <0x49 0x04 0x00>` | none — **EINT 4** (edge) | parent 276 | line 10, `mtk-eint 4 Edge sc8950_irq.5-006b` (252) | Yes, via EINT layer |
| PMIC / gauge (battery) | `fdt.dts:3228-3233` `mt6358-irq`, interrupt-parent `<0x49>`, `interrupts = <0x89 4 0x89 0>` | none — **EINT 137** (level), then mt6358 sub-IRQs | parent 276 | line 16, `mtk-eint 137 Level mt6358-irq`; sub-IRQs e.g. lines 272-280 (`mtk_gauge_*`) | Yes, via two chained layers |

Notes about the table:

* The first column of `/proc/interrupts` is Linux's *virtual* IRQ number (339/340/341, 327, 286, 29, 17, 21); it is **not** the number to hand to XNU. Use the GIC hwirq/INTID column.
* The GPU names map in DTB order: 396=GPU (428), 397=MMU (429), 398=JOB (430), 399=EVENT (431), 400=PWR (432). The three live lines prove the job/MMU/GPU sources are real; EVENT/PWR are DTB-only and must stay behind a boot-arg.
* Touch's `mediatek,touch` framework node uses EINT 9 (`fdt.dts:1620`); the live panel line in this dump is EINT 12 from the FTS `irq-gpio`. Wire EINT 12 for the panel; do not mistake EINT 9 for it.
* UFS warning: `bsd/dev/board/ufshc.c:17-20, 590-595` explicitly keeps the controller interrupt **off** (UEFI setting) and detects completions by polling `UTRLDBR`. Any IRQ mode must be opt-in and must restore the controller/loader state on quiesce (`bsd/dev/board/ufs.c:1830` discusses handing the controller back).

## 2. The API actually available in `refs/xnu-12377-hakios`

All paths below are relative to `refs/xnu-12377-hakios/`.

| API | Location | Contract / caveat |
|---|---|---|
| `kern_return_t gic_v3_spi_attach(uint32_t intid, bool edge, void (*handler)(void *, uint32_t), void *ctx)` | `osfmk/arm64/gic_v3_irq.c:123` (prototype), `:247-295` (definition) | Thread context. Returns `KERN_SUCCESS`, `KERN_INVALID_ARGUMENT` (bad INTID/CPU), `KERN_NAME_EXISTS` (double attach), `KERN_RESOURCE_SHORTAGE`. Configures group 1 NS, clears pending/active, sets priority, trigger, and `GICD_IROUTER` to the **attaching CPU** (`:289-290`). |
| Handler contract | `gic_v3_irq.c:241-245, 327-368` | Called in **IRQ context, interrupts off, before EOI**. Must only read/clear the device source and signal (e.g. `wakeup()`); no sleeping, no blocking locks, no `delay()`, no MMIO unmapping. For level IRQs the source must be quieted before return. |
| `gic_v3_spi_mask/unmask(intid)` | `gic_v3_irq.c:224-239` | Level-IRQ masking from thread context; also usable by the handler? No — handler path must not call `gic_distributor_settle()` spin wait; call device-level mask instead. |
| `gic_v3_spi_detach(intid)` | `gic_v3_irq.c:298-313` | Masks, clears pending, waits for in-flight handlers. Must be called **before** the driver unmaps MMIO or frees its context. |
| `uint64_t gic_v3_spi_count(intid)` | `gic_v3_irq.c:316-325` | Ready-made per-INTID counter for verification/sysctl. |
| Capacity | `gic_v3_irq.c:83` (`GIC_SPI_MOST 48`) | At most 48 simultaneously attached SPIs; this plan uses ≤ 7. |
| Compilation gate | `pexpert/pexpert/arm64/BCM2837.h:14` defines `GENERIC_GIC_V3_IRQ`; `-DARM64_BOARD_CONFIG_BCM2837` is in `refs/hakios-common/kernel/build.sh:252` and `kernel/deps/setup.sh:243` | Present in this build. Do not add a second definition. |
| DT discovery | `gic_v3_irq.c:455-490` | `gic_v3_irq_cpu_init()` requires a device-tree node at **`/arm-io/gic`** with `reg` = `<distributor, size, redistributors, size>` (four u64). The handset's Linux DT calls the node `/interrupt-controller` (`fdt.dts:776-787`, GICD `0x0c000000/0x40000`, GICR `0x0c040000/0x200000`). Gate 0 below verifies the XNU DT exposes the `/arm-io/gic` alias with those values. |
| Kernel install point | `gic_v3_irq.c:583` | `ml_install_interrupt_handler(NULL, 0, NULL, gic_irq_handler, NULL)` — the single IRQ entry point already exists; drivers only attach SPI slots. Unhandled IRQs are masked and counted (`gic_unhandled_count`, `:358-361`). |
| IOKit event source | `iokit/IOKit/IOInterruptEventSource.h:132-180`, `iokit/Kernel/IOInterruptEventSource.cpp` | Exists, but not wired to a GIC controller here (see §0). Not needed for BSD-style drivers. |
| `cdev` | `bsd/dev/board/ufs.c` (`cdevsw_add_with_bdev` in `hakios_ufs_init`), `bsd/dev/memdev.c:677`, `bsd/dev/monotonic.c:111` | Available for userland select/ioctl notification; none of the five target drivers has a cdev node today. Adding one is optional and separate from getting the IRQs working. |
| `IPE` | — | **Absent**. No interrupt API by that name exists in this tree. |
| EINT controller | — | **Absent**. `apirq@1000b000` (`mediatek,mt6983-eint`) is DTB-only; no XNU driver claims INTID 276. |

There is no public header exporting the GIC SPI functions in this tree (only the `.c` prototype at `:123`). New code should declare them `extern` next to the existing `ml_io_map()`/`kvtophys()` declarations (e.g. `kernel/x6886/hakios.x6886_mali.c:46-50`) or in one new shared header `kernel/x6886/x6886_irq.h`.

Source-of-truth rule: the maintained sources are `kernel/x6886/hakios.x6886_*.c`; the build tree has copies at `refs/xnu-12377-hakios/bsd/dev/hakios.x6886_*.c` (both trees currently hold a mali copy with the same content; see `docs/V9-GPU-RUNTIME-PROOF.md:260`). Any implementation must edit both or copy from `kernel/x6886/` into the build tree before building.

## 3. Per-driver wiring design

### 3.1 GPU — first, and the only driver that can use the GIC API as-is

Targets in `kernel/x6886/hakios.x6886_mali.c` (and its build-tree copy):

1. `:158-165` — replace/augment the DTB SPI constants with GIC INTIDs: `428 GPU`, `429 MMU`, `430 JOB`, `431 EVENT`, `432 PWR`, each with a comment naming the DTB SPI +32. Keep the existing `X6886_MALI_SPI_*` values for traceability.
2. Near `:46-50` — add `extern` declarations for `gic_v3_spi_attach`, `gic_v3_spi_detach`, `gic_v3_spi_count`.
3. Near `wait_job()` (`:1169-1190`) — add `static void mali_irq_handler(void *ctx, uint32_t intid)`: read `GPU_INT_RAWSTAT` / `MMU_INT_RAWSTAT` / `JOB_INT_RAWSTAT`, write the matching `*_INT_CLEAR` for the bits consumed, set a per-source `mali_irq_seen` flag and call `wakeup((caddr_t)&mali_wait_event)`. No `lck_mtx_lock`, no `delay()`, no page-table work in the handler. Record counts into a static array for a sysctl.
4. `wait_job()` (`:1175-1190`) — replace the 20×10 µs spin with `lck_mtx_sleep_deadline()` on `&mali_wait_event`, keeping the absolute deadline and the register recheck. This makes the GPU completion event-driven instead of polled.
5. `x6886_mali_init()` (`:1426-1442`) — after `x6886_mali_up()` succeeds, parse `mali_irq` (default **0** = polling, as today); when 1, call `gic_v3_spi_attach(428/429/430, false, mali_irq_handler, NULL)` and log each return value. Treat `KERN_NAME_EXISTS`/`KERN_RESOURCE_SHORTAGE` as clean errors, never partial success. Keep 431/432 behind a separate `mali_irq_extra=1` gate because they never fired.
6. `x6886_mali_stop()` (`:1450-1480`) — detach each attached INTID **before** `wr(GPU_INT_MASK, …)`/`ml_io_unmap()`, and set `mali_worker_stop` + `wakeup()` as it does now.

Verification: new `kern.hakios.mali_irq_counts` sysctl reading `gic_v3_spi_count()`; ramoops lines naming INTID and return code; with `mali_trace=1` job completion timestamps must still advance; `gic_unhandled_count` must not grow.

### 3.2 Audio — straightforward second GIC SPI

Targets in `kernel/x6886/hakios.x6886_audio.c`:

1. Constants near `:98-168` — add `X6886_AFE_GIC 266` (SPI 234 +32), with the `fdt.dts:5585` and `interrupts.txt:147` evidence in a comment.
2. New handler after `x6886_afe_dma_check()` (`:239-265`) — read AFE IRQ status, write AFE IRQ clear (REF offsets from `mt6789-reg.h:2291-2296`, already cited in `docs/X6886-STUB-REMEDIATION.md`; only under `x6886_audio_ref=1`), set flag + `wakeup()` the DMA waiter.
3. `x6886_audio_init()` (`:372-400`) — after `x6886_afe_ready()` and only when both `x6886_audio_ref=1` and new `x6886_audio_irq=1`, attach INTID 266 level-triggered; detach in the audio stop path.

Verification: `gic_v3_spi_count(266)` sysctl and AFE status/clear readback; no IRQ storm (count grows only while audio DMA runs).

### 3.3 UFS — opt-in only, keep the poller as fallback

Targets in `refs/xnu-12377-hakios/bsd/dev/board/ufshc.c` (note: this file lives only in the build tree):

1. Top comment `:17-20` — document the new opt-in mode explicitly (UEFI's interrupt-off default is unchanged).
2. Near the poller (`:590-680`) — add `ufs_irq_handler(void *, uint32_t)`: read `UFSHC_IS` (`ufshc.c:85`), write `UFSHC_IE`/clear only in IRQ mode, derive completed slots from `UTRLCNR` (`:95`), set `hc_done` under `hc_lock`? **No** — IRQ context cannot take `hc_lock`; instead set an atomic bitmask `hc_irq_done` and `wakeup(&hc_waiting)`, letting the existing `hc_poller()` at `:603` collect it under the lock. Keep `hc_complete()` (`:643-680`) unchanged apart from checking the atomic flag.
3. Init path around `:1315-1335` (where `hc_poller` is started) — parse `ufs_native_irq` (default 0); when 1, attach INTID **169**, `edge=false`, before the poller starts. Keep `hc_spin_us`/poller as the fallback and for the loader-handoff path.
4. `hakios_ufs_quiesce()` (called from `loader_return`, see `board/ufs.c:1830`) — detach 169 and restore `UFSHC_IE` to UEFI's value before returning control to the loader.

Verification: `kern.hakios.ufs_native_fallbacks` must not increase; `hc_poller_polls`/`hc_polls` sysctls (`ufshc.c:175, 600`) should drop sharply with `ufs_native_irq=1`; `gic_v3_spi_count(169)` should track reads.

### 3.4 Touch, charger, battery — need an EINT layer first, not a GIC attach

`gic_v3_spi_attach()` can only claim the EINT controller's **parent** SPI 244 → INTID **276** (`fdt.dts:3105-3113`). The parent does not appear as a separate line in `interrupts.txt`, and no XNU driver owns it. Therefore:

1. New file `kernel/x6886/hakios.x6886_eint.c` (+ build-tree copy under `bsd/dev/`): attach INTID 276 (`edge=false`) once; in its handler read the `apirq@1000b000` (`0x1000b000`, `fdt.dts:3108`) EINT status registers, clear the handled bits, and dispatch to registered per-EINT callbacks. Register/unregister functions must be thread-context; callbacks run in IRQ context and may only set a flag + `wakeup()`. **Register offsets for this controller are not DTB-proven and are not present in the local XNU sources** — they must come from an owner-reviewed reference before any write; until then the file must only claim/observe and count, never program the EINT block.
2. Touch (`kernel/x6886/hakios.x6886_touch.c`): register EINT **12** callback in `x6886_touch_init()` (`:609-699`, after `x6886_ready()`), replacing the `pin_get(FTS_PIN_IRQ)` spin at `:616-645`; callback wakes a channel that `tcm_thread()` waits on. The existing "IRQ line asserted before first read" log at `:588` becomes the first callback event. Keep the bounded tick count and `x6886_touch_stop()` (`:698`) unregistering the callback. Boot arg `x6886_touch_irq=1` (default: polling).
3. Charger (`kernel/x6886/hakios.x6886_charger.c`): register EINT **0** (SC8548) and EINT **4** (SC8950) callbacks; each sets a flag that wakes the existing bounded verification loop around `x6886_chg_read_status()` (`:410-450`). Do not write SC8548 registers — `x6886_chg_sc8548_probe()` at `:454+` documents that no register map exists. Boot arg `x6886_chg_irq=1`.
4. Battery (`kernel/x6886/hakios.x6886_battery.c`): register EINT **137** (mt6358-irq, `interrupts.txt:16`) and, only if a PMIC sub-IRQ decoder is later ported, the gauge sub-IRQs at `/proc` lines 272-280. Wake `batt_thread()` (`:156-214`) instead of the current fixed `delay(1000000)`. This is last: it depends on both the EINT layer and a PMIC register map that is currently REF-only (`x6886_batt_pmic=1` gate).

Verification for EINT work: a sysctl counter per EINT number; callback counts must match the live `/proc/interrupts` deltas when later compared on-device; no unhandled-GIC growth.

## 4. Ordered implementation and verification plan

| Step | Change | Gate / verification (no device access until a separate, explicitly approved bring-up) |
|---|---|---|
| 0 | Confirm statically that the XNU device tree given to `gic_v3_irq_cpu_init()` exposes `/arm-io/gic` with `reg` = GICD `0x00c00000/0x40000`, GICR `0x00c04000/0x200000` (Linux names it `/interrupt-controller`, `fdt.dts:776-787`). Confirm `GENERIC_GIC_V3_IRQ` is compiled via `BCM2837.h:14` + `build.sh:252`. | If `/arm-io/gic` is absent, add the alias in the loader-provided DT (target: loader DT construction in `build/loader/mtkloader.c`, inspect around the GIC setup at `:2228`/`:2914`); do **not** patch the Linux dump. Stop if the node cannot be established. |
| 1 | Add `kernel/x6886/x6886_irq.h` with the extern prototypes and `X6886_*_GIC` INTID constants; copy to build tree. | Syntax check only; no behavioural change. |
| 2 | GPU: steps 3.1.1–3.1.6. | `mali_irq=1` sysctl counters; `mali_irq=0` must reproduce today's exact polling behaviour (regression gate). |
| 3 | Audio: steps 3.2.1–3.2.3. | `x6886_audio_ref=1 x6886_audio_irq=1` only; REF offsets remain opt-in. |
| 4 | UFS: steps 3.3.1–3.3.4. | `ufs_native_irq=0` unchanged; `=1` reduces poller counts; quiesce restores UEFI state. |
| 5 | EINT layer (observe/claim only until register offsets are owner-approved), then touch, charger, battery callbacks. | Per-EINT counters; no GIC `unhandled_count` growth. |
| 6 | Optional userland notification via `cdevsw_add()` + `select`/`wakeup` for touch/charger events. | Only after steps 2–5; no cdev exists today. |

Cross-cutting rules: every new boot arg defaults to the current polling behaviour; every attach is paired with a detach before MMIO unmap; handlers never sleep; on `KERN_NAME_EXISTS`/`KERN_RESOURCE_SHORTAGE` the driver logs and fails cleanly; all diagnostics use the shared ramoops mirror at `0x480af000`; no flashing, no on-device testing, no build artifacts are part of this document.

## 5. Explicit gaps (state these, do not invent APIs)

* No `IPE` API exists in `refs/xnu-12377-hakios`.
* No GIC IOKit interrupt controller exists there; `IOInterruptEventSource` is present but has no provider nub for these SPIs.
* No MTK EINT controller driver exists; touch/charger/PMIC lines cannot be registered with `gic_v3_spi_attach()` until the parent INTID 276 is claimed and EINT status/clear offsets are owner-approved.
* No public header declares `gic_v3_spi_attach()`; `extern` declarations are required.
* No cdev node exists for GPU/touch/audio/charger today — cdev is an optional later userland path, not a prerequisite for real handlers.
* EVENT/PWR GPU SPIs 399/400 (INTIDs 431/432) are DTB-only and have never fired on this handset; keep them disabled by default.
