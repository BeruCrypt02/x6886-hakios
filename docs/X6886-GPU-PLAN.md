# X6886 GPU acceleration plan

Offline research only. No source, build, boot image or device change is part of this
document. Every number below comes from a local file. Each claim carries a source tag:

| Tag | Source |
| :-- | :-- |
| FDT | `device-dump/fdt.dts` (live device tree, overlays applied) |
| PROC | `device-dump/interrupts.txt`, `iomem.txt`, `bus.txt`, `modules.txt`, `dmesg.txt`, `getprop.txt` |
| DUMP | `/home/berucrypt00/otadump` (`all_files.txt`, `kernel_config.txt`, `mtk/installed-files-vendor.txt`) |
| REF | `refs/` repositories listed in the reference table |
| SPEC | `device-dump/X6886-specs.txt` |

REF evidence comes from other phones. It guides implementation order. It does not prove
that X6886 behaves the same way.

## Verified GPU hardware

| Item | Value | Source |
| :-- | :-- | :-- |
| Node | `/soc/mali@13000000`, compatible `mediatek,mali` and `arm,mali-valhall` | FDT 5737-5746 |
| MMIO | `0x13000000` to `0x13003fff`, 16 KiB | FDT, PROC `iomem.txt` |
| Architecture | Valhall v9 Job Manager, no Command Stream Frontend | FDT compatible, DUMP driver names |
| Driver module | `mali_kbase_mt6789_a16w_jm.ko`, plus `mali_mgm_mt6789_a16w_jm.ko` and `mali_prot_alloc_mt6789_a16w_jm.ko` | PROC `modules.txt`, DUMP `all_files.txt` |
| Interrupt names | GPU, MMU, JOB, EVENT, PWR | FDT 5741 |
| DT interrupt IDs | SPI 396, 397, 398, 399, 400, all level high | FDT 5740 |
| GICv3 hwirqs seen live | 428, 429, 430 (three lines named `13000000.mali`); 431 and 432 absent | PROC `interrupts.txt` 339-341 |
| OPP table | 45 points, 1100 MHz at 900 mV down to 390 MHz at 675 mV | FDT 5777-7582 |
| Malformed OPP | `opp15` (893 MHz) has a string in `opp-microvolt`, not a number | FDT 5856-5859 |
| GPU rails | `vgpu` 0.5-1.293 V ramp 200; `vsram_gpu` 0.5-1.293 V ramp 240 | FDT 3417-3423, 3579-3584 |
| GPU throttle | `gpu-power-throttling`: low-battery and OC limits at 485000 kHz, passive thermal trip at 85 C | FDT 2188-2197 |
| GPU thermal sensors | `gpu2` uses LVTS channel 9, `gpu1` uses LVTS channel 10, both 85 C passive | FDT 982-1053, 5647-5657 |
| Critical SoC trip | 113.5 C, 2.5 C hysteresis | FDT 851-866 and other zones |
| LVTS block | `lvts@1100B000`, IRQ SPI 201, hwirq 233 | FDT 5647-5657, PROC `interrupts.txt` 322 |
| gpufreq registers | 11 blocks: `0x13fbf000`, `0x10006000`, `0x1021c000`, `0x1020e000`, `0x10042000`, `0x10001000`, `0x10023000`, `0x1002b000`, `0x10000000`, `0x11c10000`, `0x10215000` | FDT 5748-5761 |
| gpufreq clocks | clk_mux, clk_ref_mux, clk_main_parent, clk_sub_parent, subsys_bg3d, clk_fhctl | FDT 5760-5761 |
| Missing from mali node | no `power-domains`, no `clocks`, no `iommus`, no `operating-points-v2` runtime state beyond the table | FDT 5737-5746 |
| M4U | `m4u@10220000`, no phandle and no `iommus` link from the GPU node | FDT 5347-5350 |
| Display IOMMU | `iommu@14016000`, phandle `0xa3`, used by display and camera nodes; the GPU does not reference it | FDT 6612-6655 |
| Userspace libraries | `libGLES_mali.so` 51,196,472 bytes, `vulkan.mali.so` 133,512 bytes, `arm.mali.platform-V2-ndk.so` 85,344 bytes, `libmtk_mali_user.so` 52,128 bytes, `libarm_mali_config_sysprops.so`, `mali_platform.config` | DUMP |
| CSF firmware | none found. No `mali_csffw.bin`, no `valhall` firmware file in `all_files.txt` | DUMP |
| Android selector | `ro.hardware.vulkan=mali` | PROC `getprop.txt` |

