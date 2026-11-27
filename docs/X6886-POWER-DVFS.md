# X6886 GPU power, rails, clocks and DVFS — offline evidence and safe bring-up

Offline research only. No source file outside this document was edited, no build was
run, no boot/flash/device access occurred and nothing was written to the OTA dump at
`/home/berucrypt00/otadump` (read-only). This document is a **specification of a safe
path**, not a claim that the path has been executed on X6886.

## 0. Provenance rules

| Tag | Meaning |
| :-- | :-- |
| FDT | `device-dump/fdt.dts` — live device tree from this X6886, overlays applied. Line numbers are from that file. |
| PROC | `device-dump/interrupts.txt`, `iomem.txt`, `modules.txt`, `dmesg.txt` — live /proc captures from this X6886. |
| DUMP | `/home/berucrypt00/otadump` — `all_files.txt`, `kernel_config.txt`, `mtk/installed-files-vendor.txt`, partition trees. |
| REF | `refs/heliodarwin/wiki/MT6789-GPU-Power-Sequence.md` and patches 0147/0178/0182, measured on the **X676C**, a different phone. Also `refs/oosexgang-xnu27` (MT6768 X6826B). Unverified on X6886. |
| TREE | `kernel/x6886/hakios.x6886_mali.c` and `kernel/x6886/PORT-GPU.md` — local design/implementation state. Describes intent, not hardware proof. |

Rules used throughout:

- A value is **DTB-verified** only if it literally appears in `fdt.dts` (or a PROC
  capture of this device). A block base being in the DTB does **not** verify any
  offset inside that block.
- Every register offset inside PWRAP, SPM, infracfg, topckgen, MFG_TOP and the Mali
  JM/MMU blocks is REF-only. None of those offsets appears in this X6886 FDT.
- REF values guide ordering and readback design; they are never promoted to X6886
  facts. "Confirmed on X676C" is not evidence about X6886.
- Old docs contain two drifted claims corrected here: the GPU OPP table ends at
  `fdt.dts:6005`, not 7582; and the FDT trip hysteresis is `0x7d0` = 2000 mC = **2.0
  C**, not 2.5 C. The raw FDT value is authoritative.

## 1. DTB-verified power and clock topology

### 1.1 Nodes and bindings

| Item | FDT-verified value | Source |
| :-- | :-- | :-- |
| GPU node | `mali@13000000`, compatible `mediatek,mali` + `arm,mali-valhall`; `reg <0 0x13000000 0 0x4000>` (16 KiB) | FDT 5737-5739; PROC `iomem.txt` `13000000-13003fff : 13000000.mali` |
| GPU IRQs | SPI 396, 397, 398, 399, 400 (`0x18c`..`0x190`), all level-high; names GPU, MMU, JOB, EVENT, PWR | FDT 5740-5741 |
| Live GPU IRQs | GICv3 hwirq 428, 429, 430 only; 431/432 (SPI 399/400) absent from `/proc/interrupts` | PROC `interrupts.txt` 339-341 |
| OPP binding | `operating-points-v2 = <0x87>` → `opp-table0` | FDT 5742, 5777-5779 |
| Cooling binding | `#cooling-cells = <2>`; `gpu2` cooling map points at phandle `0x1d` (the GPU) with `contribution = <0x400>` (1024) | FDT 5743, 1013-1019 |
| GED chain | mali `ged-supply = <0x88>` → `ged` node `gpufreq-supply = <0x8f>` → `gpufreq` phandle | FDT 5744, 5771-5775, 5760 |
| What is absent | mali node has **no** `power-domains`, **no** `clocks`, **no** `iommus`, **no** reset property. Rail and register ownership sits on the separate `gpufreq` node. | FDT 5737-5746 |
| gpufreq | `mediatek,gpufreq`; 11 x 4 KiB register blocks | FDT 5748-5761 |
| gpufreq supplies | `_vgpu-supply = <0x8a>` and `_vsram_gpu-supply = <0x8b>` (note the leading underscore in the property names); `fhctl-supply = <0x8d>`; `gpufreq-wrapper-supply = <0x8e>`; `nvmem-cells = <0x8c>` (efuse) | FDT 5754-5759 |
| gpufreq wrapper | `mediatek,gpufreq-wrapper`; `gpufreq-version = <2>`, `dual-buck = <0>`, `gpueb-support = <0>` | FDT 5763-5769 |
| Rail aliases | `mt6358_vgpu_reg` → `.../mt6358regulator/buck-vgpu`; `mt6358_vsram_gpu_reg` → `.../mt6358regulator/ldo-vsram-gpu` | FDT 9096, 9114 |
| Other GPU DVFS nodes | `g3d_dvfs@13fbb000`, `g3d_config@13fbc000`, `g3d_config@13fbd000`, each 4 KiB | FDT 6012-6024 |

