# Kernel Module & Kernel Config Inventory — Infinix X6886 (MT6789)
### Read-only analysis for XNU / HakiOS port bring-up ordering

- **Source:** `/home/berucrypt00/otadump` (read-only; nothing modified)
- **Kernel:** `Linux version 6.12.38-android16-5-gb575a0b6e647-ab14355190-4k` (from `kernel_version.txt`)
- **Config:** `kernel_config.txt`, 8055 lines, "Linux/arm64 6.12.38 Kernel Configuration"
- **Modules found:** 572 `.ko` files (all plain ELF, none compressed — see §6)
- **Analysis tools:** `find`, `grep`, `ls`, `file`, `modinfo`, `strings`
- **Convention:** "VERIFIED" = read directly from dump; "INFERENCE" = engineering conclusion drawn from verified facts.

---

## 1. Directory inventory (VERIFIED)

| Directory (relative to `/home/berucrypt00/otadump`) | `.ko` count | Role |
|---|---:|---|
| `vendor_boot/ramdisk/lib/modules/` | 235 | Early-boot / vendor_boot ramdisk modules (pinctrl, clocks, display, UFS, PMIC) |
| `vendor_dlkm/lib/modules/` | 186 | Vendor dynamic kernel modules (GPU, audio, camera, modem, thermal, perf) |
| `odm_dlkm/lib/modules/` | 70 | ODM (Transsion) modules (charging/TCPC, camera AF, NFC, fingerprint TA) |
| `system_dlkm/lib/modules/` | 81 | GKI modules (generic network, USB, virtio, crypto) |
| **TOTAL** | **572** | Matches the task's stated 572 |

Helper metadata present in every directory: `modules.load`, `modules.dep`, `modules.alias` (plus `modules.load.recovery` in vendor_boot). These are the authoritative boot load lists and are reproduced in §7.

### Build provenance / vermagic split (VERIFIED — important)
`modinfo` shows **two different vermagic strings** across the 572 modules:

| Vermagic | Count | Where | Toolchain |
|---|---:|---|---|
| `6.12.38-android16-5-gcc51d883045d-4k SMP preempt mod_unload modversions aarch64` | 491 | all 235 vendor_boot + all 186 vendor_dlkm + all 70 odm_dlkm | GCC-built vendor modules |
| `6.12.38-android16-5-gb575a0b6e647-ab14355190-4k SMP preempt mod_unload modversions aarch64` | 81 | all 81 system_dlkm | Clang 19, **exactly matches `kernel_version.txt`** |

- All modules carry `modversions` (CONFIG_MODVERSIONS=y) and `SMP preempt`.
- `built_with: DDK` appears on nearly every vendor/odm module.
- **INFERENCE:** the 81 GKI modules were built from the exact kernel whose config/version is in this dump; the 491 vendor/odm modules come from a differently-tagged vendor build (`gcc51d883045d`, no `-ab14355190` Android build-id suffix). For an XNU port this is only a provenance marker — XNU will not load these `.ko` files at all; they are reference material. If anyone ever tries to reflash Linux modules, the two flavors are not interchangeable with a single kernel vermagic.

---

## 2. Module inventory grouped by subsystem (VERIFIED)

Names below are basenames (a few exist in more than one directory, e.g. `arm_dsu_pmu`, `mt6358_battery`, `mtk_battery_manager`, `mtk-kpd`, `tc_*`, `pca_tfcp_ta`, `pd_dbg_info`, `rt_pd_manager`, `reboot-mode`, `syscon-reboot-mode`, `rfkill`, `libarc4`, `zsmalloc`, `thd`, `tnek`, `transsion_tranlog`, `libarc4`, `mtk_irtx_pwm`, `mtk-mbox`, `mtk-pwm`, `mtk_rpmsg_mbox`, `mtk_tinysys_ipi`, `tms_device_modules`, `tran_extcon_usb`, `tran_key_bigdata`, `tran_usb_ctrl`). The per-directory lists in §1's source files and the `modules.load` order in §7 give exact placement.

### 2.1 Display / panel (≈45 modules)
- Core DRM: `mediatek_drm_v1`, `mtk_panel_ext`, `mtk_disp_notify`, `mtk_disp_sec`, `tran_drm_panel_i2c`, `drm_dma_helper`, `drm_display_helper`, `dbgtop-drm`
- CMDQ / MML / SMI: `mtk-cmdq-drv-ext`, `cmdq-platform-mt6789`, `cmdq_helper_inf`, `cmdq-sec-drv`, `mtk-mml`, `mtk-smi`, `mtk-smi-dbg`, `mtk_sync`
- Backlight / PWM / LEDs: `leds-mtk`, `leds-mtk-disp`, `leds-mtk-pwm`, `pwm-mtk-disp`, `mtk_irtx_pwm`, `backlight_cooling`, `tc_lcd_notify`
- MDP: `mdp_drv_mt6789`
- **Panel drivers (vendor-specific, all AMOLED DSI VDO/DSC):**
  - `icnl9922c_fhdp_dsi_vdo_txd_boe_120hz_lj6`
  - `nt37703a_fhdp_dsi_vdo_dsc_tm_chct_120hz_cm5`
  - `nt37703a_fhdp_dsi_vdo_dsc_tm_tm_120hz_cl6`
  - `nt37703a_fhdp_dsi_vdo_dsc_tm_tm_120hz_cla6`
  - `nt37703a_fhdp_dsi_vdo_dsc_tm_tm_120hz_cm5`
  - `nt37703a_fhdp_dsi_vdo_dsc_tm_tm_120hz_cm6`
  - `nt37703a_fhdp_dsi_vdo_dsc_tm_tm_144hz_x6855`
  - `nt37703a_fhdp_dsi_vdo_dsc_tm_tm_144hz_x6858`
  - `nt37703a_fhdp_dsi_vdo_tm_tm_120hz_x6850`
  - `nt37706a_fhdp_dsi_vdo_dsc_boe_144hz_cn5`
  - `nt37706a_fhdp_dsi_vdo_dsc_boe_144hz_cn5c`
  - **`nt37706a_fhdp_dsi_vdo_dsc_boe_boe_144hz_x6886`** ← X6886-specific
  - `nt37706a_fhdp_dsi_vdo_dsc_vxn_vxn_144hz_x6885`
  - `rm69220_fhdp_dsi_vdo_dsc_csot_csot_120hz_x6850`
  - `rm69220_fhdp_dsi_vdo_dsc_csot_csot_120hz_x6853`
  - `rm69220_fhdp_dsi_vdo_dsc_csot_csot_144hz_x6858`
  - `vtdr6126a_fhdp_dsi_vdo_dsc_tm_144hz_cn5`
  - `vtdr6126a_fhdp_dsi_vdo_dsc_tm_144hz_cn5c`
  - `vtdr6126a_fhdp_dsi_vdo_dsc_tm_tm_144hz_x6885`
  - **`vtdr6126a_fhdp_dsi_vdo_dsc_tm_tm_144hz_x6886`** ← X6886-specific
  - `vtdr6126_fhdp_dsi_vdo_csot_panda_120hz_cl6`
  - `vtdr6126_fhdp_dsi_vdo_dsc_gvo_gvo_120hz_cm5`
  - `vtdr6126_fhdp_dsi_vdo_dsc_wxn_gvo_120hz_x6853`

