# X6886 MT6789 audio port manifest

Branch `feat/x6886-real-port`, 100% offline: no adb, no fastboot, no kernel build, no flash.
Only `drivers/audio/hakios.x6886_audio.c` and this manifest were written.
No Android `.so`/`.ko` is loaded, no modem/NVRAM/IMEI write, no iscle (banned, Qualcomm).

## Sources

- Genuine MediaTek MT6789 code in `refs/alps-device-modules-6.12/sound/soc/mediatek/mt6789/`:
  `mt6789-reg.h` (register/bit map), `mt6789-afe-pcm.c` (AFE, DMA/WPDMA, IRQ, power),
  `mt6789-afe-clk.c` (clock enables), `mt6789-mt6366.c` (machine driver/controls).
- Device truth: `devices/x6886/evidence/iomem.txt`, `devices/x6886/evidence/interrupts.txt`, `devices/x6886/evidence/fdt.dts`,
  `devices/x6886/evidence/modules.txt`.
- Stock audio routing: `otadump/vendor/etc/audio_device.xml` (this handset's kctl map).

## Device constants (file:line proof)

| Constant | Value | Proof |
|---|---|---|
| AFE MMIO base / size | `0x11210000`, `0x2000` (`11210000-11211fff`) | `devices/x6886/evidence/iomem.txt:105`; `fdt.dts:5582-5584` (`reg = <0 0x11210000 0 0x2000>`) |
| AFE DT SPI IRQ | `0xea` = **234** | `fdt.dts:5585` (`interrupts = <0x00 0xea 0x04 0x00>`) |
| AFE Linux IRQ (GICv3) | **266** = 234 + 32 SPI offset | `interrupts.txt:147` (`GICv3 266 Level Afe_ISR_Handle`) |
| AFE is its own clock controller (phandle `0x3a`) | `0x11210000`, `0x1000` | `fdt.dts:1776-1780`; used at `fdt.dts:5590-5591` |
| topckgen (phandle `0x2b`) | `0x10000000` | `fdt.dts:1582-1586` |
| infracfg_ao (phandle `0x2c`) | `0x10001000` | `fdt.dts:1589-1593` |
| apmixedsys (phandle `0x3c`) | `0x1000c000` | `fdt.dts:1639-1643` |
| scpsys power domain (phandle `0x2e`, index `0x0b`) | `0x10006000` | `fdt.dts:1629-1636`; `fdt.dts:5589` |
| pwrap / mt6366 PMIC | `0x10026000` | `fdt.dts:3219-3220`, `3228-3229` |
| mt6358codec (`mediatek,mt6366-sound`) | under pwrap, `reg-vaud28-supply` | `fdt.dts:3832-3840` |
| vaud28 regulator | `0x2ab980` uV = **2800000 uV / 2.8 V**, always-on | `fdt.dts:3727-3734` |
| speaker amp RT5512, I2C `0x5c` | on `i2c@1101a000` = `0x1101a000` | `fdt.dts:5186-5192`; bus at `fdt.dts:5149` |
| sound card | `mediatek,mt6789-mt6366-sound`, `spk-i2s = <3 0>` | `fdt.dts:7626-7635` |
| audio module presence | `snd_soc_mt6789_afe`, `snd_soc_mt6366`, `mt6789_mt6366`, `clk_mt6789_audiosys` | `modules.txt:147-153`, `465` |

### IRQ note (task prompt vs dump)

The task text says "interrupts.txt IRQ 234". `interrupts.txt` has no AFE line with 234
(the only 234 substring is an IPI counter at `interrupts.txt:177`). The proven values are
**DT SPI 234** at `fdt.dts:5585` and **Linux GICv3 266** at `interrupts.txt:147`.
Both are coded, with their own file:line proof; no unproven IRQ constant was invented.

## Register constants (genuine MediaTek source, file:line proof)

| Constant | Value | Proof |
|---|---|---|
| `AUDIO_TOP_CON0/1/2/3` | `0x0000/0x0004/0x0008/0x000c` | `mt6789-reg.h:2154-2157` |
| `AFE_DAC_CON0` | `0x0010` | `mt6789-reg.h:2158` |
| `AFE_DL1_CON0`, `BASE_MSB`, `BASE`, `CUR_MSB`, `CUR`, `END_MSB`, `END` | `0x004c,0x0050,0x0054,0x0058,0x005c,0x0060,0x0064` | `mt6789-reg.h:2171-2177` |
| `AFE_VUL12_CON0`, `BASE_MSB`, `BASE`, `CUR_MSB`, `CUR`, `END_MSB`, `END` | `0x0374,0x0378,0x037c,0x0380,0x0384,0x0388,0x038c` | `mt6789-reg.h:2282-2288` |
| `AFE_IRQ_MCU_CNT0` | `0x0300` | `mt6789-reg.h:2267` |
| `AFE_IRQ_MCU_CON0/STATUS/CLR/EN` | `0x03a0/0x03a4/0x03a8/0x03b4` | `mt6789-reg.h:2291-2296` |
| `AFE_APLL1_TUNER_CFG`, `AFE_APLL2_TUNER_CFG` | `0x03f0`, `0x03f4` | `mt6789-reg.h:2309-2310` |
| `AFE_HD_ENGEN_ENABLE` | `0x0dd0` | `mt6789-reg.h:2644` |
| `AFE_AGENT_ON` | `0x1b00` | `mt6789-reg.h:2884` |
| `PDN_22M/24M/APLL2_TUNER/ADC/DAC/DAC_PREDIS/TML/NLE` shifts | `8,9,18,24,25,26,27,28` | `mt6789-reg.h:70,67,55,46,43,40,37,34` |
| `AHB_IDLE_EN_EXT_SFT` | `29` (reference clears/sets this at power-on) | `mt6789-reg.h:31`; `mt6789-afe-pcm.c:2967` |
| `DL1_ON_SFT`, `VUL12_ON_SFT` | `4`, `31` | `mt6789-reg.h:2949`, `:2889` |
| `IRQ0_MCU_ON/CLR/EN_SFT` | `0,0,0` | `mt6789-reg.h:1622`, `:1797`, `:1833` |
| `AFE_IRQ_STATUS_BITS`, `AFE_IRQ_CNT_MASK` | `0x87ffffff`, `0x3ffff` | `mt6789-reg.h:3085-3087` |
| clock names (`aud_afe_clk`, `aud_dac_clk`, …) | table | `mt6789-afe-clk.c:41-63`; DT `fdt.dts:5591` |
| CCF-controlled TOP_CON0/1 | skipped by the AFE driver itself | `mt6789-afe-pcm.c:2588-2589` |
| IRQ handler reads STATUS/EN, writes CLR `0xffffffff` | behaviour studied, not registered in XNU | `mt6789-afe-pcm.c:2835-2926` |
| enable-clock / disable-clock order | studied, not driven by XNU | `mt6789-afe-clk.c:241-330` |

## Audio paths (stock `otadump/vendor/etc/audio_device.xml`)

| Path | Kctl settings | Proof |
|---|---|---|
| speaker | `DAC In Mux=Normal Path`, `LOL Mux=Playback` | `:50-56` |
| earpiece / receiver | `DAC In Mux=Normal Path`, `RCV Mux=Voice Playback` | `:26-32` |
| main mic | `ADC L/R Mux=Left/Right Preamplifier`, `PGA L/R=AIN0` | `:85-96` (also `:133-144`) |
| rear mic | same preamps, `PGA L/R=AIN2` | `:109-120` (also `:169-180`) |
| headset mic | same preamps, `PGA L/R=AIN1` | `:193-204` |

`X6886_LOG` reports these as declarative routes (`[X6886-audio]`), plus the decoded
board wiring (pwrap, vaud28, RT5512 I2C, spk-i2s).

## Boot safety

- `x6886_audio=1` opt-in; default is a no-op return 0.
- pstore mirror at `0x480af000`, `sig +0 = 0x43474244`, `start +4`, `size +8`, `data +12`
  (same `persistent_ram_buffer` convention as the other X6886 drivers).
- Map AFE first; **no MMIO before mapping**.
- Readiness gate: bus-sanity read (`0xffffffff` sentinel) + bounded settle loop
  (`X6886_READY_SPINS = 64`, `X6886_READY_DELAY_US = 100` — scaffold bounds, not device
  constants), then require all PDN bits clear and `AHB_IDLE_EN_EXT` set. Failure dumps
  TOP0-3, IRQ STATUS/EN, DL1 and VUL12 windows, then returns `ENXIO`/`EBUSY`.
- Zero infinite loops. No `panic()`, no assert, no XNU abort. Clean errno only.
- DMA check is read-only: cursor-in-window verification, no buffer start, no IRQ clear.

## Translated vs skipped

- **Translated:** real register offsets/bits, ready-gate and failure-register diagnostics,
  DL1/VUL12 DMA window check, IRQ numbering, DAPM route tables, device wiring.
- **Skipped on purpose:** CCF/PLL enable sequence (`mt6789-afe-clk.c:241-330`), SMC/SPM DRAM
  request (`:332-...`), AFE IRQ registration and `AFE_IRQ_MCU_CLR` writes
  (`mt6789-afe-pcm.c:2835-2926`), DMA start/stop and buffer programming. XNU has no CCF/ALSA
  and the SMC path is Android firmware-only; forcing any of these would be an unproven guess.
- **Skipped on purpose:** MT6366 PMIC register writes. The kctl names/values are proven from
  `audio_device.xml`, but the MT6358 codec register map is not present in
  `refs/alps-device-modules-6.12/sound/soc/` (no `codecs/mt6358*`), so no PMIC offset can be
  cited. Routes are declarative; no speaker/earpiece/mic is actually switched.

## Gaps / owner sign-off

- Owner decision needed before real playback: provide or approve the MT6358 codec register
  map, then implement the pwrap write path with file:line proof per register.
- `AUDIO_TOP_CON0` PDN bits are CCF-controlled on Linux (`mt6789-afe-pcm.c:2588-2589`); the
  ready-gate assumes the stock boot chain has already enabled them. If a cold XNU boot
  finds them down, the driver refuses cleanly instead of guessing a PLL sequence.
- The external RT5512 amp at I2C `0x5c` and the aw87xxx amps at `0x58`/`0x59`
  (`fdt.dts:5172-5184`) are decoded but not powered; smart-amp firmware is Android-side.
- `itr` SCP/ADSP/VOW paths (`snd_soc_mtk_scp_ultra`, `adsp`, `mtk_vow`) exist in
  `modules.txt:154-163,225` but are out of scope for this scaffold.
- No build, no flash, no device test was performed (task constraint).
