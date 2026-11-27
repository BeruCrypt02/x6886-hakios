# X6886 driver bring-up matrix

Offline audit, 24 Nov 2026. Every value comes from a local file or the stock dump. "WORKING-IN-LOADER" means the bare-metal handoff code does it before XNU starts. A reference from another MT6789 phone is not evidence that X6886 does it.

| Subsystem | X6886 Hardware / DT Node | Reference Source in refs/ | Current Status | Phase | Verification Signal |
| --- | --- | --- | --- | --- | --- |
| 1. Loader handoff + early console | fb 0xfe0c0000 pitch 4352; OVL0 0x14005000; ramoops console 0x480af000 size 0x40000. The `0xfeab6000` / 4320 layer-1 claim has no local source; HelioDarwin's reference is 0xfeabe000 / 4320 (`refs/heliodarwin/wiki/Display-Pipeline.md:14`) | `build/loader/mtkloader.c:2747` banner, `:1574` EL1 fault dump (ESR, ELR, FAR), `:1933` XNU return; `devices/x6886/board.toml:33-36`; `build/boards/x6886/hakios_platform.h:76-79`; `docs/HANDOFF-IOS27-BOOT.md:38` | WORKING-IN-LOADER | 1 | Panel text `=== HakiOS xnu-12377 handoff ===`; `/sys/fs/pstore/console-ramoops-0` at 0x480af000; fault line `ESR ... FAR ...` |
| 2. CPU / GICv3 / arch timers / SMP | 2x A76 + 6x A55; GICD 0x0c000000, GICR 0x0c040000; CNTFRQ 13 MHz; EL1 virtual timer PPI 27. The 1999 MHz claim has no local source | `refs/unixnu-mt6789/dts/mt6789.dtsi:8,15-16,19-29`; `refs/oosexgang-xnu27/kernel/overlay/osfmk/arm64/gic_v3_irq.c:75,83-89`; `refs/oosexgang-xnu27/docs/porting.md:129-135`; `build/loader/mtkloader.c:2242,2315,2913` | WORKING-IN-LOADER | 1 | `timer probe at EL1:` then `ICC_CTLR_EL1 ... (EOImode clear)`; a timer actually taken |
| 3. UFS + APFS root | `ufshci@11270000`; infracfg 0x10001000 resets 0x130/0x134/0x140/0x144; SMC 0x82000276 (`refs/klios716/.../0107-kl7-mtk-ufs.patch:37`) and 0xc2000276 (`refs/heliodarwin/.../0141-x676c-ufs-driver.patch:754`), both present in `otadump/images/tee.img` at 0x2c7c8 / 0x2c7cc; refclk 0x144 PRE+POST; DME_LINK_STARTUP; fDeviceInit; main LUN 2; APFS in GPT `super` | `refs/klios716/xnu/patches/0107-kl7-mtk-ufs.patch:41-49,97-98,163,243-246,364-377`; `build/loader/ufs.h:47-53,163,626`; `build/loader/mtkloader.c:828,860`; `refs/oosexgang-xnu27/docs/drivers.md:15` | WORKING-IN-LOADER | 1 | `UFS init: ...`, `UFS <step>: VER ... HCE ... REFCLK ...`, then `super at block ... blocks ...` / `super: raw APFS container at offset 0` (`super: raw APFS container (NXSB at +0x20)` in the current tree) |
| 4. Display pipeline | OVL0 0x14005000, OVL0_2L 0x14006000, DSI0 0x14013000; BOE `nt37706a_fhdp_dsi_vdo_dsc_boe_boe_144hz_x6886`, 1080x2400, DSI Video + DSC, 144 Hz. LK owns scanout and backlight; no XNU DSI or backlight driver here | `refs/heliodarwin/wiki/Display-Pipeline.md:12-15`; `refs/heliodarwin/xnu/patches/0146-x676c-hardware-dsi-and-safe-sampling.patch:61,65`; `refs/heliodarwin/xnu/patches/0188-x676c-trace-ui-scanout-and-diagnostic-reset.patch:172`; `devices/x6886/evidence/fdt.dts:93`; `docs/reference/hardware.md:36-46` | WORKING-IN-LOADER | 1 | Banner visible on glass; `SWEET FB TRACE: OVL src ... L0 ... L1 ...` is reference-only from X676C |
| 5. Touch + physical buttons | `touch@0` under `spi1@11010000`, FocalTech FTS, 8 MHz; power/volume via MT6366 and keypad 0x10010000. Loader touch-firmware path has no local `touch_fw` declaration | `devices/x6886/evidence/dt-nodes.txt:3344-3384`; `docs/reference/hardware.md:52-58`; `refs/heliodarwin/wiki/Input-and-HID.md:26,49-53`; `refs/heliodarwin/xnu/patches/0175-x676c-mtk-keypad-and-focaltech-i2c-with-unlock-swipe.patch:70,129,298` (I2C reference) | NOT-PORTED | 2 | `SWEET TOUCH: MTK FocalTech selected...` and `SWEET KEYS: MTK Keypad polling started` are reference-only; no X6886 SPI driver log |
| 6. Power (PMIC, gauge, charging, vibrator, flashlight) | `pwrap@10026000` MT6366; charger chips sc8950 / sc8548 in overlays; gauge and flashlight nodes in live FDT. No X6886 charging, haptic or flashlight driver in tree | `refs/heliodarwin/wiki/MT6789-GPU-Power-Sequence.md:39,43-55`; `refs/heliodarwin/wiki/Input-and-HID.md:49-55`; `refs/oosexgang-xnu27/docs/drivers.md:15`; `docs/reference/hardware.md:78-82` | NOT-PORTED | 2 | `SWEET BATTERY: SPMI arb=... observer reads only` is reference-only; no X6886 log |
| 7. Graphics | `mali@13000000`, Mali-G57 MC2 Valhall v9 Job Manager, GPU_ID 0x90930010, no CSF firmware. HakiOS's metal component targets Adreno through freedreno/ir3; no Mali Metal in the active tree | `refs/heliodarwin/wiki/MT6789-GPU-Power-Sequence.md:3,19,33`; `refs/heliodarwin/wiki/MaliMetal-Driver.md:179`; `refs/heliodarwin/MaliMetal/MaliMetalDevice.m`; `refs/hakios-common/components/metal/docs/air.md:3-5`; `docs/reference/hardware.md:62-69` | NOT-PORTED | 3 | `GPU_ID = 0x90930010` from HelioDarwin is reference-only; no Mesa, OpenGL or compositor signal on X6886 |
| 8. Connectivity | `consys@18002000`, `wifi@18000000`. No XNU cellular/RIL source locally; HelioDarwin lists modem transport as missing | `docs/reference/hardware.md:74-76`; `refs/oosexgang-xnu27/docs/drivers.md:16`; `refs/oosexgang-xnu27/docs/bluetooth-porting.md:1-5`; `refs/heliodarwin/wiki/System-Services-and-Posters.md:277` | NOT-PORTED | 3 | `connsys_step=N` / `connsys_bt=N` lines are reference-only; no X6886 CONNSYS log |
| 9. Audio (speaker, earpiece, mic) | No X6886 audio DT node or driver cited in local refs. Stock dmesg and `X6886-specs.txt` list audio feature flags only | `refs/oosexgang-xnu27/docs/boot-args.md:119-125`; `refs/oosexgang-xnu27/docs/drivers.md:37,41`; `devices/x6886/evidence/X6886-specs.txt:450-458` | UNVERIFIED | 3 | No X6886 signal. `ringer sounding at 29 s` in `refs/oosexgang-xnu27/docs/air.md:592` is Qualcomm reference only |
| 10. Sensors | "27 sensors" claim has no local source. Live FDT has no gsensor/gyro/alsps node; only the SCP sensor hub is described, with accelerometer samples read on X6826B | `refs/hakios-mtk/docs/sensors-x6826b.md:6-7,27-44`; `refs/oosexgang-xnu27/docs/boot-args.md:107`; `refs/oosexgang-xnu27/docs/drivers.md:14` | UNVERIFIED | 3 | `kern.hakios.accel` after `scp_step=7` is reference-only; no X6886 sensor log |
| 11. Camera | Camera pins, main/sub sensors and flashlight from the overlay; `X6886-specs.txt` lists rear/front cameras. No X6886 camera driver or sensor table in tree | `refs/oosexgang-xnu27/docs/camera-porting.md:1-5,25-45`; `refs/oosexgang-xnu27/docs/drivers.md:18`; `devices/x6886/evidence/X6886-specs.txt:116-174` | NOT-PORTED | 3 | No X6886 capture log. `camera_test` and `S5K JN1` capture are reference-only |
| 12. Under-display optical fingerprint | Goodix optical sensor under the BOE AMOLED. `/soc/spi3@11013000/fingerprint@0`: `compatible = "tran_fp_legacy"`, `goodix;`, `spi-max-frequency = <0x19bfcc0>` (27 MHz), CS0, IRQ GPIO13, reset GPIO119, power GPIO59, `fod_location_xy = "540,2210"`. Live props: `persist.vendor.fingerprint.sensor_type=optical`, `ro.optical_fingerprint_support=1`. SKU probably GF3976; the dump cannot prove which Goodix part is soldered. Matching is inside a TEE trustlet | `devices/x6886/evidence/fdt.dts:4549-4582`; `docs/reference/dump-analysis/gap-goodix-fod.md:16-53`; `docs/reference/dump-analysis/fingerprint.md:62-67`; `devices/x6886/legacy/board.dts:54-64`; `devices/x6886/legacy/soc.dtsi:39` | NOT-PORTED | 4 | No XNU probe line. Phase 4 design below. No kernel SPI driver, no Theos tweak, no HBM spot control in this tree |