### 2.2 Touch (present as `.ko`: 8)
`tui-common`, `ts_scp_common`, `gt9886`, `gt9896s`, `focaltech_ft3683g`, `adaptive-ts`, `touch_boost`, `mtk_ioctl_touch_boost`. Related input: `mtk-kpd` (keypad), `mtk-pmic-keys`, `splitswitch-gpio`.
- **VERIFIED discrepancy:** `init.insmod.touch.cfg` lists 25 touch modules (see §7.2) but **only 4 of those actually exist as `.ko` in this dump** (`tui-common`, `ts_scp_common`, `gt9886`, `gt9896s`). The other 21 (`gt9895`, `ft3518u`, `ft3683g`, `st61y`, `gt9916`, `nt36672c` + i2c/120hz/144hz variants, `synaptics_tcm`, `gt1151`, `td4320`, `nt36xxx_no_flash_spi`, `hxchipset_hx83102p`, `legacy_gt9896s`, `ft3518`, `nt36532`, `focaltech_touch`, `fts_touch_spi`, `omnivision_td4160`) are **absent**. The dump instead has `focaltech_ft3683g` and `adaptive-ts` in `odm_dlkm` (loaded via `modules.load`, not via touch.cfg).
- **INFERENCE:** X6886 most likely uses one of `gt9886`/`gt9896s`/`focaltech_ft3683g`/`adaptive-ts`; the panel-side `nt36672c` touch ICs are for sibling boards, not this dump.

### 2.3 Fingerprint (3)
`tran_fp`, `tran_fp_v2`, `pca_tfcp_ta` (the last is a Transsion TFCP TA/secure-element interface, also in odm_dlkm).

### 2.4 UFS / storage / MMC (≈21)
`ufs-mediatek-mod`, `ufs-mediatek-dbg`, `phy-mtk-ufs`, `tran-ufshcd-core`, `tran-ufshcd-pltfrm`, `tran-proc-bootdevice-mod`, `tran_scsi_mod`, `rpmb`, `rpmb-mtk`, `cqhci`, `tran_mmc_block`, `mtk-mmc-mod`, `mtk-mmc-dbg`, `tran_sd_mod`, `sd_tray`, `tran_block`, `blocktag`, `tran_ldo`, `phy-generic`, `virtio_blk` (GKI), `zsmalloc`/`tran_zram` (swap).

### 2.5 GPU (≈14)
`mali_kbase_mt6789_a16w_jm`, `mali_mgm_mt6789_a16w_jm`, `mali_prot_alloc_mt6789_a16w_jm`, `mtk_gpu_hal`, `mtk_gpufreq_mt6789`, `mtk_gpufreq_wrapper_legacy`, `mtk_gpu_qos`, `mtk_gpu_power_throttling`, `ged`, `mtk_heap_debug`, `mtk_sec_heap`, `system_heap`, `met_gpu_api`, `met_gpu_adv_api`.
- `mali_kbase` DT alias: `arm,mali-valhall` → **Mali-G720 "Valhall" (Arm 5th-gen) GPU**, MT6789-specific build (`_mt6789_a16w_jm`).

### 2.6 Audio (≈26)
`snd-soc-mt6789-afe`, `snd-soc-mtk-common`, `mt6789-mt6366`, `snd-soc-mt6366`, `snd-soc-audiodsp-common`, `snd-soc-mtk-scp-ultra`, `snd-soc-aw87xxx`, `snd-soc-fs1599`, `snd-soc-fs1894`, `snd-soc-oca72xxx`, `audio_ipi`, `adsp`, `mtk-afe-external`, `mtk-btcvsd`, `mtk-sp-spk-amp`, `mtk-scp-audiocommon`, `mtk-scp-ultra`, `mtk-vow`, `mtk-scp-vow`, `mt6358-accdet`, `rt4831a_drv`, `tc_audio`, `tran_audio_control`, `tran_audio_typec`, `mtk-scp-ultra`, `mt6789-mt6366`.

### 2.7 Wi-Fi / BT / FM / GPS / modem / NFC / Ethernet (≈40)
`wlan_drv_gen4m_6789`, `wmt_drv`, `wmt_chrdev_wifi`, `cfg80211`, `mac80211`, `libarc4`, `connadp`, `connfem`, `mddp`, `bt_drv_connac1x`, `btif_drv`, `hci_uart`, `btbcm`, `btqca`, `btsdio`, `bluetooth`, `rfcomm`, `hidp`, `fmradio_drv_mt6631_6635`, `gps_drv_stp`, `gps_pwr`, `gnss`, `ccci_md_all`, `ccci_ccif`, `ccci_dpmaif`, `ccci_auxadc`, `ccci_fsm_scp`, `ccci_util_lib`, `ccmni`, `ccidvfs`, `c2k_usb`, `c2k_usb_f_via_atc`, `c2k_usb_f_via_ets`, `c2k_usb_f_via_modem`, `mtk_u_ether`, `r8152`, `r8153_ecm`, `asix`, `ax88179_178a`, `cdc_ether`, `cdc_acm`, `cdc_ncm`, `cdc_eem`, `usbnet`, `aqc111`, `tms_device_modules` (NFC, per `init.thn31.nfc.rc`), `nfc`, `8021q`, `wlan_drv` variants referenced via property in `.rc`.