### 1.2 The 11 gpufreq register blocks (FDT 5750-5751)

| # | Base | Size | `reg-names` |
| --: | :-- | :-- | :-- |
| 0 | `0x13fbf000` | 0x1000 | `mfg_top_config` |
| 1 | `0x10006000` | 0x1000 | `sleep` (SPM / `power-controller@10006000`) |
| 2 | `0x1021c000` | 0x1000 | `nth_emicfg_reg` |
| 3 | `0x1020e000` | 0x1000 | `infracfg` |
| 4 | `0x10042000` | 0x1000 | `fmem_ao_debug_ctrl` |
| 5 | `0x10001000` | 0x1000 | `infracfg_ao` |
| 6 | `0x10023000` | 0x1000 | `infra_ao_debug_ctrl` |
| 7 | `0x1002b000` | 0x1000 | `infra_ao1_debug_ctrl` |
| 8 | `0x10000000` | 0x1000 | `topckgen` |
| 9 | `0x11c10000` | 0x1000 | `efuse` |
| 10 | `0x10215000` | 0x1000 | `infra_bcrm` |

`clocks` / `clock-names` on gpufreq (FDT 5752-5753): `clk_mux`, `clk_ref_mux`,
`clk_main_parent`, `clk_sub_parent`, `subsys_bg3d`, `clk_fhctl`. The alias
`mfg_top_config_clk = "/syscon@13fbf000"` is at FDT 9011. Other DTB-verified block
bases used by the bring-up path: `pwrap@10026000` (3219-3222),
`infracfg_ao@10001000` (2359), `topckgen@10000000` (2354),
`power-controller@10006000` (1629-1631), `lvts@1100B000` (5647-5650). `iomem.txt`
confirms `10000000`, `10006000`, `10026000`, `1100b000` and `13000000` mappings.

### 1.3 GPU rails (FDT)

| Rail | Node | Min uV | Max uV | Enable ramp raw | Other properties | Phandle |
| :-- | :-- | --: | --: | --: | :-- | :-- |
| `vgpu` | `buck-vgpu` | 500000 (`0x7a120`) | 1293000 (`0x13bdb6`) | 200 (`0xc8`) | `regulator-allowed-modes = <0 1>`; **not** always-on | `0x8a` |
| `vsram_gpu` | `ldo-vsram-gpu` | 500000 (`0x7a120`) | 1293000 (`0x13bdb6`) | 240 (`0xf0`) | no allowed-modes; **not** always-on | `0x8b` |

Sources: FDT 3417-3424 and 3579-3585; aliases FDT 9096/9114. Neither rail is
`regulator-always-on`, so do not assume firmware left them enabled — verify.
The allowed range goes to 1.293 V while the top OPP is 0.900 V; the range is a
capability, not a target. Never force a voltage (in particular never force 0.800 V).

### 1.4 OPP table (FDT 5777-6005)

`opp-table0`, phandle `0x87`, referenced by the mali node at FDT 5742. 45 entries
`opp00`..`opp44`. Values below are exact raw `opp-hz` / `opp-microvolt` divided to
Hz and uV. `opp15` carries a string, not a number, and must be treated as an
explicit invalid slot.