## Phase 2: `super` as the live XNU root

Path (b) is now wired in the loader, but has no X6886 hardware log yet. Full
image detail stays in `docs/SUPER-IMAGE.md`; the boot-arg form is in
`plan/super-image-spec.md`.

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
- **(b) Mount `super` directly from the kernel**: done in the loader. After
  `super_apfs_probe()` sees `NXSB`, `ufs_darwin_publish()` fills a shared page
  whose `darwin` entry covers the whole `super` partition at LBA 0, and a PSCI
  server core answers XNU's reads with no DRAM copy. Boot args become
  `ufs=2 root_apfs=ufs_darwin ufs_shared_pa=0x...`. Verification: an X6886 log
  with `block server ... running`, a `ufs: ... /dev/ufs_NAME` line from XNU, and
  `/dev/ufs_darwin` mounted at `/flash`.

`super_apfs_probe()` keeps the HAKIOS wrapper check and the raw fallback. Raw
APFS at `super` LBA 0 has `NXSB` at offset 32, so no repackaging is needed.
If the probe fails or the server core does not start, boot args stay as Phase 1
and XNU mounts the embedded md0.

## Phase 1 critical path

1. Flash `boot_a` and let stock LK chain the loader at 0x40010000.
2. Panel must show `=== HakiOS xnu-12377 handoff ===`; pstore must receive it at 0x480af000.
3. GIC EOImode fix and `timer probe at EL1` must show virtual timer PPI 27 taken.
4. UFS must pass HCE, infracfg reset, SMC 0x82000276, refclk PRE+POST, DME_LINK_STARTUP and fDeviceInit, then read LUN 2 GPT `super` and APFS `NXSB`.
5. Hand XNU DRAM `0x140000000 + 0x0ff600000`, clean caches at EL1 and EL2, leave TOPRGU disabled at handoff. Power cycle or Penumbra BROM recovers a hung run.