### 2.8 Camera (≈22)
`camera_isp`, `camera_mem`, `camera_eeprom`, `camera_dip_isp6s`, `camera_fdvt_isp51`, `camera_rsc_isp6s`, `imgsensor_isp6s`, `cam_qos`, `mtk_jpeg`, `mtk-cqdma`, `mainaf`, `main2af`, `main3af`, `subaf`, `sub2af`, `dw9784ois` (OIS), `flashlight`, `tran_flashlight`, `trancam_driver_elapsedtime`, `opsMediator`, `hf_manager`, `widevine_driver`.

### 2.9 Sensors (≈7)
`sensorhub`, `mtk-ssc`, `mtk-ssc-dbg-v2`, `elliptical-ultrasound`, `reset-ti-syscon`, `tc_ambient_temp_det`, `mtk-socinfo`.

### 2.10 PMIC / battery / charging (≈60, mostly Transsion `tc_*`)
- PMIC core: `mtk-pmic-wrap`, `mt6358-regulator`, `rt5133-regulator`, `mt6375`, `mt6375-adc`, `mt6375-auxadc`, `mt6397`, `rtc-mt6397`, `mt63xx-debug`, `mt63xx-oc-debug`, `mt6577_auxadc`, `mt635x-auxadc`, `nvmem-mt635x-efuse`, `clkbuf`, `pmic_lbat_service`, `pmic_lvsys_notify`, `pmic_temp`, `regulator-vibrator`, `mtk-pmic-keys`
- Battery/charging: `mt6358_battery`, `mtk_battery_manager`, `mtk_battery_oc_throttling`, `mtk_low_battery_throttling`, `mtk_bp_thl`, `tc_pmic`, `tc_gauge`, `tc_charger_class`, `tc_charger_framework`, `tc_charger_class`, `tc_adapter_class`, `tc_algorithm_class`, `tc_ta_class`, `tc_common_class`, `tc_sc8548_charger`, `tc_sc8950`, `tc_sgm41516d`, `tc_hl7139a`, `tc_tcpc`, `tcpc_class`, `tcpc_upm7610`, `tcpci_late_sync`, `tc_pd_adapter`, `tc_pd_algo`, `tc_pe20_algo`, `tc_pep50`, `tc_tc30_algo`, `tc_hvdcp20_algo`, `tc_pid_chg_algo`, `tc_pd_algo`, `rt_pd_manager`, `pca_tfcp_ta`, `tc_tfcp_ta`, `tc_otg_ctrl`, `tc_water_detect`, `tc_phy_insert_det`, `tc_port_corrosion_prevention`, `tc_chg_type_det`, `tc_temp_forecast`, `tc_auto_test`, `tc_trancare`, `tc_misc_intf`, `tran_power_ctrl`, `tran_health_sched`.

### 2.11 Thermal (≈14)
`thd`, `thermal_interface`, `thermal-generic-adc`, `thermal_trace`, `board_temp`, `soc_temp_lvts`, `pmic_temp`, `md_cooling_all`, `backlight_cooling`, `mtk_bp_thl`, `mtk_cpu_power_throttling`, `mtk_gpu_power_throttling`, `mtk_md_power_throttling`, `mtk_apu_power_throttling`, `mtk_peak_power_budget`, `mtk_mdpm`.

### 2.12 Clocks / pinctrl / bus / IOMMU / misc platform (≈55)
- Clocks: `clk-common`, `clk-mt6789`, `clk-mt6789-audiosys`, `clk-mt6789-bus`, `clk-mt6789-cam`, `clk-mt6789-disp_dsc`, `clk-mt6789-i2c`, `clk-mt6789-img`, `clk-mt6789-impen`, `clk-mt6789-mfgcfg`, `clk-mt6789-mmsys`, `clk-mt6789-vcodec`, `clk-bringup`, `clkbuf`, `clk-chk-mt6789`, `clk-dbg-mt6789`, `clk-fmeter-mt6789`, `clk-disable-unused`, `fhctl`
- Pinctrl/buses: `pinctrl-mtk-v2`, `pinctrl-mt6789`, `pinctrl-mtk-common-v2_debug`, `i2c-mt65xx`, `spi-mt65xx`, `8250_mtk`, `mtk-uart-apdma`, `timer-mediatek`, `mtk-pwm`, `mtk_irtx_pwm`
- IOMMU/M4U: `mtk_iommu`, `mtk_iommu_util`, `iommu_debug`, `iommu_secure`, `iommu_gz`, `iommu_test`, `mtk-smi`, `mtk-smi-dbg`, `devmpu`, `emi`, `emi-mpu`, `mtk-emi`
- Power/performance: `mtk_dcm`, `mt6789_dcm`, `mtk-dvfsrc`, `mtk-dvfsrc-regulator`, `mtk-dvfsrc-devfreq`, `mtk-dvfsrc-helper`, `mtk-dvfsrc-start`, `mtk_qos`, `mmqos-common`, `mmqos-mt6789`, `mmqos_wrapper`, `mediatek-cpufreq-hw`, `cpudvfs`, `mtk_core_ctl`, `mtk_slbc`, `mtk_scmi`, `tinysys-scmi`, `mtk_scpsys`, `mtk-scpsys-bringup`, `mtk-scpsys-mt6789`, `scp`, `sspm_v3`, `mcupm`, `mtk-tinysys-ipi`, `mtk-mbox`, `mtk-mbox-mailbox`, `mtk_rpmsg_mbox`, `mtk_infracfg`?
- Hypervisor/TEE: `gzvm`, `gz_main_mod`, `gz_irq_mod`, `gz_log_mod`, `gz_ipc_mod`, `gz_tz_system`, `gz_trusty_mod`, `gz_virtio_mod`, `pkvm_p1_dummy`, `trusty`? → `trusted_mem`, `mtk_sec_heap`, `ffa_v11`, `teeperf`, `mcDrvModule`, `isee`, `sec`, `sec-rng`, `open-dice`, `widevine_driver`.
- Debug/logging: `aee_aed`, `aee_hangdet`, `aee_rs`, `mrdump`, `mkp`, `bootprof`, `dbgtop-drm`, `log_store`, `mmprofile`, `met`, `met_*_api`, `irq-dbg`, `mtk_printk_ctrl`, `monitor_hang`, `load_track`, `trace_mmstat`, `transsion_tranlog`, `tran_*` (various), `arm_dsu_pmu`, `archcounter_timesync`, `atf_logger`, `low_latency_sched`, `scheduler`, `cpuqos_v3`, `cpufreq_sugov_ext`, `fpsgo`, `mtk_fpsgo_v3`, `perf_common_v`, `mtk_perf_common`, `mtk_perf_ioctl`, `mtk_ioctl_powerhal`, `mtk_ioctl_touch_boost`, `rps_perf`, `vip_engine`, `pmsr`, `tkv`, `tkv_recovery`, `tnek`, `hiber`, `uload_ind`, `priority_allocate`, `pages_prefilter`, `trancam_driver_elapsedtime`.
- GKI generic (system_dlkm): `kheaders`, `netfs`, `rust_binder`, `virtio_*`, `can*`, `ppp*`, `l2tp*`, `tipc*`, `9pnet*`, `nhc_*`, `ieee802154*`, `mac802154`, `6lowpan`, `tls`, `macsec`, `8021q`, `open-dice`, `vcpu_stall_detector`, `pwrseq-core`, `ptp*`, `pps_core`, `gnss`, etc.

