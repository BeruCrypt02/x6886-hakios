# X6886 hardware from the dump

Source: `vendor_boot/dtb` and `images/dtbo.img` in `/home/berucrypt00/otadump`. MediaTek wraps the FDT with a 64-byte header, so strip 64 bytes before `dtc`. Decompiled trees are in `board/x6886-base.dts` and `board/x6886-overlays.dts`.

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

- Two suppliers: FocalTech `ft3682g` and ChipSemi `chct5560`.
- FocalTech node `touch@0`: IRQ GPIO `0x0c`, reset GPIO `0x74`, IOVDD GPIO `0x0f`, AVDD GPIO `0x96`.
- Display coordinates: `0x437f` x `0x95ff` raw touch units.

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