## Phased rollout

Phase 1 covers the handoff only. The loader boots XNU directly, drives the early
console at `0x480af000`, sets up GIC and arch timers, brings up UFS through LUN 2
with the GPT and APFS probe, and installs VBAR_EL1 fault vectors. No display,
touch, power, audio, sensor, or camera driver runs in XNU yet. The loader draws
the panel banner. XNU uses the 4 MiB embedded md0; the 4.002 GiB
`system27-prune.apfs` stays out of the flashed image because the preload cap
rejects it.

Phase 2 covers input and the live root. Port FocalTech touch on `spi1`, power
and volume keys, and the MT6366 power path. Keep the loader framebuffer as the
console until each driver prints an X6886 probe line. Wire the XNU UFS shared
page and direct `super` mount from
`plan/phase2-ufs-darwin-reference.patch`, then move XNU from embedded md0 to
`/dev/ufs_darwin`.

Phase 3 covers graphics, audio, sensors, camera, and connectivity. Put each
driver behind its own boot arg so a failed probe leaves the Phase 1 handoff
intact. Mark a row DONE only after an X6886 log line exists. Reference logs
from X676C and HelioDarwin stay in the source column; they are not proof of a
working X6886 driver.

## Phase 4: under-display optical fingerprint (design only, NOT-PORTED)

Nothing in this section runs. It records what a port would have to build, so
the work can be scoped without guessing at the hardware again.

### Hardware, from the stock dump

The live FDT puts the sensor on `spi3@11013000` at 27 MHz, chip select 0, IRQ
GPIO 13, reset GPIO 119 and power GPIO 59. `fod_location_xy` is `540,2210`;
Android's `persist.vendor.fingerprint.optical.sensorlocation` says `540::2172`.
The node carries `goodix;` and `tran_fp_sched`, with six `tran_fp_*` pinctrl
states. Stock `tran_fp.ko` owns `/dev/goodix_fp` and passes SPI through; the
matcher runs in a TEE trustlet. `tran_fp_v2.ko` has no consumer and its GF3976
trustlet is unused. The dump lists GF3956, GF3976, GF3986, GW9578 and GW9598
part names. Static evidence cannot pick the soldered part, and there is no
Goodix register map or init sequence in the dump.

### XNU driver (NOT-PORTED)

An XNU `IOSPI` client would bind `tran_fp_legacy`, bring up power/reset/IRQ
through the existing MT6789 GPIO path, clock `spi3`, then expose a character
device with the same ioctl surface as `gf_fingerprint.default.so`. Enrollment
and match stay in the TEE; this driver only moves frames. The first probe line
to aim for is a chip-ID read over SPI. Before any of that, the exact Goodix SKU
and its SPI command set must be recovered from the running stock stack.

### Theos tweak (NOT-PORTED)

A tweak would hook SpringBoard's lockscreen view and `LocalAuthentication` to
drive the optical animation: show the FOD spot at the reported coordinates,
call into the XNU character device for a capture, then forward the TEE result
to the auth callback. The same `LocalAuthentication` hook would cover
in-app authentication requests. An unlock or app-auth bypass is a deliberate
decision for the operator of this device, not a side effect of the driver;
this document does not implement one.

### BOE AMOLED HBM spot (NOT-PORTED)

The panel is BOE `nt37706a` at 1080x2400, 144 Hz, DSC. Activating a small
high-brightness spot for the sensor needs the panel's HBM window registers and
a backlight handshake. `LK` owns scanout in Phase 1, so this work lands after
the XNU display driver from Phase 3. No HBM register values are present in the
local dump.