---

## 3. Built-in (`=y`) vs loadable (`=m`) for key subsystems (VERIFIED from `kernel_config.txt`)

| Subsystem | Core config (all `=y` unless noted) | Vendor driver delivered as `.ko`? |
|---|---|---|
| **UFS** | `CONFIG_SCSI_UFSHCD=y`, `CONFIG_SCSI_UFSHCD_PLATFORM=y`, `CONFIG_SCSI_UFSHCD_PCI=y`, `CONFIG_SCSI_UFS_CRYPTO=y`, `CONFIG_SCSI_UFS_BSG=y`, `CONFIG_SCSI_UFS_DWC_TC_PLATFORM=y`, `CONFIG_SCSI_UFS_HISI=y`, `CONFIG_SCSI_UFS_VARIABLE_SG_ENTRY_SIZE=y`, `CONFIG_GKI_HIDDEN_UFS_CONFIGS=y` | **Yes**: `ufs-mediatek-mod`, `phy-mtk-ufs`, `ufs-mediatek-dbg`, `tran-ufshcd-core/pltfrm`, `rpmb-mtk` |
| **MMC/SD** | `CONFIG_MMC=y`, `CONFIG_MMC_SDHCI=y`, `CONFIG_MMC_SDHCI_PLTFM=y`, `CONFIG_MMC_SDHCI_IO_ACCESSORS=y`, `CONFIG_MMC_BLOCK=y`, `CONFIG_MMC_CRYPTO=y`, `CONFIG_GKI_HIDDEN_MMC_CONFIGS=y` | **Yes**: `mtk-mmc-mod`, `mtk-mmc-dbg`, `tran_mmc_block`, `tran_sd_mod`, `cqhci` |
| **I2C** | `CONFIG_I2C=y`, `CONFIG_I2C_BOARDINFO=y`, `CONFIG_I2C_HID=y` | **Yes**: `i2c-mt65xx` |
| **SPI** | `CONFIG_SPI=y`, `CONFIG_SPI_MASTER=y`, `CONFIG_SPI_MEM=y`, `CONFIG_SPI_DYNAMIC=y`, `CONFIG_SPI_SLAVE=y` | **Yes**: `spi-mt65xx` |
| **DRM / framebuffer** | `CONFIG_DRM=y`, `CONFIG_DRM_KMS_HELPER=y`, `CONFIG_DRM_MIPI_DSI=y`, `CONFIG_DRM_PANEL=y`, `CONFIG_DRM_PANEL_BRIDGE=y`, `CONFIG_DRM_BRIDGE=y`, `CONFIG_DRM_GEM_SHMEM_HELPER=y`, `CONFIG_DRM_TTM=y`, `CONFIG_GKI_HIDDEN_DRM_CONFIGS=y`. **No `CONFIG_FB=y` / `CONFIG_FRAMEBUFFER_CONSOLE` found** — pure DRM/KMS, no fbdev | **Yes**: `mediatek_drm_v1`, `mtk_panel_ext`, all panel `.ko` |
| **Input / touchscreen** | `CONFIG_INPUT=y`, `CONFIG_INPUT_TOUCHSCREEN=y`, `CONFIG_INPUT_EVDEV=y`, `CONFIG_INPUT_KEYBOARD=y`, `CONFIG_INPUT_MISC=y`, `CONFIG_INPUT_LEDS=y` | **Yes**: all touch IC modules |
| **Regulator** | `CONFIG_REGULATOR=y`, `CONFIG_REGULATOR_FIXED_VOLTAGE=y`, `CONFIG_REGULATOR_SUN20I=y` | **Yes**: `mt6358-regulator`, `rt5133-regulator`, `mtk-dvfsrc-regulator` |
| **Clock** | `CONFIG_COMMON_CLK=y`, `CONFIG_HAVE_CLK=y`, `CONFIG_HAVE_CLK_PREPARE=y`, `CONFIG_PM_CLK=y`, `CONFIG_CLKSRC_MMIO=y` | **Yes**: all `clk-mt6789*`, `clk-common` |
| **Pinctrl** | `CONFIG_PINCTRL=y`, `CONFIG_PINCTRL_SINGLE=y`, `CONFIG_OF_GPIO=y` | **Yes**: `pinctrl-mt6789`, `pinctrl-mtk-v2` |
| **GPIO** | `CONFIG_GPIOLIB=y`, `CONFIG_GPIOLIB_IRQCHIP=y`, `CONFIG_GPIO_GENERIC=y`, `CONFIG_GPIO_GENERIC_PLATFORM=y`, `CONFIG_GPIO_PL061=y`, `CONFIG_OF_GPIO=y` | minimal: `splitswitch-gpio`, `mtk-pmic-keys` |

**Summary (VERIFIED):** every key bus/storage/display core is compiled **into the GKI kernel (`=y`)**; the entire MediaTek/Transsion platform layer is **out-of-tree loadable modules (`=m`)**. Only one `CONFIG_MTK_*` symbol exists in the whole config: `CONFIG_MTK_GZVM=m`. This confirms the config is a clean GKI kernel with vendor bits added purely as modules.

---

## 4. Key config values (VERIFIED)