Linux IRQ numbering in `/proc/interrupts` for this kernel equals DT SPI ID plus 32. The
mapping checks out on the PMIC wrapper (DT 252, hwirq 284), UFS (DT 137, hwirq 169) and
DSI (DT 301, hwirq 333). The GPU line therefore shows the first three names. Confirm
which three the vendor `mali_kbase` claims before wiring XNU interrupt handlers.

### OPP table

Voltages are the raw `opp-microvolt` values divided by 1000. Row 15 carries no numeric
voltage.

| OPP | MHz | mV | OPP | MHz | mV | OPP | MHz | mV |
| :-- | --: | --: | :-- | --: | --: | :-- | --: | --: |
| 00 | 1100 | 900.0 | 15 | 893 | malformed | 30 | 722 | 712.5 |
| 01 | 1086 | 893.8 | 16 | 880 | 793.8 | 31 | 711 | 706.2 |
| 02 | 1072 | 887.5 | 17 | 868 | 787.5 | 32 | 700 | 700.0 |
| 03 | 1058 | 881.2 | 18 | 857 | 781.2 | 33 | 674 | 700.0 |
| 04 | 1045 | 875.0 | 19 | 846 | 775.0 | 34 | 648 | 700.0 |
| 05 | 1031 | 868.8 | 20 | 835 | 768.8 | 35 | 622 | 693.8 |
| 06 | 1017 | 862.5 | 21 | 823 | 762.5 | 36 | 596 | 693.8 |
| 07 | 1003 | 856.2 | 22 | 812 | 756.2 | 37 | 570 | 693.8 |
| 08 | 990 | 850.0 | 23 | 801 | 756.2 | 38 | 545 | 687.5 |
| 09 | 976 | 843.8 | 24 | 790 | 750.0 | 39 | 519 | 687.5 |
| 10 | 962 | 831.2 | 25 | 778 | 743.8 | 40 | 493 | 687.5 |
| 11 | 948 | 825.0 | 26 | 767 | 737.5 | 41 | 467 | 681.2 |
| 12 | 935 | 818.8 | 27 | 756 | 731.2 | 42 | 441 | 681.2 |
| 13 | 921 | 812.5 | 28 | 745 | 725.0 | 43 | 415 | 681.2 |
| 14 | 907 | 806.2 | 29 | 733 | 718.8 | 44 | 390 | 675.0 |

The table is stock firmware policy. It is not proof that XNU can drive any of those
frequencies.

## Reference inventory

