# X6886 driver bring-up matrix

Offline audit, 24 Nov 2026. Every value comes from a local file or the stock dump. "WORKING-IN-LOADER" means the bare-metal handoff code does it before XNU starts. A reference from another MT6789 phone is not evidence that X6886 does it.

| Subsystem | X6886 Hardware / DT Node | Reference Source in refs/ | Current Status | Phase | Verification Signal |
| --- | --- | --- | --- | --- | --- |
| 1. Loader handoff + early console | fb 0xfe0c0000 pitch 4352; OVL0 0x14005000; ramoops console 0x480af000 size 0x40000. The `0xfeab6000` / 4320 layer-1 claim has no local source; HelioDarwin's reference is 0xfeabe000 / 4320 (`refs/heliodarwin/wiki/Display-Pipeline.md:14`) | `build/loader/mtkloader.c:2747` banner, `:1574` EL1 fault dump (ESR, ELR, FAR), `:1933` XNU return; `devices/x6886/board.toml:33-36`; `build/boards/x6886/hakios_platform.h:76-79`; `docs/HANDOFF-IOS27-BOOT.md:38` | WORKING-IN-LOADER | 1 | Panel text `=== HakiOS xnu-12377 handoff ===`; `/sys/fs/pstore/console-ramoops-0` at 0x480af000; fault line `ESR ... FAR ...` |
| 2. CPU / GICv3 / arch timers / SMP | 2x A76 + 6x A55; GICD 0x0c000000, GICR 0x0c040000; CNTFRQ 13 MHz; EL1 virtual timer PPI 27. The 1999 MHz claim has no local source | `refs/unixnu-mt6789/dts/mt6789.dtsi:8,15-16,19-29`; `refs/oosexgang-xnu27/kernel/overlay/osfmk/arm64/gic_v3_irq.c:75,83-89`; `refs/oosexgang-xnu27/docs/porting.md:129-135`; `build/loader/mtkloader.c:2242,2315,2913` | WORKING-IN-LOADER | 1 | `timer probe at EL1:` then `ICC_CTLR_EL1 ... (EOImode clear)`; a timer actually taken |
| 3. UFS + APFS root | `ufshci@11270000`; infracfg 0x10001000 resets 0x130/0x134/0x140/0x144; SMC 0x82000276 (`refs/klios716/.../0107-kl7-mtk-ufs.patch:37`) and 0xc2000276 (`refs/heliodarwin/.../0141-x676c-ufs-driver.patch:754`), both present in `otadump/images/tee.img` at 0x2c7c8 / 0x2c7cc; refclk 0x144 PRE+POST; DME_LINK_STARTUP; fDeviceInit; main LUN 2; APFS in GPT `super` | `refs/klios716/xnu/patches/0107-kl7-mtk-ufs.patch:41-49,97-98,163,243-246,364-377`; `build/loader/ufs.h:47-53,163,626`; `build/loader/mtkloader.c:828,860`; `refs/oosexgang-xnu27/docs/drivers.md:15` | WORKING-IN-LOADER | 1 | `UFS init: ...`, `UFS <step>: VER ... HCE ... REFCLK ...`, then `super at block ... blocks ...` / `super: raw APFS container at offset 0` (`super: raw APFS container (NXSB at +0x20)` in the current tree) |
| 4. Display pipeline | OVL0 0x14005000, OVL0_2L 0x14006000, DSI0 0x14013000; BOE `nt37706a_fhdp_dsi_vdo_dsc_boe_boe_144hz_x6886`, 1080x2400, DSI Video + DSC, 144 Hz. LK owns scanout and backlight; no XNU DSI or backlight driver here | `refs/heliodarwin/wiki/Display-Pipeline.md:12-15`; `refs/heliodarwin/xnu/patches/0146-x676c-hardware-dsi-and-safe-sampling.patch:61,65`; `refs/heliodarwin/xnu/patches/0188-x676c-trace-ui-scanout-and-diagnostic-reset.patch:172`; `device-dump/fdt.dts:93`; `docs/HARDWARE.md:36-46` | WORKING-IN-LOADER | 1 | Banner visible on glass; `SWEET FB TRACE: OVL src ... L0 ... L1 ...` is reference-only from X676C |
| 5. Touch + physical buttons | `touch@0` under `spi1@11010000`, FocalTech FTS, 8 MHz; power/volume via MT6366 and keypad 0x10010000. Loader touch-firmware path has no local `touch_fw` declaration | `device-dump/dt-nodes.txt:3344-3384`; `docs/HARDWARE.md:52-58`; `refs/heliodarwin/wiki/Input-and-HID.md:26,49-53`; `refs/heliodarwin/xnu/patches/0175-x676c-mtk-keypad-and-focaltech-i2c-with-unlock-swipe.patch:70,129,298` (I2C reference) | NOT-PORTED | 2 | `SWEET TOUCH: MTK FocalTech selected...` and `SWEET KEYS: MTK Keypad polling started` are reference-only; no X6886 SPI driver log |
| 6. Power (PMIC, gauge, charging, vibrator, flashlight) | `pwrap@10026000` MT6366; charger chips sc8950 / sc8548 in overlays; gauge and flashlight nodes in live FDT. No X6886 charging, haptic or flashlight driver in tree | `refs/heliodarwin/wiki/MT6789-GPU-Power-Sequence.md:39,43-55`; `refs/heliodarwin/wiki/Input-and-HID.md:49-55`; `refs/oosexgang-xnu27/docs/drivers.md:15`; `docs/HARDWARE.md:78-82` | NOT-PORTED | 2 | `SWEET BATTERY: SPMI arb=... observer reads only` is reference-only; no X6886 log |
| 7. Graphics | `mali@13000000`, Mali-G57 MC2 Valhall v9 Job Manager, GPU_ID 0x90930010, no CSF firmware. HakiOS's metal component targets Adreno through freedreno/ir3; no Mali Metal in the active tree | `refs/heliodarwin/wiki/MT6789-GPU-Power-Sequence.md:3,19,33`; `refs/heliodarwin/wiki/MaliMetal-Driver.md:179`; `refs/heliodarwin/MaliMetal/MaliMetalDevice.m`; `refs/hakios-common/components/metal/docs/air.md:3-5`; `docs/HARDWARE.md:62-69` | NOT-PORTED | 3 | `GPU_ID = 0x90930010` from HelioDarwin is reference-only; no Mesa, OpenGL or compositor signal on X6886 |
| 8. Connectivity | `consys@18002000`, `wifi@18000000`. No XNU cellular/RIL source locally; HelioDarwin lists modem transport as missing | `docs/HARDWARE.md:74-76`; `refs/oosexgang-xnu27/docs/drivers.md:16`; `refs/oosexgang-xnu27/docs/bluetooth-porting.md:1-5`; `refs/heliodarwin/wiki/System-Services-and-Posters.md:277` | NOT-PORTED | 3 | `connsys_step=N` / `connsys_bt=N` lines are reference-only; no X6886 CONNSYS log |
| 9. Audio (speaker, earpiece, mic) | No X6886 audio DT node or driver cited in local refs. Stock dmesg and `X6886-specs.txt` list audio feature flags only | `refs/oosexgang-xnu27/docs/boot-args.md:119-125`; `refs/oosexgang-xnu27/docs/drivers.md:37,41`; `device-dump/X6886-specs.txt:450-458` | UNVERIFIED | 3 | No X6886 signal. `ringer sounding at 29 s` in `refs/oosexgang-xnu27/docs/air.md:592` is Qualcomm reference only |
| 10. Sensors | "27 sensors" claim has no local source. Live FDT has no gsensor/gyro/alsps node; only the SCP sensor hub is described, with accelerometer samples read on X6826B | `refs/hakios-mtk/docs/sensors-x6826b.md:6-7,27-44`; `refs/oosexgang-xnu27/docs/boot-args.md:107`; `refs/oosexgang-xnu27/docs/drivers.md:14` | UNVERIFIED | 3 | `kern.hakios.accel` after `scp_step=7` is reference-only; no X6886 sensor log |
| 11. Camera | Camera pins, main/sub sensors and flashlight from the overlay; `X6886-specs.txt` lists rear/front cameras. No X6886 camera driver or sensor table in tree | `refs/oosexgang-xnu27/docs/camera-porting.md:1-5,25-45`; `refs/oosexgang-xnu27/docs/drivers.md:18`; `device-dump/X6886-specs.txt:116-174` | NOT-PORTED | 3 | No X6886 capture log. `camera_test` and `S5K JN1` capture are reference-only |

