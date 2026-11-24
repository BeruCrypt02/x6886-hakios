# X6886 keypad / MT6366 keys port manifest

Branch `feat/x6886-real-port`, 100% offline: no adb, no fastboot, no build, no flash.
Only `kernel/x6886/hakios.x6886_keys.c` and this manifest were written.

## Source
Real MTK logic: `refs/oosexgang-xnu-kernel-mtk-helio/kernel/overlay/bsd/dev/mtk_keys.c`
(keypad bit-per-key + two-look debounce `:10-12,28,355-372`; WACS2 pwrap transaction `:30-36,63-99`).
The reference's CPU/GPU voltage writers (`:123-302`) are **not** ported: they are MT6358 values for
another board. No iscle source was read.

## Constants (file:line)
| Constant | Value | Proof |
|---|---|---|
| keypad | `0x10010000`, size `0x1000` | `device-dump/fdt.dts:4002-4004` (`kp@10010000`, `mediatek,kp`) |
| map | `0x73` vol-up, `0x72` vol-down | `fdt.dts:4006` (`mediatek,hw-init-map`); `mtk_keys.c:7-8` |
| KP_MEM1 | `0x4` | `mtk_keys.c:28` |
| pwrap | `0x10026000`, size `0x1000` | `fdt.dts:3219-3220` (`mediatek,mt6789-pwrap`) |
| WACS2 cmd/rdata/vldclr | `0xc20/0xc24/0xc28` | `mtk_keys.c:31-33` |
| FSM idle / WFVLDCLR | `0` / `6` | `mtk_keys.c:34-36` |
| bounded wait | 100000 spins | `mtk_keys.c:66` |
| MT6366 keys node, power keycode `0x74` | — | `fdt.dts:3356-3362` |
| poll 20 ms / two looks | — | `mtk_keys.c:12,362-376` |

## Boot safety and gaps
- `x6886_keys=1` opt-in; a map failure or unready pwrap is a clean `ENXIO`/no-start, no panic.
- Power key is **off by default**: `TOPSTATUS 0x28` / bit 1 comes from `mtk_keys.c:38-39`
  (MT6358-compatible register) and the mt6366 node only declares a `mediatek,mt6358-gauge` sibling
  (`fdt.dts:3870`), not a verified MT6366 key register. `x6886_pwrkey=1` enables it with an explicit
  owner-sign-off log. This is the "MT6366, not MT6358" PMIC correction: the wrapper base is
  `0x10026000` from this device, never the reference board's.
- Corroboration **and a conflict** found later in local refs (not used to enable anything):
  `refs/heliodarwin/wiki/Input-and-HID.md:49` states "MT6366 PMIC `TOPSTATUS` register `0x28`,
  active-low bit 1", matching the value used here; `refs/klios716/xnu/NOTES.md:705` records a
  different MT6366 `TOPSTATUS 0x0e` for its own board. Two local sources disagree, so the gate stays
  OFF until the owner confirms the X6886 register on-device. No iscle source was involved.
- Polling thread has a hard tick cap (`x6886_keys_ticks`, max `0x00ffffff`), no unbounded `for(;;)`.
- Every diagnostic goes to printf and to pstore `0x480af000` via `X6886_LOG` (`[X6886-keys]`).