| OPP | MHz | uV | OPP | MHz | uV | OPP | MHz | uV |
| --: | --: | --: | --: | --: | --: | --: | --: | --: |
| 00 | 1100 | 900000 | 15 | 893 | **malformed** | 30 | 722 | 712500 |
| 01 | 1086 | 893750 | 16 | 880 | 793750 | 31 | 711 | 706250 |
| 02 | 1072 | 887500 | 17 | 868 | 787500 | 32 | 700 | 700000 |
| 03 | 1058 | 881250 | 18 | 857 | 781250 | 33 | 674 | 700000 |
| 04 | 1045 | 875000 | 19 | 846 | 775000 | 34 | 648 | 700000 |
| 05 | 1031 | 868750 | 20 | 835 | 768750 | 35 | 622 | 693750 |
| 06 | 1017 | 862500 | 21 | 823 | 762500 | 36 | 596 | 693750 |
| 07 | 1003 | 856250 | 22 | 812 | 756250 | 37 | 570 | 693750 |
| 08 | 990 | 850000 | 23 | 801 | 756250 | 38 | 545 | 687500 |
| 09 | 976 | 843750 | 24 | 790 | 750000 | 39 | 519 | 687500 |
| 10 | 962 | 831250 | 25 | 778 | 743750 | 40 | 493 | 687500 |
| 11 | 948 | 825000 | 26 | 767 | 737500 | 41 | 467 | 681250 |
| 12 | 935 | 818750 | 27 | 756 | 731250 | 42 | 441 | 681250 |
| 13 | 921 | 812500 | 28 | 745 | 725000 | 43 | 415 | 681250 |
| 14 | 907 | 806250 | 29 | 733 | 718750 | 44 | 390 | 675000 |

`opp15` raw `opp-microvolt = "\0\f5"` at FDT 5858. This is stock Android policy;
it is not proof that XNU can drive any of those frequencies or that the OPP voltage
maps to a PMIC selector on X6886.

### 1.5 Power throttling, thermal trips and sensors (FDT)

| Item | FDT-verified value | Source |
| :-- | :-- | :-- |
| `gpu-power-throttling` | `mediatek,gpu-power-throttling`; `lbat-max-level = 3`, `lbat-limit-freq-lv1 = 0x7fffffff` (no cap), lv2/lv3 = `0x76688` = 485000 kHz; `oc-max-level = 2`, oc-limit lv1/lv2 = 485000 kHz; `soc-max-level = 1`, soc-limit lv1 = `0x7fffffff` | FDT 2188-2200 |
| GPU thermal zone `gpu2` | LVTS channel 9; passive trip `0x14c08` = 85000 mC (85 C); `hysteresis = 0x7d0` = 2000 mC (2.0 C); cooling device = GPU phandle `0x1d`, contribution 1024; polling passive 12 / normal 500 | FDT 998-1021 |
| GPU thermal zone `gpu1` | LVTS channel 10; passive trip 85 C; hysteresis 2000 mC | FDT 1023-1037 |
| SoC critical trip | `soc_max`, LVTS channel 0, `type = "critical"`, `0x1bb5c` = 113500 mC (113.5 C), hysteresis 2000 mC | FDT 854-868 |
| Other LVTS zones | CPU little/big 1..4, `soc1/2`, `md` at 113.5 C passive; NTC and virtual-shell zones at 113.5 C / 119 C passive | FDT 870-1250 |
| LVTS controller | `mediatek,mt6789-lvts`, `reg <0 0x1100b000 0 0x1000>`, SPI 201 (`0xc9`) level-high, clock `lvts_clk`, reset, nvmem `e_data1/e_data2`, phandle `0x1b` | FDT 5647-5657; PROC `iomem.txt`, `interrupts.txt` hwirq 233 |
| Thermal interface | `mediatek,therm_intf`, SRAM `0x114000`, 1 KiB | FDT 845-849 |
| Thermal config (kernel) | `CONFIG_THERMAL`, `CONFIG_THERMAL_OF`, `CONFIG_CPU_THERMAL`, `CONFIG_THERMAL_GOV_POWER_ALLOCATOR`, `CONFIG_THERMAL_GOV_STEP_WISE`, `CONFIG_THERMAL_EMULATION` all `=y` | DUMP `kernel_config.txt` |

### 1.6 PWRAP (FDT — what is and is not verified)

