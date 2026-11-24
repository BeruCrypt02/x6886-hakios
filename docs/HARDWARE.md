# X6886 hardware

Two sources, both decompiled with `dtc`:
- Stock dump: `vendor_boot/dtb` and `images/dtbo.img` in `/home/berucrypt00/otadump`. MediaTek wraps the FDT with a 64-byte header, so strip 64 bytes before `dtc`. Decompiled trees are in `board/x6886-base.dts` and `board/x6886-overlays.dts`.
- Live device: `/sys/firmware/fdt` from serial `153857057T001452`, decompiled to `device-dump/fdt.dts`, plus the Device Info HW+ PDF in `device-dump/X6886-specs.txt`. The verified hardware identity is recorded in `device-dump/HARDWARE-IDENTITY.md`.

Where the two disagree, the live FDT wins because it has the overlays applied.

## SoC and boot

- GICv3, interrupt controller node present.
- System timer `systimer@10017000`, compatible `mediatek,mt6789-timer`.
- Arch timer node at `/timer`.
- CPU topology: 2 A76 + 6 A55 from the spec. Reference HelioDarwin publishes six A55 and excludes A76 for its first boot; that is a reference, not a requirement for X6886.
- Kernel page size in HelioDarwin reference: 16 KiB.

## Storage

- UFS host `ufshci@11270000`, compatible `mediatek,mt8183-ufshci`, size `0x2300`, IRQ 0x89.
- eMMC controller `mmc@11230000` also present but unused for boot.

## Display

- MMSYS base `0x14000000`.
- OVL0 `0x14005000`.
- RDMA0 `0x14007000`.
- DSI0 `0x14013000`.
- Dither `0x1400f000`, postmask `0x1400e000`, DSC `0x14012000`.
- LK framebuffer address from `atag,videolfb-fb_base_l`: `0x7e605000` as raw FDT hex bytes; byte order needs checking before use.
- VRAM size `0x1be0000`, FPS value `0x1770`.
- Panels in overlays: `nt37706a_fhdp_dsi_vdo_dsc_boe_boe_144hz_x6886` and `vtdr6126a_fhdp_dsi_vdo_dsc_tm_tm_144hz_x6886`. Both 1080x2400, DSI video mode, DSC, 144 Hz.

## Touch

- Two suppliers: FocalTech `ft3682g` and ChipSemi `chct5560`. The live FDT's `touch,lcm_name` selects the BOE panel and therefore the FocalTech touch controller.
- FocalTech node `touch@0` lives under `spi1@11010000`, not I2C: IRQ GPIO `0x0c`, reset GPIO `0x74`, IOVDD GPIO `0x0f`, AVDD GPIO `0x96`, 8 MHz.
- Display coordinates: `0x437f` x `0x95ff` raw touch units (17279 x 38400), mapped to 1080x2400.

## Fingerprint

- The spec PDF's `tran_keys` is a Transsion name; the live FDT node `fingerprint@0` under `spi3@11013000` carries `compatible = "tran_fp_legacy"` with a `goodix` flag and alias `goodix_fp = "/soc/fingerprint"`. The silicon is Goodix, not Silead and not the FocalTech touch chip.
- SPI 27 MHz (`0x19bfcc0`), IRQ GPIO `0x0d`, reset GPIO `0x77`, power GPIO `0x3b`, FOD centre `540,2210`.
- Goodix register map and init are not in the stock FDT. Need a datasheet, firmware blob from otadump, or a logic capture before writing a driver.

## GPU

- Mali `mali@13000000`, compatible `mediatek,mali` and `arm,mali-valhall`, region `0x4000`.
- Five interrupts: GPU, MMU, JOB, EVENT, PWR.
- GPU frequency block `gpufreq` with registers at `0x13fbf000`, `0x10006000`, `0x1021c000`, `0x1020e000`, `0x10042000`, `0x10001000`, `0x10023000`, `0x1002b000`, `0x10000000`, `0x11c10000`, `0x10215000`.

## Power and connectivity

- PMIC wrapper `pwrap@10026000`, PMIC MT6366.
- CONNSYS `consys@18002000`, compatible `mediatek,mt6789-consys`.
- Wi-Fi `wifi@18000000`.
- Battery and charger data in overlays: sc8950 and sc8548.

## Reference comparison

HelioDarwin X676C (same SoC family) reports:
- kernel RAM window `0x140000000`, size `0xffff0000`;
- GICD `0x0c000000`, GICR `0x0c040000`;
- counter 13 MHz, virtual timer PPI 11 / INTID 27;
- LK framebuffer `0xfeabe000`, 4320-byte stride;
- UFS host `0x11270000`;
- ramoops console `0x4811f000`, size `0x40000`.

These values are reference data. Each one must be checked against the X6886 tree before use.
In particular, X6886's live ramoops console is `0x480af000` (the non-zero `ftrace_size=0x1000`
shifts it 4 KiB below the X676C value), and its framebuffer is `0xfe0c0000` with a 4352-byte stride.