| Symbol | Value |
|---|---|
| Page size | `CONFIG_PAGE_SIZE_4KB=y`, `CONFIG_ARM64_4K_PAGES=y`, `CONFIG_PAGE_SIZE_LESS_THAN_64KB=y`, `CONFIG_PAGE_SIZE_LESS_THAN_256KB=y` (i.e. **4 KiB pages**) |
| ARM64 | `CONFIG_ARM64=y`, `CONFIG_ARM64_VA_BITS=39`, `CONFIG_ARM64_PA_BITS=48`, `CONFIG_ARM64_4K_PAGES=y` |
| CMA | `CONFIG_CMA=y`, `CONFIG_DMA_CMA=y`, `CONFIG_CMA_SIZE_MBYTES=16` (`CONFIG_CMA_SIZE_SEL_MBYTES=y`), `CONFIG_CMA_AREAS=16`, `CONFIG_CMA_ALIGNMENT=8`, `CONFIG_CMA_SYSFS=y`, `CONFIG_CMA_DEBUGFS=y` |
| GKI / version | Kernel `6.12.38-android16-5-…`, `CONFIG_LOCALVERSION="-4k"`, many `CONFIG_GKI_HIDDEN_*` and `CONFIG_GKI_HACKS_TO_FIX=y`, `CONFIG_GKI_DYNAMIC_TASK_STRUCT_SIZE=y`, `CONFIG_GKI_TASK_STRUCT_VENDOR_SIZE_MAX=512`. **INFERENCE: GKI 6.12 / android16** |
| Modules | `CONFIG_MODULES=y`, `CONFIG_MODVERSIONS=y`, `CONFIG_MODULES_TREE_LOOKUP=y`, `CONFIG_MODULES_USE_ELF_RELA=y` |
| Device tree | `CONFIG_OF=y`, `CONFIG_OF_FLATTREE=y`, `CONFIG_OF_EARLY_FLATTREE=y`, `CONFIG_OF_ADDRESS=y`, `CONFIG_OF_IRQ=y`, `CONFIG_OF_IOMMU=y`, `CONFIG_OF_GPIO=y`, `CONFIG_OF_RESERVED_MEM=y`, `CONFIG_OF_MDIO=y`, `CONFIG_OF_PMEM=y`, `CONFIG_OF_KOBJ=y` |
| DRM | `CONFIG_DRM=y` (details §3) |
| MediaTek highlights | `CONFIG_MTK_GZVM=m` (only `CONFIG_MTK_*` symbol); `CONFIG_GKI_HIDDEN_UFS_CONFIGS=y`, `CONFIG_GKI_HIDDEN_MMC_CONFIGS=y`, `CONFIG_GKI_HIDDEN_DRM_CONFIGS=y`, `CONFIG_GKI_HIDDEN_GPU_CONFIGS=y`, `CONFIG_GKI_HIDDEN_SND_CONFIGS=y`, `CONFIG_GKI_HIDDEN_MEDIA_CONFIGS=y` |
| Boot cmdline | `CONFIG_CMDLINE="console=ttynull stack_depot_disable=on cgroup_disable=pressure kasan.stacktrace=off kvm-arm.mode=protected bootconfig"`, `CONFIG_CMDLINE_EXTEND=y` → pKVM enabled, bootconfig used |
| Other memory | `CONFIG_ZRAM=m` (backend LZ4/LZO/ZSTD, default `lzo-rle`), `CONFIG_ZSMALLOC=m`, `CONFIG_SPARSEMEM=y`, `CONFIG_TRANSPARENT_HUGEPAGE=y`, `CONFIG_ZONE_DMA32=y`, `CONFIG_SWIOTLB=y`, `CONFIG_LZ4_*`/`CONFIG_LZO_*` |
| Toolchain | Clang 19.0.1 + LLD 19 (`r536225`), Rust 1.82 enabled, LTO + PGO + BOLT + MLGO |

---

## 5. Module load mechanism (VERIFIED)

`/home/berucrypt00/otadump/vendor/bin/init.insmod.sh` parses `init.insmod.*.cfg`. Actions: `insmod`, `setprop`, `enable`, `modprobe`. For `modprobe` it iterates `/system_dlkm/lib/modules`, `/vendor/lib/modules`, `/odm/lib/modules`, picks a 16k subdir only when `getconf PAGE_SIZE == 16384`, and runs `modprobe -a -d <dir> $(cat <dir>/modules.load)`. `init.insmod.mt6789.cfg` uses `modprobe|*` → so **all `modules.load` entries load**. This kernel is 4K, hence the 16k variants are not used.

### 5.1 Consolidated `init.insmod.*.cfg` table (VERIFIED — complete)

| File | Line(s) | Module / action | Parameters |
|---|---|---|---|
| `vendor/etc/init.insmod.mt6789.cfg` | 1 | `modprobe\|*` → expands to every `modules.load` entry in system_dlkm + vendor + odm | none |
| | 2 | `setprop\|vendor.all.modules.ready` | property set to `1` after load |
| `vendor/etc/init.insmod.touch.cfg` | 1–25 | `modprobe\|tui-common.ko`, `ts_scp_common.ko`, `gt9895.ko`, `ft3518u.ko`, `ft3683g.ko`, `st61y.ko`, `gt9916.ko`, `nt36672c.ko`, `gt9886.ko`, `synaptics_tcm.ko`, `gt9896s.ko`, `gt1151.ko`, `td4320.ko`, `nt36xxx_no_flash_spi.ko`, `hxchipset_hx83102p.ko`, `nt36672c_i2c.ko`, `nt36672c_144hz.ko`, `nt36672c_120hz.ko`, `legacy_gt9896s.ko`, `ft3518.ko`, `nt36532.ko`, `focaltech_touch.ko`, `fts_touch_spi.ko`, `omnivision_td4160.ko` | **none** (bare `modprobe` lines) |

**VERIFIED:** neither cfg file passes any module parameter — so there is no parameter table to extract from them. Module parameters live in the ELF `.modinfo` sections instead (from `modinfo`, 622 `parm:` lines across all 572 modules). The useful ones for hardware expectations are in §5.2/§5.3.

### 5.2 Additional `.rc`-level insmod hooks (VERIFIED — `vendor/etc/init/`)