| Reference | What it contains | Fit for X6886 |
| :-- | :-- | :-- |
| `refs/heliodarwin/wiki/MT6789-GPU-Power-Sequence.md` | Same MT6789 family. GPU base `0x13000000`, SPM `0x10006000`, infracfg `0x10001000`, topckgen `0x10000000`, apmixedsys `0x1000c000`, MFG_TOP `0x13fbf000`, PWRAP `0x10026000`. Split SWINF PWRAP offsets. VGPU `0x1508` bit 0, voltage `0x1526` bits 6:0, VSRAM_GPU `0x1bc8` bit 0, voltage `0x1bf4` bits 6:0. MFG0-3 power control at SPM `+0x308`, `+0x30c`, `+0x310`, `+0x314`. v9 job header 32 bytes, 128-byte alignment. | Best match. Register bases match FDT. GPU_ID `0x90930010` and shader mask `0x5` are X676C readings only. |
| `refs/heliodarwin/wiki/MaliMetal-Driver.md` | Full stack: Metal frontend, AIR compiler, PanVK v9 JM backend, XNU IOKit transport. Records BO limit at 128, `CTR_EL0` access fix, 64-bit page-to-address fix, FP16 gate, MPS and MRT gates. Correct pixels still unvalidated there. | Reuse as the rendering path. Treat every recorded failure as a gate to reproduce. |
| `refs/heliodarwin/MaliMetal/` | Native device, shader translation and Vulkan bridge source | Port target |
| `refs/heliodarwin/xnu/patches/0147`, `0148`, `0182`, `0178` and the 0208-0212 series | GPU power-on, MFG domains, MTCMOS, PWRAP and timestamp work | Source for XNU kernel-side bring-up |
| `refs/heliodarwin/MaliMetal/Vulkan/` | Pinned Mesa build scripts: `prepare_mesa.py`, `build_mesa.py`, `build_mesa_wsl.py`, `apply_xnu.py`, `package_icd.py`, plus the XNU kernel module glue in `MaliKmodDarwin.c` | Concrete build path for the selected backend |
| `refs/klios716/gpu/mali-sweet/` and `refs/klios716/gpu/mesa-ios/` | Mesa 25.0.7 cross-build for iOS with Gallium Panfrost, a vendored Panfrost DRM ioctl header and a libdrm stand-in. GL and GLES contexts without EGL or X11. | Fallback for OpenGL ES. It does not supply a Metal frontend. Keep it behind a separate boot arg. |
| `refs/akita/drivers/gpu/hakios.akita_mali.c` | Mali-G715 on Pixel 8a, architecture 11.8, Command Stream Frontend and `mali_csffw-r54p2.bin` | Wrong generation and wrong submission model. Do not copy its firmware path to X6886. |
| `refs/hakios-common/components/metal/docs/air.md` | AIR to NIR compiler targeting freedreno `ir3` on Adreno | Not a Mali path. It targets a different GPU vendor and instruction set. |
| `refs/hakios-mtk/xnu/files/bsd/dev/redwood_mali.c` | Mali-G52 Bifrost on X6826B, GPU base `0x13040000`, MFG config `0x13000000` | Wrong generation and wrong base. Useful for sequence structure only. |
| `refs/oosexgang-xnu27/kernel/overlay/bsd/dev/mtk_mali.c`, `loader/mtkmali.h` | Mali-G52 Bifrost bring-up on MT6768; branches `mali-native`, `malinative` | Wrong generation. Useful for register-style reference only. |

## Readiness

| Area | State | Evidence and gap |
| :-- | :-- | :-- |
| Register map and MMIO size | READY FROM DUMP | FDT and `iomem.txt` agree on `0x13000000` to `0x13003fff` |
| Clock and power register blocks | READY FROM DUMP | All 11 gpufreq blocks in FDT; HelioDarwin uses the same addresses on MT6789 |
| Power rail enable | REFERENCE ONLY | Rail registers from HelioDarwin X676C; X6886 rail ranges in FDT, no XNU code |
| Clock/DVFS policy | BLOCKED | 45 OPP points exist; MFG reference clock selection and frequency were never measured on X6886 |
| JM register programming | REFERENCE ONLY | HelioDarwin confirms JM completion on X676C; GPU_ID not read on X6886 |
| Job submission and GPU MMU | REFERENCE ONLY | HelioDarwin implements 4 KiB GPU pages and per-client address spaces; XNU CPU pages are 16 KiB |
| MediaTek IOMMU | GAP | GPU node has no `iommus`; M4U has no phandle; display IOMMU is a separate block |
| Shader translation | REFERENCE ONLY | HelioDarwin uses pinned Mesa v9 XML and PanVK. klios716 carries Mesa Panfrost GL/GLES without a Metal frontend. hakios-common path is Adreno only. |
| Metal frontend and IOKit | REFERENCE ONLY | HelioDarwin reaches backboardd and SpringBoard paths; correct frames unvalidated |
| Firmware | READY | JM needs no CSF firmware. No firmware file is missing from the dump. |
| Thermal hook | REFERENCE ONLY | 85 C passive and 113.5 C critical trips are in FDT; no XNU thermal driver |
| Display handoff | GAP | LK owns scanout and backlight. No XNU DSI or backlight driver exists in this tree. |

## Implementation order

Each step needs explicit approval before any source file changes. No step here has been
executed by this research track.

