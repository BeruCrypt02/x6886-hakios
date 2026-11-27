# X6886 keypad / MT6366 keys port manifest

Branch `feat/x6886-real-port`, 100% offline: no adb, no fastboot, no build, no flash.
Only `drivers/input/hakios.x6886_keys.c` and this manifest were written.

## Source
Real MTK logic: `refs/oosexgang-xnu-kernel-mtk-helio/kernel/overlay/bsd/dev/mtk_keys.c`
(keypad bit-per-key + two-look debounce `:10-12,28,355-372`; WACS2 pwrap transaction `:30-36,63-99`).
The reference's CPU/GPU voltage writers (`:123-302`) are **not** ported: they are MT6358 values for
another board. No iscle source was read.

MT6366 power key, primary source (device-family, not a wiki note):
`refs/alps-device-modules-6.12/drivers/input/keyboard/mtk-pmic-keys.c`
(file is in the git object store; `drivers/input/keyboard/` is outside this checkout's sparse set).
`refs/alps-device-modules-6.12/arch/arm64/boot/dts/mediatek/mt6366_mt6789.dtsi:123-124`
declares `mt6366keys` / `mediatek,mt6366-keys` for this SoC. The legacy `mt6358keys` node at
`arch/arm64/boot/dts/mediatek/mt6366.dtsi:446-447` is not the one bound here.

## Constants (file:line)
| Constant | Value | Proof |
|---|---|---|
| keypad | `0x10010000`, size `0x1000` | `devices/x6886/evidence/fdt.dts:4002-4004` (`kp@10010000`, `mediatek,kp`) |
| map | `0x73` vol-up, `0x72` vol-down | `fdt.dts:4006` (`mediatek,hw-init-map`); `mtk_keys.c:7-8` |
| KP_MEM1 | `0x4` | `mtk_keys.c:28` |
| pwrap | `0x10026000`, size `0x1000` | `fdt.dts:3219-3220` (`mediatek,mt6789-pwrap`) |
| WACS2 cmd/rdata/vldclr | `0xc20/0xc24/0xc28` | `mtk_keys.c:31-33` |
| FSM idle / WFVLDCLR | `0` / `6` | `mtk_keys.c:34-36` |
| bounded wait | 100000 spins | `mtk_keys.c:66` |
| MT6366 keys node, power keycode `0x74` | — | `fdt.dts:3356-3362` |
| poll 20 ms / two looks | — | `mtk_keys.c:12,362-376` |
| MT6366 TOPSTATUS | `0x28` | `mtk-pmic-keys.c:44` (`MT6366_TOPSTATUS`), wired into `mt6366_regs` at `:170-175` |
| PWRKEY debounce mask | `0x1` (bit 0), active low | `mtk-pmic-keys.c:47` (`MT6366_PWRKEY_DEB_MASK`), `key_deb &= deb_mask; pressed = !key_deb;` at `:464-470` |
| of_match for this PMIC | `mediatek,mt6366-keys` | `mtk-pmic-keys.c:681-682`; dtsi `mt6366_mt6789.dtsi:123-124` |

## Resolution of the 0x28 vs 0x0e conflict
- `refs/heliodarwin/wiki/Input-and-HID.md:49`: "MT6366 PMIC `TOPSTATUS` register `0x28`, active-low
  bit 1". Register address matches the alps primary source.
- `refs/klios716/xnu/NOTES.md:705`: "TOPSTATUS `0x0e`". That is not an address: the same board's
  patch `refs/klios716/xnu/patches/0113-kl7-keys-vibrator.patch:83` defines
  `MT6358_TOPSTATUS 0x0028u` and `:222` reads it into `topstatus`. `0x0e` is the value read
  (idle bits set), so the two references never actually disagreed on the register address.
- The alps MT6366 driver proves `0x28` for `mediatek,mt6366-keys`, which is the node this SoC uses.
  The power key is therefore **enabled by default** under `x6886_keys=1`; `x6886_pwrkey=0` is an
  explicit opt-out. Volume keys are unchanged.
- Bit correction (primary source wins over the refs): the osexgang reference uses `(1u << 1)`
  (`mtk_keys.c:39`), klios716 uses `topstatus & 2u` (patch `:371`), and the heliodarwin wiki says
  "bit 1". The alps MT6366 driver defines `MT6366_PWRKEY_DEB_MASK 1` (`:47`) and applies it as a
  literal bitmask with active-low polarity (`:469-470`). The driver now uses `PWRKEY_DEB 0x1u`.
- Attribution: the earlier 5-line paragraph describing this conflict landed in commit `d145ac2`
  (the touch commit) inside this manifest. Its content was correct and is superseded here;
  no history rewrite was made.

## Boot safety and gaps
- `x6886_keys=1` opt-in; a map failure or unready pwrap is a clean `ENXIO`/no-start, no panic.
- Every PMIC read failure is treated as "not pressed" (`top = PWRKEY_DEB`), no panic.
- Polling thread has a hard tick cap (`x6886_keys_ticks`, max `0x00ffffff`), no unbounded `for(;;)`;
  pwrap waits are bounded to 100000 spins.
- Every diagnostic goes to printf and to pstore `0x480af000` via `X6886_LOG` (`[X6886-keys]`,
  signature `0x43474244`).
- Remaining gap, stated rather than guessed: on-device confirmation of the bit polarity is not
  possible offline. The vendor driver sets `release_irq = true` for MT6366 (`mtk-pmic-keys.c:181`,
  probe `:769-775`), so its Android path bypasses the TOPSTATUS debounce read; this XNU port polls,
  so it consumes the register definition directly from the same source. No iscle source was involved.