| Item | FDT-verified value | Source |
| :-- | :-- | :-- |
| Wrapper | `pwrap@10026000`, compatible `mediatek,mt6789-pwrap`, `reg <0 0x10026000 0 0x1000>`, IRQ SPI 252 (`0xfc`) level-high | FDT 3219-3223; PROC `iomem.txt` `10026000-10026fff`, `interrupts.txt` hwirq 284 |
| Clocks | `spi`, `wrap`, `ulposc`, `ulposc_osc` | FDT 3224-3225 |
| PMIC child | `mediatek,mt6366`, interrupt-controller with `#interrupt-cells = <2>`, interrupts `<0x89 4>` and `<0x89 0>` on parent `0x49`; sub-blocks `mt6358-regulator`, `mt6358-gauge`, `mt6358-auxadc`, efuse, keys, RTC, accdet, etc. | FDT 3228-3860 |
| Aliases | `pwrap`, `pmic`, `mt6358regulator`, `mt6358_vgpu_reg`, `mt6358_vsram_gpu_reg` all resolve under `/soc/pwrap@10026000` | FDT 9084-9096, 9114 |

**Not in the FDT:** the SWINF command/data offsets, the capability bit that selects
SWINF, the MT6366 rail register addresses, the PWRAP FSM encoding, and any PMIC
voltage-selector encoding. These are REF-only (section 2).

### 1.7 Kernel config and shipped modules (DUMP, PROC)

Enabled in `kernel_config.txt`: `CONFIG_PM`, `CONFIG_PM_SLEEP`, `CONFIG_PM_CLK`,
`CONFIG_PM_GENERIC_DOMAINS`, `CONFIG_PM_GENERIC_DOMAINS_OF`, `CONFIG_OF`,
`CONFIG_REGULATOR`, `CONFIG_REGULATOR_FIXED_VOLTAGE`, `CONFIG_COMMON_CLK`,
`CONFIG_PM_OPP`, `CONFIG_PM_DEVFREQ`, `CONFIG_PM_DEVFREQ_EVENT`, `CONFIG_SPMI=y`.
`CONFIG_SPMI_HISI3670` and `CONFIG_SPMI_MSM_PMIC_ARB` are not set: this board reaches
the PMIC through `pwrap`, not a Qualcomm SPMI arbiter. There is no enabled
`CONFIG_MFD_MT6366`/`CONFIG_MFD_MT6358` in the config; those live as modules.
`CONFIG_GKI_HIDDEN_GPU_CONFIGS=y` hides the vendor GPU options from this file.

Live modules (PROC `modules.txt`): `mali_kbase_mt6789_a16w_jm` (991232 bytes),
`mali_mgm_mt6789_a16w_jm`, `mali_prot_alloc_mt6789_a16w_jm`, `ged`,
`mtk_gpu_qos`, `mtk_gpufreq_mt6789`, `mtk_gpufreq_wrapper_legacy`, `mtk_gpu_hal`,
`mtk_gpu_power_throttling`, `mt6358_regulator`, `mtk_pmic_wrap`,
`mt6789_mt6366`, `soc_temp_lvts`, `pmic_temp`, `thermal_interface`,
`thermal_generic_adc`, `clk_common`. Files in DUMP `all_files.txt` include
`vendor_dlkm_b/lib/modules/mali_kbase_mt6789_a16w_jm.ko`,
`mali_mgm_mt6789_a16w_jm.ko`, `mali_prot_alloc_mt6789_a16w_jm.ko`,
`mtk_gpufreq_mt6789.ko`, `mtk_gpufreq_wrapper_legacy.ko`,
`mtk_gpu_power_throttling.ko`, `mt6789-mt6366.ko`, `soc_temp_lvts.ko`,
`vendor_boot/ramdisk/lib/modules/mt6358-regulator.ko` and
`vendor_b/etc/mali_platform.config`. No `mali_csffw.bin` and no Valhall CSF firmware
exists in the dump: the Valhall v9 part uses the Job Manager, not CSF.

## 2. X676C reference-only values — unverified on X6886

None of the following appears in this device's FDT. They are the reason
`mali_hwctl` exists and why no rail/DVFS write is currently attempted.

### 2.1 PWRAP transaction layer (REF `wiki:43-55`)

| Item | X676C value | Status on X6886 |
| :-- | :-- | :-- |
| Interface | Split SWINF selected by capability bit 5 | Not read; older WACS2 (`+0xc20/+0xc24/+0xc28`, MT6768) must not be mixed in |
| CMD / WDATA / RDATA / VLDCLR / STATUS | `+0x880` / `+0x884` / `+0x894` / `+0x8a4` / `+0x8a8` | Offsets unverified |
| Write encoding | address OR `BIT(29)`; WDATA written before CMD | Unverified |
| FSM | `(STATUS >> 1) & 7`; idle 0, read-valid 6; VLDCLR write 1 | Unverified |
| PWRAP base | `0x10026000` | **Block base is DTB-verified** (FDT 3219-3222) |