## Phase 2: `super` as the live XNU root

Documented only; no Phase 1 code or image layout changes. Full detail in
`docs/SUPER-IMAGE.md`.

`load_system_image()` (`mtkloader.c:841-914`) reads the whole APFS container
from UFS into DRAM at `SYSTEM_PA` and hands it to XNU as md0. Its guard
`SYSTEM_PA + size > XNU_PHYS_BASE + XNU_MEM_SIZE - 64 MiB` (`:911`) caps a
container at 3.803 GiB, so the 4.002 GiB `system27-prune.apfs` is rejected with
`APFS container too large for the XNU window, using embedded md0`. This is the
loader's preload model, not an XNU/UFS block-driver limit. Two Phase 2 paths:

- **(a) Enlarge the XNU window** in `devices/x6886/board.toml` (`xnu_base` /
  `xnu_size`, mirrored in `build/boards/x6886/hakios_platform.h`) so the whole
  container fits within real RAM; verification: the `APFS container too large`
  branch is not taken and the container reads into `SYSTEM_PA`.
- **(b) Mount `super` directly from the kernel**: pass an LBA/device to XNU
  instead of preloading, and let the kernel's UFS driver read APFS on demand.
  Needs the XNU MTK UFS driver wired (`ufs=2` / `root_apfs=ufs_darwin`
  shared-page export); verification: `/dev/ufs_darwin` mounts at `/flash` with
  no 4 GiB DRAM copy.

`super_apfs_probe()` (`mtkloader.c:807-838`) accepts raw APFS at `super` LBA 0
(`NXSB` at offset 32) without a HAKIOS wrapper, so no repackaging is needed for
either path.

## Phase 1 critical path

1. Flash `boot_a` and let stock LK chain the loader at 0x40010000.
2. Panel must show `=== HakiOS xnu-12377 handoff ===`; pstore must receive it at 0x480af000.
3. GIC EOImode fix and `timer probe at EL1` must show virtual timer PPI 27 taken.
4. UFS must pass HCE, infracfg reset, SMC 0x82000276, refclk PRE+POST, DME_LINK_STARTUP and fDeviceInit, then read LUN 2 GPT `super` and APFS `NXSB`.
5. Hand XNU DRAM `0x140000000 + 0x0ff600000`, clean caches at EL1 and EL2, keep the 30 s TOPRGU watchdog for fallback.