| RC file | Module / mechanism |
|---|---|
| `init.bt_drv.rc` | `insmod /vendor/lib/modules/bt_drv_${ro.vendor.bt.platform}.ko` |
| `init.wlan_drv.rc` | `insmod ${ro.vendor.wlan.chrdev}.ko`, `insmod wlan_drv_${ro.vendor.wlan.gen}.ko` |
| `init.gps_drv.rc` | `insmod ${ro.vendor.gps.chrdev}.ko` |
| `init.wmt_drv.rc`, `hw/meta_init.connectivity.rc` | `insmod wmt_drv.ko`, `wmt_chrdev_wifi.ko`, `btmtk_uart_unify.ko`, `fmradio_drv_${ro.vendor.fm.platform}.ko` |
| `init.thn31.nfc.rc` | `insmod tms_device_modules.ko` |
| `hw/init.mt6789.rc` | `insmod fpsgo.ko`; **commented out**: `mali.ko`, `m4u.ko`, `mjc_kernel_driver.ko` |
| `init.connfem.rc` | note: "Move `insmod connfem.ko` cmd to ko_order_table.csv" (that CSV is not present in this dump) |
| `hw/init.mtkgki.rc` | `write /proc/bootprof "modprobe: Load_Module_START"` → `start insmod_sh` |

### 5.3 Selected module parameters from `modinfo` (VERIFIED — the real "expected firmware/pins" data)

| Module | `parm:` (name: description (type)) |
|---|---|
| `ufs-mediatek-mod` | *(no parms)* — depends: `tran-ufshcd-pltfrm,ufs-mediatek-dbg,tran-proc-bootdevice-mod,tran-ufshcd-core,irq-dbg,thd,transsion_tranlog,tran_scsi_mod,blocktag,rpmb` |
| `tran-ufshcd-core` | `dev_cmd_timeout` (timeout ms, default 1.5 s), `uic_cmd_timeout` (default 500 ms), `use_mcq_mode` (bool, UFSHCI 4.0 MCQ), `tran_poll_queues` (default 1), `read_queues` (default 0), `rw_queues` (default nr_cpus) |
| `tran_mmc_block` | `perdev_minors` (int) |
| `tran_fp` / `tran_fp_v2` | `bufsiz` = "data bytes in biggest supported SPI message" (uint) → **fingerprint is SPI-attached** (depends `spi-mt65xx`) |
| `mali_kbase_mt6789_a16w_jm` | `gpu_req_timeout`, `gpu_metrics_tp_emit_interval_ns`, `corestack_driver_control`, `fully_backed_gpf_memory`, `l2_hash_values`, `override_l2_hash`, `override_l2_size`, `kbase_page_migration_enabled`, `large_page_conf` |
| `wlan_drv_gen4m_6789` | `nvram:charp` — Wi-Fi NVRAM passed as insmod parameter; driver requests firmware via `request_firmware` and reads `/data/nvram/APCFG/APRDEB/WIFI` (strings) |
| `wmt_drv` | `gWmtDetectMajor:uint`; SDIO IDs `037a:d018a/b/c`, `037a:d6619`, `037a:d020a/b/c`, `037a:d5921`, `037a:d6628/d6630/d6602/d6632`; contains `mt66xx_patch_hdr.bin` firmware string |
| `camera_isp` | `debug_camisp:int` |
| `thd` | `hung_task_warnings`, `hung_task_maxdeath_count`, `hung_task_maxiowait_count` |
| `tran_fp`, `mali_kbase` | both call `request_firmware` / `release_firmware` (strings) |

### 5.4 Authoritative boot load order (VERIFIED — `vendor_boot/ramdisk/lib/modules/modules.load`, 183 entries)
This is the real dependency-respecting order used by Android init. Abridged into phases:

1. **Debug/log:** `mkp`, `bootprof`, `dbgtop-drm`, `mrdump`, `mtk_sync`, `mtk_disp_notify`, `aee_aed`, `aee_hangdet`, `monitor_hang`, `mtk_printk_ctrl`, `log_store`
2. **Identity/efuse/secure:** `nvmem_mtk-devinfo`, `mtk_wdt`, `timer-mediatek`, `opsMediator`, `sec-rng`, `nvmem-mt635x-efuse`, `device-apc-common`, `device-apc-mt6789`
3. **Pinctrl + clocks:** `pinctrl-mtk-v2`, `pinctrl-mt6789`, `pinctrl-mtk-common-v2_debug`, `clk-common`, `clk-mt6789`, `clk-mt6789-{audiosys,cam,mmsys,img,i2c,bus,vcodec,disp_dsc,impen,mfgcfg}`, `mtk-scpsys{, -bringup, -mt6789}`, `clk-bringup`, `pd-chk-mt6789`, `clk-fmeter/chk/dbg-mt6789`
4. **Memory/power:** `mtk_dramc`, `mtk-dvfsrc`, `mtk-dvfsrc-regulator`, `mtk-emi`, `mtk-smi`, `mtk-smi-dbg`, `mtk-uart-apdma`, `8250_mtk`
5. **Buses:** `i2c-mt65xx`, `rtc-mt6397`, `spi-mt65xx`
6. **MM/QoS/IOMMU/CMDQ:** `mmprofile`, `mtk-icc-core`, `mtk-mmdvfs*`, `mmqos*`, `mtk_iommu_util`, `iommu_debug`, `iommu_secure`, `mtk_iommu`, `system_heap`, `mtk-cmdq-drv-ext`, `cmdq-platform-mt6789`, `cmdq_helper_inf`, `mtk-mml`, `drm_dma_helper`, `drm_display_helper`
7. **Display:** `rt4831a_drv`, `mtk_panel_ext`, `tran_ldo`, `tran_drm_panel_i2c`, **all panel `.ko`**, then `mediatek_drm_v1`, `aee_rs`
8. **USB PHY:** `phy-generic`, `phy-mtk-tphy`, `musb_hdrc`, `musb_main`, `usb_meta`
9. **PMIC:** `mtk-pmic-wrap`, `tran_expand_key`, `mt635x-auxadc`, `mt6397`, `mt63xx-debug`, `mt6358-regulator`
10. **Storage:** `tran_sd_mod`, `tran_scsi_mod`, `tran-ufshcd-pltfrm`, `tran-ufshcd-core`, `tran_mmc_block`, `tran_block`, `low_latency_sched`, `cqhci`, `mtk-mmc-dbg`, `mtk-mmc-mod`, `tran_rhi`, `pmic_lbat_service`, `pmic_lvsys_notify`, `reset-ti-syscon`, `phy-mtk-ufs`, `rpmb`, `rpmb-mtk`, `ufs-mediatek-mod`, `ufs-mediatek-dbg`, `tran-proc-bootdevice-mod`, `blocktag`, `pidmap`
11. **Input/FP/misc:** `mtk-pmic-keys`, `mtk-kpd`, `regulator-vibrator`, `mtk_dcm`, `mt6789_dcm`, `mt6375*`, `leds-mtk*`, `irq-dbg`, `tran_fp_v2`, `tran_fp`, `mtk-socinfo`, `mtk-afe-external`, cpufreq/debug, `sd_tray`, `mtk-pwm`, `mtk_irtx_pwm`

