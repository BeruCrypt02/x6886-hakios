# X6886 board package (HakiOS MTK / oosexgang layout)

Infinix Hot 60 Pro+ — MT6789V/T (Helio G200), UFS storage, 8 GB LPDDR4X,
1080x2400 AMOLED. Stock XOS 16 / Android 16.

This package follows the `devices/_template` layout of
`refs/oosexgang-xnu27`: `board.toml` (this device), `socs/mt6789/soc.toml` (the
chip). Every address is from the live FDT of serial `153857057T001452` or
the stock dump; evidence is in `devices/x6886/evidence/HARDWARE-IDENTITY.md` and
`docs/reference/dump-analysis/FINDINGS.md`. The earlier DTS files in `devices/x6886/legacy/` are
from the superseded `hakios-common` layout and are kept for reference only.

## Status (nothing enabled yet)

- `features = []` — no driver starts until it has been seen to work here.
- Storage is **UFS** (`ufshci@11270000`), not eMMC. The XNU UFS driver already
  exists and builds (`xnu-12377-hakios/bsd/dev/board/ufshc.c`, `ufs.c`); the
  loader side (`mtkloader`) must pass the controller address, added as
  `ufs = 0x11270000` in `socs/mt6789/soc.toml`.
- Panel: nt37706a (BOE), 144 Hz, DSI video + DSC. Touch: FocalTech FT3683G
  on spi1. Fingerprint: Goodix optical FOD on spi3.

## Verified constants

| What | Value | Source |
|---|---|---|
| GICD / GICR | 0x0c000000 / 0x0c040000 | live FDT + /proc/iomem |
| Kernel load | 0x40010000 | /proc/iomem "Kernel code" |
| LK framebuffer | 0xfe0c0000, 0x1f30000 | atag,videolfb + mblock-13 |
| ramoops | 0x48090000, size 0xe0000 | cmdline + mblock-2-pstore |
| UFS | 0x11270000 | live FDT ufshci@11270000 |
| GPIO | 0x10005000 | pinctrl, 188 pins |
| XNU window | 0xc0000000, 0x14000000 | free System RAM in /proc/iomem |

## Open (need the device or vendor source)

- Marker/panic-log/scratch addresses are placeholders, confirm with fbdiag.
- Sensor orientation matrix (compiled into `scp.img`).
- Goodix FOD register map (encrypted MobiCore TA).