### 2.2 MT6366 rail registers (REF `wiki:57-68`)

| Rail | Enable | Voltage selector | X676C observation |
| :-- | :-- | :-- | :-- |
| VGPU | `0x1508`, bit 0 | `0x1526`, bits `[6:0]` | inherited selector `0x38` |
| VSRAM_GPU | `0x1bc8`, bit 0 | `0x1bf4`, bits `[6:0]` | inherited selector `0x38` |

X676C enables the rails in the order VGPU then VSRAM_GPU, changes only the enable
bit, checks transaction errors and verifies enable readback. It reports selectors
but does not treat selector readback as a voltage or DVFS measurement, and never
forces 0.800 V. Selector values and register offsets are REF-only; the X6886 rail
*names*, ranges, phandles and rpms are DTB-verified (section 1.3).

### 2.3 MFG domains, bus protection and clocks (REF `wiki:70-102`, patch 0182)

- MFG0 `+0x308` mask `0x4`, MFG1 `+0x30c` mask `0x8`, MFG2 `+0x310` mask `0x10`,
  MFG3 `+0x314` mask `0x20`, relative to SPM `0x10006000` (base DTB-verified,
  offsets not). PWR_CON fields: `RST_B` bit 0, `ISO` bit 1, `PWR_ON` bit 2,
  `PWR_ON_2ND` bit 3, `CLK_DIS` bit 4, `SRAM_PDN` bit 8, `SRAM_PDN_ACK` bit 12.
- Status must be read from **both** `SPM +0x16c` and `SPM +0x170`.
- Per domain: set both power-on bits, poll both status registers, clear CLK_DIS and
  ISO, release reset, clear SRAM_PDN, poll SRAM_PDN_ACK.
- After MFG1, infracfg_ao bus protection: `+0x718 = 0x80`, `+0x2a4 = 0x600000`,
  `+0x718 = 0x60`, `+0x2ac = 0x200000`.
- MFG_TOP/mfgcfg (`0x13fbf000`): bg3d gate status `+0x0`, clear `+0x8`, set `+0x4`,
  bit 0; external clock controls `0x20`, `0xb0`, `0xb4`, `0x24`, `0xf60`.
- topckgen (`0x10000000`): `mfg_ref_sel` bits 17:16 with gate bit 23, parents
  `tck_26m_mx9_ck`, `mainpll_d6_d2`, `mainpll_d6`, `mainpll_d5_d2`; `mfg_pll_sel`
  bit 18 with parents `mfg_ref_ck`, `mfgpll_ck`. CLK_CFG at `+0x50`, set/clear
  `+0x54/+0x58`, CLK_CFG_UPDATE `+0x04`. X676C logged mux `0x01020000` selecting
  `mainpll_d6`; the resulting frequency was never measured there either.
- apmixedsys `0x1000c000`, MFGPLL control `+0x268`, power `+0x274`. Full PLL
  program/lock and DVFS transitions were never validated on X676C.

### 2.4 Other REF-only readings

`GPU_ID = 0x90930010`, `AS_PRESENT = 0xff`, `JS_PRESENT = 0x7`, shader mask `0x5`
are X676C values (REF `wiki:19`). X6886 has never had `GPU_ID` read; do not copy
them. JM/MMU register offsets (`GPU_ID 0x0`, `AS_PRESENT 0x18`,
`JS_PRESENT 0x1c`, job slots `0x1800 + n*0x80`, address spaces `0x2400 + n*0x40`)
and the in-tree clock/power writes at topckgen `0x58/0x04/0x50`, mfgcfg `0x8/0x0/
0x20/0xb0/0xb4/0x24/0xf60` are likewise REF (TREE `PORT-GPU.md` section 2b).

## 3. Current X6886 state (TREE, not device proof)