1. **Freeze the X6886 board descriptor.** Record GPU MMIO, 11 gpufreq register blocks, SPM, infracfg, topckgen, apmixedsys, PWRAP and LVTS bases from FDT. Keep HelioDarwin values in a clearly marked reference column. Gate: a diff that contains no X676C address in an active X6886 field.
2. **Verify the PWRAP interface.** HelioDarwin reports split SWINF at PWRAP `+0x880/0x884/0x894/0x8a4/0x8a8` with capability bit 5. The older MT6768 code uses WACS2 at `+0xc20/0xc24/0xc28`. Read the capability on device before choosing a transaction helper. Gate: one bounded read of a known MT6366 register with serialized access.
3. **Bring up the two GPU rails.** Enable VGPU then VSRAM_GPU through `0x1508` and `0x1bc8`. Keep firmware voltage selectors. Read both enable bits back. Do not force 0.8 V. Gate: both readbacks match the request before any JM access.
4. **Power the MFG domains.** Follow SPM MFG0, MFG1, MFG2, MFG3 power control at `+0x308`, `+0x30c`, `+0x310`, `+0x314`. Poll both SPM status registers at `+0x16c` and `+0x170`. Release infracfg bus protection with the four documented writes. Gate: all four status masks set and bus protection cleared.
5. **Start clocks without claiming a frequency.** Prepare `mfg_ref_sel`, gate BG3D in MFG_TOP, update topckgen `CLK_CFG`. Log the raw selector value. Gate: GPU register reads work and the raw MUX value is recorded.
6. **Read GPU identity on X6886.** Read `GPU_ID`, `AS_PRESENT`, `JS_PRESENT` from `0x13000000`. HelioDarwin saw `0x90930010`, `0xff`, `0x7`. Gate: X6886 values stored in the board file, not copied from X676C.
7. **Set up GPU memory.** Implement per-client GPU VA spaces, four-level AArch64 tables with 4 KiB GPU pages, guard pages, explicit cache ownership and write-combine CPU mappings. Keep CPU VM at 16 KiB. Never pass a CPU VA as a GPU VA. Gate: an identity-mapped and a page-table-mapped buffer both complete a job.
8. **Implement JM submission.** Use 32-byte v9 job headers at 128-byte alignment, job slots from `0x1800 + n*0x80` and address spaces from `0x2400 + n*0x40`. Keep one active hardware job globally. Add a five-second watchdog and status capture. Gate: a fragment job reaches a terminal record with zero JS, MMU and GPU fault bits.
9. **Wire interrupts and recovery.** Start with the three live hwirqs 428, 429, 430. Map DT SPI 396-400 explicitly. Confirm EVENT and PWR ownership before claiming them. Keep the GPU device marked unavailable after failed recovery so DMA cannot touch freed pages. Gate: a fault injection path recovers or fails closed.
10. **Port the shader path from HelioDarwin.** Use the pinned Mesa v9 XML, PanVK v9 JM work and AIR to SPIR-V translation. Do not start from `hakios-common/components/metal`, which targets Adreno `ir3`. Gate: the offline shader corpus compiles and validates before any phone boot.
11. **Port the Metal frontend and IOKit transport.** Bring over `MTLDevice` class conformance, native limits, feature queries, IOSurface staging and the render/compute ABI versions. Keep the BO suballocation fix so backboardd does not exhaust 128 live handles. Gate: backboardd creates a device and reaches a render pass without a BO allocation failure.
12. **Connect to the display path.** LK still owns scanout. Pass a finished GPU surface into the existing framebuffer path first. Add an XNU DSI driver only as a separate task. Gate: a known static pattern appears on the 1080x2400 144 Hz panel before any SpringBoard animation.
13. **Add thermal feedback last.** Tie GPU frequency caps to the 85 C passive trip and the 485 MHz power-throttling limit. Leave dynamic power-off until rail and domain control are proven. Gate: a forced thermal event reduces GPU load before the 113.5 C critical trip.

## Blockers to fix first

| Priority | Blocker | Why it stops acceleration |
| :-- | :-- | :-- |
| 1 | No XNU PWRAP and rail driver for X6886 | The GPU cannot leave the firmware-powered state safely |
| 2 | No measured MFG clock or GPU_ID on X6886 | All frequency and identity values would be copies from another phone |
| 3 | No GPU IOMMU binding in the device tree | GPU address spaces rely on the Mali MMU; MediaTek M4U is unbound here |
| 4 | No XNU shader compiler or Vulkan/Metal stack in this tree | SpringBoard has no GPU rendering backend |
| 5 | Display scanout stays in LK | Even a working GPU needs a verified path to the panel |
| 6 | `opp15` voltage is malformed | A DVFS table built from raw FDT values would contain an invalid entry |
| 7 | Three of five GPU interrupts registered live | EVENT and PWR handling cannot be assumed |
| 8 | CPU pages 16 KiB, GPU pages 4 KiB | Cache and mapping code must handle both granularities |
