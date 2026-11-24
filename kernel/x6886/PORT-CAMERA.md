# X6886 camera infrastructure port manifest (inspect-only)

Branch `feat/x6886-real-port`, 100% offline: no adb, no fastboot, no build, no flash.
Only `kernel/x6886/hakios.x6886_camera.c` and this manifest were written.

## Source and an explicit missing-source finding
Real MTK logic: `refs/oosexgang-xnu-kernel-mtk-helio/kernel/overlay/bsd/dev/mtk_camera.c`
(bounded window lookup / rd32 / wr32 `:71-117`; PMIC WACS2 transaction and lock `:125-190`;
GPIO save/restore `:214-263`).
The driver's own header says the 496-write sensor table and the 66-word SENINF/CAMSV RX table
live in `mtk_camera_jn1.h` / `mtk_camera_rx.h` (`:22-24`), guarded by `__has_include` (`:51-57`).
**Neither header exists anywhere in osexgang** (only `userland/src/camera_isp.h`,
`userland/src/hakios_camera.h`, `bsd/sys/hakios_camera.h` exist). The sensor register sequence for
`gc13a0ff` / `imx882` is therefore **missing source** and is NOT fabricated. This port maps and
validates the hardware and reports sensor identity; it does not stream, capture or program a sensor.

## Constants (file:line)
| Constant | Value | Proof |
|---|---|---|
| camsys | `0x1a000000` | `device-dump/fdt.dts:1841` |
| seninf1..6 | `0x1a004000`..`0x1a009000` | `fdt.dts:3165-3193` |
| seninf_top | `0x1a004000` | `fdt.dts:3195-3196` |
| camsv2..7 legacy | `0x1a092000`..`0x1a097000` | `fdt.dts:7087-7136` |
| topckgen / infracfg | `0x10000000` / `0x10001000` | `fdt.dts:2354-2361` |
| gpio | `0x10005000` | `fdt.dts:2376` |
| pwrap | `0x10026000` | `fdt.dts:3219-3220` |
| rear imx882 | i2c2 `0x11eb0000`, addr `0x2a` | `fdt.dts:45,4668,4693-4697`; cam0 `:1912` |
| front gc13a0ff | i2c4 `0x11eb1000`, addr `0x1a` | `fdt.dts:47,4827,4851-4855`; cam1 `:1913` |
| MT6366 VCAMIO / VCAMD / VCAMA1 | regulator nodes | `fdt.dts:9106-9107,9121` |
| MCLK pin groups | cam0/cam1 | `fdt.dts:2976-3057` |
| window sizes / pwrap offsets | `0x20000`, `0x2000`, `0x53000`; `0xc20/0xc24/0xc28` | `mtk_camera.c:75-80,127-129` |

## Boot safety and gaps
- `x6886_cam=1` opt-in; any unmapped window or unready pwrap is a clean error return, no panic.
- Every MMIO access goes through the bounded `cam_reg()` window check (`mtk_camera.c:88-99`).
- Only pwrap **reads** of VCAMIO/VCAMD enable registers; the reference's rail writes
  (`mtk_camera.c:198-212`) are not ported. MT6358-compatible PMIC offsets need owner sign-off.
- No sensor init, no CSI lane setup, no CAMSV DMA, no frame buffer: those need the missing vendor
  tables. Reported on every probe line rather than papered over with a stub.
- Every diagnostic goes to printf and pstore `0x480af000` via `X6886_LOG` (`[X6886-camera]`).