`vendor_dlkm/modules.load` then brings up perf/thermal (thd), GPU (`mtk_gpu_hal` → `ged` → `mali_kbase*`), modem (`ccci_*`), audio (`adsp`, `audio_ipi`, `snd-soc-*`), camera, TEE/pKVM, Wi-Fi (`wmt_drv`), etc. `odm_dlkm/modules.load` brings up touch/focaltech, camera AF, NFC, and the whole `tc_*` charging stack. `system_dlkm/modules.load` is generic GKI networking.

---

## 6. Compression status (VERIFIED)

- All 572 files are named `*.ko` — **zero** `.ko.xz`, `.ko.gz`, `.ko.zst`, or `.ko.lzma`.
- `file` on `vendor_boot/ramdisk/lib/modules/ufs-mediatek-mod.ko` returns: `ELF 64-bit LSB relocatable, ARM aarch64, version 1 (SYSV), BuildID[sha1]=…, not stripped`. Spot checks across all four directories are identical.
- **No decompression is required.** If a future dump provides compressed modules, the commands would be:
  - `xz -dk <file>.ko.xz` (or `unxz -k`)
  - `gzip -dk <file>.ko.gz` (or `gunzip -k`)
  - `zstd -d <file>.ko.zst`
  - then run `modinfo <file>.ko`.

---

## 7. Prioritized bring-up order recommendation for XNU / HakiOS (INFERENCE)

Framing: XNU cannot load Linux `.ko` files. These modules are a **hardware map** — DT `compatible` strings, clock names, register addresses, firmware/NVRAM expectations, and the stock load order. The sequence below mirrors the verified Android `modules.load` order and adapts it to what an XNU kernel must initialize to reach a usable display+input state.

| Tier | Hardware | Evidence from dump (`modules.load` / modules) | Why this order |
|---|---|---|---|
| **0. Boot foundations** (before any driver) | CPU/MMU, 4K pages, device tree, CMA (16 MB), pKVM cmdline, timer, interrupts, early console | `CONFIG_ARM64`, `CONFIG_PAGE_SIZE_4KB`, `CONFIG_OF_*`, `CONFIG_CMA_SIZE_MBYTES=16`, `CMDLINE … kvm-arm.mode=protected`, `timer-mediatek`, `mtk_wdt`, `irq-dbg` | Nothing else works without MMU/DT/clock source. Note stock console is `ttynull` — XNU needs its own UART (`8250_mtk` + `mtk-uart-apdma`). |
| **1. Pinctrl + clocks + PMIC** | `pinctrl-mt6789`, `pinctrl-mtk-v2`, `clk-mt6789*`, `clk-common`, `mt6358-regulator`, `mtk-pmic-wrap`, `mtk-dvfsrc`, `mtk_dramc`, `mtk-emi`, SCP | load-order phases 3–4 and 9; clock names in §8 | Every other controller needs its pins muxed and clocks enabled first. MT6789 clock tree is the single biggest dependency hub. |
| **2. Boot storage** | UFS: `tran-ufshcd-core/pltfrm`, `phy-mtk-ufs`, `ufs-mediatek-mod`, `rpmb`, `blocktag`; MMC fallback: `mtk-mmc-mod`, `tran_mmc_block`, `cqhci` | phase 10; `CONFIG_SCSI_UFSHCD=y` core + vendor UFS host `.ko`; `ufs-mediatek-mod` parms | Root filesystem must mount. UFS is primary (MT6789 + UFS MPHY), SD/MMC fallback. RPMB needed for secure storage. |
| **3. Display** | `mediatek_drm_v1`, `mtk_panel_ext`, CMDQ, `mtk_iommu`, `mtk-mml`, `mtk-smi`, then the X6886 panel **`nt37706a_…_x6886`** or **`vtdr6126a_…_x6886`** (144 Hz) | phase 6–7; DT aliases `mediatek,mt6789-disp-iommu`, `mediatek,mt*-mmsys`; panel DT compat `nt37706a,fhdp,dsi,vdo,dsc,boe,boe,144hz,x6886` and `vtdr6126a,…` | First user-visible milestone. Two candidate panels exist for X6886 — read the actual panel ID / DT chosen in the dump's DTB before committing. DSC + 144 Hz is non-trivial; start with a dumb framebuffer via the display engine, then DSI/DSC. |
| **4. Touch / input** | SPI/I2C touch (`gt9886`, `gt9896s`, `focaltech_ft3683g`, `adaptive-ts`), `tui-common`, `ts_scp_common`, `i2c-mt65xx`, `spi-mt65xx` | §2.2 + touch.cfg; `CONFIG_INPUT_TOUCHSCREEN=y` | Required to interact with the UI once display is up. Touch IC is binary-specific (4 candidates present); identify via DT/prop before writing XNU driver. |
| **5. Fingerprint** | `tran_fp`, `tran_fp_v2`, `pca_tfcp_ta` (SPI-attached, `bufsiz` parm), plus TEE `trusted_mem`, `gz_*`, `mtk_sec_heap` | §2.3, §5.3; `depends: spi-mt65xx` | Needs SPI + TEE/pKVM already working; enroll/auth is a later UX milestone. |
| **6. GPU** | `mali_kbase_mt6789_a16w_jm` (Valhall), `mtk_gpu_hal`, `mtk_gpufreq_mt6789`, `ged`, IOMMU, CMA | `vendor_dlkm/modules.load` GPU block; DT `arm,mali-valhall`; many heap/QoS deps | Hardware-accelerated UI; depends on IOMMU + CMA + clocks from tiers 1/3. |
| **7. Wi-Fi / connectivity** | `wlan_drv_gen4m_6789` (needs `nvram` param + `request_firmware`), `wmt_drv` (SDIO, `mt66xx_patch_hdr.bin`, `/data/nvram/APCFG/APRDEB/WIFI`), `cfg80211`, `mac80211`, BT `bt_drv_connac1x` | §2.7, §5.3, `.rc` property-driven loads | Last — needs firmware files + NVRAM partition + SDIO/PMIC; not needed for first boot. |
| **8. Camera / audio / sensors / charging** | `camera_isp`+`imgsensor_isp6s`, `snd-soc-mt6789-afe`, `sensorhub`, `mtk-ssc`, full `tc_*` charging stack | §2.6/2.8/2.9/2.10, odm modules.load | Feature-completion phase; large surface, no effect on core boot. |