`kernel/x6886/hakios.x6886_mali.c` gates every write to a DTB-unproven power or
clock register behind boot arg `mali_hwctl`, which defaults to **0**. With 0 the
driver performs a read-only survey and returns `ENXIO` if firmware has not already
left the domains/gates on; it does not write an unproven gate. Bring-up is ordered
topckgen status → MFG domain status → mfgcfg gate status → `GPU_ID`, and stops on
the first failed read. **Rails are not written at all today**: VGPU and VSRAM_GPU
are left exactly as firmware set them (TREE `PORT-GPU.md` gaps 2 and 7). DVFS is
not implemented; the 45 OPPs are validated and reported only. `x6886_mali_init()`
is itself a no-op unless `mali=1`. The safe sequence below is therefore a target
specification, not a description of code already running on X6886.

## 4. Exact safe bring-up sequence (VGPU → VSRAM_GPU → MFG → clocks)

Every step is fail-closed: no write follows a failed read, all polls are bounded,
and every write is followed by a readback of the exact field written. `mali_hwctl=0`
means survey-only; the write path below is permitted only with explicit owner
opt-in (`mali_hwctl=1`) plus, eventually, a separate DVFS opt-in.

**Stage 0 — preconditions (read-only).**
1. Confirm board identity: `board-info.txt` = `require board=mt6789`, kernel
   6.12.38 (DUMP). Map only FDT-listed bases: PWRAP `0x10026000`, SPM
   `0x10006000`, infracfg_ao `0x10001000`, topckgen `0x10000000`, MFG_TOP
   `0x13fbf000`, Mali `0x13000000`, LVTS `0x1100b000`.
2. Take one mutex for all PWRAP traffic (shared with keys, gauge and charger).
3. Gate: `mali_hwctl == 0` by default. If 0, only survey: read rail enables,
   SPM statuses, bg3d gate and `GPU_ID`; if any is off, log and stop with `ENXIO`.
   Never write a gate that cannot be read back.

**Stage 1 — identify the PWRAP interface (before any rail transaction).**
4. Read the capability that selects split SWINF. If it is not present, **stop** —
   do not fall back to WACS2. Gate: one bounded read of a known MT6366 register
   completes with FSM idle 0 then read-valid 6, under the mutex. (Offsets REF.)

**Stage 2 — enable VGPU first.**
5. Read VGPU enable (`0x1508` bit 0) and selector (`0x1526[6:0]`); log both. If
   already enabled, record and skip the write. Otherwise write **only the enable
   bit** with a read-modify-write that preserves the selector and all other bits.
6. Gate: re-read `0x1508` bit 0 == 1 and check transaction error before proceeding.
   Do not write a voltage selector and do not force a fixed voltage. (Offsets REF.)

**Stage 3 — enable VSRAM_GPU second.**
7. Read `0x1bc8` bit 0 and `0x1bf4[6:0]`; log both. If off, write only bit 0 of
   `0x1bc8`, preserving the selector.
8. Gate: re-read `0x1bc8` bit 0 == 1. **Both** rail readbacks must be set before
   any SPM access. (Offsets REF.)

**Stage 4 — bring up the four MFG domains (readback at every sub-step).**
9. Read `SPM +0x16c` and `+0x170` and test masks `0x4/0x8/0x10/0x20`. If all four
   are already set in both registers, take the survey path and skip writes.
10. Otherwise, for MFG0 → MFG1 → MFG2 → MFG3 at `+0x308/+0x30c/+0x310/+0x314`:
    set `PWR_ON` (bit 2) and `PWR_ON_2ND` (bit 3); poll **both** status registers
    for that domain's mask; clear `CLK_DIS` (bit 4) and `ISO` (bit 1); set
    `RST_B` (bit 0); clear `SRAM_PDN` (bit 8); poll `SRAM_PDN_ACK` (bit 12).
    Between MFG1 and MFG2 release infracfg bus protection with the four writes
    `+0x718=0x80`, `+0x2a4=0x600000`, `+0x718=0x60`, `+0x2ac=0x200000`.
11. Gate: all four masks set in **both** status registers and bus protection
    released before any clock-enable write. (All offsets REF; only the SPM and
    infracfg_ao block bases are DTB-verified.)

**Stage 5 — clocks, without claiming a frequency.**
12. Prepare `mfg_ref_sel` (topckgen CLK_CFG_3, `+0x58` clear / `+0x04` update in
    the reference), then poll the stable bit in `+0x50` with a bounded loop.
13. Un-gate bg3d in MFG_TOP (clear `+0x8` bit 0) and poll status `+0x0` until bit 0
    reads 0; apply the external-CG sequence only after that. Gate: gate status
    reads open and the **raw** mux value is logged. Do not assert a frequency, do
    not program MFGPLL and do not touch voltages. (Offsets REF.)
14. Only now read `GPU_ID` at `0x13000000 + 0x0`; `0` or `~0` means the bank is
    still unpowered → stop. Store the X6886 value; never substitute the X676C ID.
    Mask/clear GPU, JOB and MMU interrupts and soft-reset with a bounded
    reset-complete readback before any job submission.

Ordering note: the vendor/X676C reference prepares the reference-clock *mux* before
the MFG domain sequence and enables the reference clock between MFG1 and MFG2. For
X6886 this document keeps rail → domains → clock-enable as the mandatory gating
order; any earlier mux preparation remains a gated write, never a clock enable or a
frequency claim.

## 5. Adding DVFS later without risking the board

DVFS must be a later, separately gated project. Prerequisites before any voltage or
frequency transition is attempted:

1. All four readback gates above proven on X6886 (both rails, both SPM status
   registers, bg3d status), plus a locally read `GPU_ID`.
2. PWRAP interface identified once and serialized; selector-to-voltage meaning
   measured on X6886 (the X676C `0x38` selectors are not a voltage measurement).
3. MFG mux parent and actual frequency measured on X6886; X676C's `0x01020000` is
   not proof of the running frequency.
4. LVTS GPU zones (85 C passive) and the 113.5 C critical trip observable from XNU.
5. The `opp15` entry excluded from any usable policy table.

Safe policy design:

- Treat the 45 OPPs as a **candidate** table only. Validate offline: strictly
  decreasing frequency, non-zero Hz, voltage inside the DTB rail range 500000..
  1293000 uV, and `opp15` marked invalid. Keep firmware selectors as the initial
  state; never invent a selector mapping from the OPP voltage alone.
- A transition is: raise voltage first and read it back, then raise frequency and
  read back the mux; when going down, lower frequency first, read back, then lower
  voltage and read back. Bounded polls and one mutex throughout. Any mismatch
  reverts to the last known-good operating point (or the inherited selector),
  disables further transitions and logs to the pstore mirror.
- Thermal hard limits come from this device's FDT: passive cap at 85 C on `gpu2`
  and `gpu1` with 2000 mC hysteresis; 485000 kHz low-battery and over-current
  caps from `gpu-power-throttling`; 113.5 C `soc_max` critical trip must never be
  reached under GPU load. Clamp frequency/cores before the critical trip.
- Keep every DVFS write behind a **separate boot arg** in addition to
  `mali_hwctl`, default off. No dynamic power-off until rail and domain control
  are proven with readbacks. No flash, no on-device experiment, and no copied
  X676C voltage value at any stage.

## 6. Open gaps

| # | Gap | Evidence status |
| --: | :-- | :-- |
| 1 | PWRAP SWINF vs WACS2 and SWINF offsets on X6886 | REF only; capability not read |
| 2 | MT6366 rail register offsets and selectors | REF only; rail names/ranges are DTB-verified |
| 3 | SPM/infracfg/topckgen/MFG_TOP internal offsets | REF only; block bases DTB-verified |
| 4 | `GPU_ID`, `AS_PRESENT`, `JS_PRESENT` on X6886 | Never read; X676C values are REF |
| 5 | MFG mux parent and real frequency | REF only; not measured |
| 6 | `opp15` malformed voltage | DTB-verified defect |
| 7 | SPI 399/400 (EVENT/PWR) absent live | PROC capture shows only 428/429/430 |
| 8 | GPU has no `iommus` and no `power-domains`/`clocks` in FDT | DTB-verified; rails/clock owned by `gpufreq` |
| 9 | No DVFS or rail write in the in-tree driver | TREE; rails left as firmware set them |
| 10 | No on-device validation of any GPU stage | No boot, no flash in this research track |

## 7. Files

No files were created or modified except this one:
`docs/X6886-POWER-DVFS.md`. `/home/berucrypt00/otadump` was read only.