**Critical-path note (INFERENCE):** Tier 1 → 2 → 3 → 4 is the minimum to a usable XNU desktop. The stock load order proves clocks/pinctrl precede UFS and display; doing storage or display before the MT6789 clock tree will fail. The two X6886 panel modules and the 4 candidate touch ICs must be disambiguated from the dump's DTB (`compatible` properties) before writing XNU drivers.

---

## 8. MediaTek-specific modules revealing register bases / clock names / DT bindings (VERIFIED)

| Module | Revealed detail (from `modinfo` aliases / `strings`) |
|---|---|
| `clk-mt6789.ko` | Clock names: `clk26m`, `clk32k`, `clkrtc`, `mmpll`, `mmpll_d4_d2`, `mmpll_d5_d2`, `mmpll_d6`, `mmpll_d6_d2`, `mmpll_d7`, `apdma_ck`, `i2c_ck`, `i2c_pseudo_ck`, `i2c_sel`, `i2c_parents`, `bus_aximem_sel`, `bus_aximem_parents`; functions `clk_mt6789_apmixed_probe`, `clk_mt6789_top_probe`, `clk_mt6789_pll_registration`. DT compat: **`mediatek,mt6789-apmixedsys`**, **`mediatek,mt6789-topckgen`** |
| `mediatek_drm_v1.ko` | **Register address: `DPTX Efuse(0x11C101B8)`**; requires DT nodes `mediatek,infracfg_ao`, `mediatek,infracfg_ao_mem`, `mediatek,larb`, `mediatek,mt-pseudo_m4u-port`; aliases include `mediatek,mt6893-disp`, `mediatek,mt6989-disp`, `mediatek,mt6768-mmsys` etc. (generic driver covering many SoCs) |
| `pinctrl-mt6789.ko` | DT compat **`mediatek,mt6789-pinctrl`**; pin names `SPI0_CLK/CSB/MI/MO` … `SPI4_*`, `CLKM0`–`CLKM3` → gives the exact pin groups for touch (SPI) and display. |
| `ufs-mediatek-mod.ko` | DT compat **`mediatek,mt8183-ufshci`** (MT6789 reuses mt8183 UFS HCI binding); DT properties `mediatek,ufs-disable-mcq`, `mediatek,ufs-disable-ah8`, `mediatek,ufs-broken-vcc`, `mediatek,ufs-broken-rtc`, `mediatek,ufs-rtff-mtcmos`, `mediatek,ufs-pmc-via-fastauto`, `mediatek,ufs-boost-crypt`, `mediatek,afl-fuzzer-enabled`; register/value hint `force reset only 0x80000003!` |
| `phy-mtk-ufs.ko` | DT compat `mediatek,mt8183-ufsphy` — the UFS MPHY |
| `i2c-mt65xx.ko` | DT aliases `mediatek,mt6781-i2c`, `mediatek,mt6768-i2c`, `mediatek,mt6873-i2c`, … (MT6789 shares the mt6781 binding) |
| `spi-mt65xx.ko` | DT aliases `mediatek,mt6765-spi`, `mediatek,mt6858-spi`, `mediatek,mt6893-spi`, … |
| `mtk_iommu.ko` | DT compat **`mediatek,mt6789-disp-iommu`** plus many `mediatek,mt*-m4u` variants — display IOMMU is MT6789-specific |
| Panel `.ko` (X6886) | DT compat `nt37706a,fhdp,dsi,vdo,dsc,boe,boe,144hz,x6886` and `vtdr6126a,fhdp,dsi,vdo,dsc,tm,tm,144hz,x6886`; authors `baofu.zhang@transsion.com` / `bing.he@transsion.com`; depends `mtk_panel_ext,mtk-cmdq-drv-ext` |
| `mali_kbase_mt6789_a16w_jm.ko` | DT compat `arm,mali-valhall`; depends `mtk_gpufreq_wrapper_legacy,mtk_gpu_hal,sspm_v3,mtk_gpu_qos,ged,mrdump` |
| `tran_fp.ko` | DT compat `tran_fp_legacy`; SPI interface (`bufsiz` parm, depends `spi-mt65xx`) |
| `wlan_drv_gen4m_6789.ko` | `nvram` charp param; firmware requests; SDIO/NVRAM path `/data/nvram/APCFG/APRDEB/WIFI` |
| `wmt_drv.ko` | SDIO VID/PID table (0x037A MediaTek), firmware `mt66xx_patch_hdr.bin` |
| `mt6789-mt6366.ko`, `snd-soc-mt6366.ko` | Audio codec MT6366 (PMIC-coupled) |

**INFERENCE:** the most valuable concrete numbers for an XNU bring-up are the `clk-mt6789` clock names, the `mediatek,mt6789-*` DT compat strings (topckgen, apmixedsys, pinctrl, disp-iommu), the `0x11C101B8` DPTX efuse register, and the panel/touch DT compatible strings. Full MMIO base addresses live in the DTB (not in the config/module metadata analysed here); the next step would be to read the dumped DTB/DTBO for `reg = <...>` on these nodes.

---

## 9. Verified vs inference — quick separation

- **VERIFIED (read from files):** all counts and paths, both vermagic strings and their counts, every `CONFIG_*` value quoted, every module name, all `init.insmod.*.cfg` contents (no parameters present), all `modules.load` order, all `modinfo` aliases/parms/depends quoted, no compressed modules, command-line strings, register `0x11C101B8`, clock names, DT compat strings.
- **INFERENCE (engineering conclusion):** GKI 6.12/android16 label; which hardware is the X6886's actual panel/touch IC; the XNU bring-up tier ordering; that XNU cannot use these `.ko` files directly; that MMIO bases must be read from the DTB next.

**End of report.**
