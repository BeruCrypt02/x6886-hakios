# X6886 audio plan (Phase 4, NOT-PORTED)

Date: 2026-11-24. Mode: read-only research. No ADB, no fastboot, no flashing, no build or source edits.

Status of every item in this document: **Phase 4 / NOT-PORTED** on the X6886 XNU side. "Verified" below means verified in the Android dump, not in XNU.

## 1. Sources and method

| Source | What it gave |
|---|---|
| `devices/x6886/evidence/fdt.dts` | Live device tree: AFE, sound card, codec, I2C amps, pinctrl, supplies |
| `devices/x6886/evidence/dt-nodes.txt` | Sysfs view confirming the same nodes |
| `devices/x6886/evidence/modules.txt`, `devices/x6886/evidence/iomem.txt` | Live modules and MMIO at dump time |
| `devices/x6886/evidence/X6886-specs.txt` | Hardware summary line for the audio amp |
| `/home/berucrypt00/otadump/kernel_config.txt` | Kernel audio config |
| `/home/berucrypt00/otadump/mtk/installed-files-vendor.txt`, `all_files.txt` | File inventory |
| `/home/berucrypt00/otadump/vendor_dlkm/lib/modules/`, `odm_dlkm/lib/modules/` | Exact `.ko` names and `modules.load` order |
| `/home/berucrypt00/otadump/odm/etc/vconfig/audio/` | Mixer map, mic metadata, tuning, amp blobs |
| `/home/berucrypt00/otadump/vendor/etc/audio_policy_configuration.xml`, `system_ext/etc/audio_policy_configuration.xml` | Android device and stream routing |
| `/home/berucrypt00/x6886-hakios/refs/` | XNU audio and CoreAudio reference work |
| `docs/driver-matrix.md:15`, `docs/X6886-PMIC-THERMAL.md:19-22` | Existing audits, read to avoid duplication |

`docs/X6886-DRIVER-AUDIT.md` was not present when this was written; `docs/X6886-PMIC-THERMAL.md` was. This document does not edit either.

## 2. Audio hardware found in the dump

### 2.1 Codec, AFE and sound card

| Item | Value | Evidence |
|---|---|---|
| Codec IC | MT6366 inside the PMIC, node `mt6358codec`, compatible `mediatek,mt6366-sound` | `fdt.dts:3832-3841`; `modules.alias` in `docs/reference/dump-analysis/sensors-audio-pmic.md:60` |
| Codec position | Child of `pwrap@10026000` / `mediatek,mt6366`, not a separate I2C chip | `fdt.dts:3219-3229` |
| Codec config | `mediatek,dmic-mode = <0>` (analog mic path), `mediatek,mic-type = <1>`, `io-channel-names = "pmic_hpofs_cal"`, `nvmem-names = "pmic-hp-efuse"` | `fdt.dts:3834-3839` |
| Codec supply | `reg-vaud28-supply` to `ldo-vaud28`, fixed 2.8 V (0x2ab980), always on | `fdt.dts:3728-3736`, `3839` |
| AFE | `mt6789-afe-pcm@11210000`, compatible `mediatek,mt6789-sound`, MMIO `0x11210000-0x11211fff` (8 KiB), IRQ 234 (0xea), phandle `0xbd` | `fdt.dts:5582-5611`; `iomem.txt:105` |
| AFE clocks | 53 named clocks: `aud_afe_clk`, `aud_dac_clk`, `aud_dac_predis_clk`, `aud_adc_clk`, `aud_apll22m_clk`, `aud_apll24m_clk`, apll1/2 tuner, `aud_tml_clk`, `aud_nle`, hires DAC/ADC, i2s1-4 bclk, three general ASRC, connsys I2S ASRC, infra, top muxes, apll12 dividers, 26 MHz | `fdt.dts:5590-5591` |
| AFE power and pinctrl | Power domain `0x2e 0x0b`; 18 pinctrl states: clk/dat mosi/miso, i2s0, i2s3, vow dat/clk miso | `fdt.dts:5589`, `5592-5610` |
| Machine driver node | `/sound`, compatible `mediatek,mt6789-mt6366-sound`, platform `0xbd` (AFE), scp-audio `0xbe`, `mediatek,spk-i2s = <0x03 0x00>` (I2S3), speaker codec `0x204` | `fdt.dts:7626-7636` |
| SCP audio | `snd_scp_audio` (`0xbe`) and `scp_audio_mbox@107ff000` both `status = "disabled"` | `fdt.dts:7609-7624` |
| Accdet | `mediatek,accdet-pmic = <0x66>` under MT6366; `mt6358_accdet.ko` and `splitswitch_gpio.ko` loaded | `fdt.dts:3259-3271`; `modules.txt:164-165` |

### 2.2 Output paths, amplifiers and I2C

| Device | Bus / address | Binding | Role | Evidence |
|---|---|---|---|---|
| `aw87xxx_pa@58` | i2c6 (`0x1101a000`) `0x58`, `dev_index = 1` | `awinic,aw87xxx_pa` | Awinic smart PA, one of two | `fdt.dts:5172-5178` |
| `aw87xxx_pa@59` | i2c6 `0x59`, `dev_index = 0` | `awinic,aw87xxx_pa` | Awinic smart PA, one of two | `fdt.dts:5179-5185` |
| `speaker_amp@5c` | i2c6 `0x5c` | `richtek,rt5512`, `#sound-dai-cells = <0>`, `sound-name-prefix = "Left"` | Speaker DAI / codec referenced by `/sound` as phandle `0x204` | `fdt.dts:5186-5191`, `7633-7635` |
| `hl1280-audioswitch@42` | i2c3 (`0x11015000`) `0x42` | `mediatek,hl1280-audioswitch`, `status = "okay"` | Audio switch | `fdt.dts:4708`, `4758-4763` |
| PMIC | `pwrap@10026000`, 4 KiB, IRQ SPI 252 (hwirq 284) | `mediatek,mt6789-pwrap` | Host for MT6366 codec registers and `vaud28` | `fdt.dts:3219-3229`; `docs/X6886-PMIC-THERMAL.md:19-21` |

The spec text lists `aw87xxx_pa (i2c 6-0059)` (`X6886-specs.txt:23`). The FDT shows two Awinic amps, `0x58` and `0x59`.

Mixer output paths in `odm/etc/vconfig/audio/audio_device.xml`, card `mt6789mt6366`:

| Path | kcontrols on | Use |
|---|---|---|
| `headphone_output` | `HPL Mux` / `HPR Mux` = `Audio Playback`, `DAC In Mux` = `Normal Path` | Wired headphone |
| `receiver_output` | `RCV Mux` = `Voice Playback` | Earpiece |
| `speaker_output` | `LOL Mux` = `Playback`, `Ext_Speaker_Amp Switch` in the test variants | Loudspeaker |
| `dual_speaker_output` | `LOL Mux` = `Playback` and `RCV Mux` = `Voice Playback` | Two-output mode |
| `headphoneDualSpeaker_output` | `HPL Mux` / `HPR Mux` = `Loud DualSPK Playback` | Headphone plus dual speaker |
| `audiotest_topspk_output`, `audiotest_botspk_output` | `RCV Mux`, `LOL Mux`, `Lineout Volume`, `Ext_Speaker_Amp Switch` | Factory test paths |
| `PLAYBACKn_TO_ADDA_DL`, `_TO_I2S1`, `_TO_I2S3`, `_TO_I2S5` | AFE routing for playback streams | Stream-to-DAI choice |

`TranAudioConfiguration.conf` sets `TRAN_AUDIO_AW87XXX_SUPPORT = yes`. `aurisys_config.xml` maps `AURISYS_SCENARIO_PLAYBACK_SMARTPA` to library `smartpa_aw88xxx`; the name is kept verbatim while the driver is aw87xxx.

### 2.3 Microphones

`odm/etc/vconfig/audio/audio_param/MicInfo_AudioParam.xml` is the direct source. It declares four parameter slots: `amic_proj,main_mic`, `amic_proj,sub_mic`, `dmic_proj,main_mic`, `dmic_proj,sub_mic`. `UplinkConfiguration_AudioParam.xml` sets `num of mic = 2`.

| Mic | Type and part | Android device / address | Group and orientation | ADC binding in `audio_device.xml` |
|---|---|---|---|---|
| Primary (main) | Analog, `SPH1642HT5H_REV_B` | `AUDIO_DEVICE_IN_BUILTIN_MIC`, `bottom` | group 0, `(0,-1,0)`, sensitivity -41 dB | `builtin_Mic_Mic1`: `PGA L Mux` / `PGA R Mux` = `AIN0`, ADC L/R = Left/Right Preamplifier |
| Secondary (back) | Analog, `SPH1642HT5H_REV_B` | `AUDIO_DEVICE_IN_BACK_MIC`, `back` | group 1, `(0,0,-1)`, sensitivity -41 dB | `builtin_Mic_Mic2`: PGA = `AIN2` |
| DMIC variants | `SPK0641HT4H_1_Rev_A` | same two device types | same locations, sensitivity -26 dB | `DMic1_ADDA_TO_CAPTURE1_4CH`, `DMic2_ADDA_TO_CAPTURE1_4CH`; `MicTypeDMICMode` sets `MTKAIF_DMIC = On` |
| Headset mic | External | not in `MicInfo` | - | `headset_mic_input`: PGA = `AIN1`, `MTKAIF_DMIC = Off` |
| Dual-mic capture | both built-in mics | - | - | `builtin_Mic_DualMic`: L = `AIN0`, R = `AIN2` |

FDT sets `mediatek,dmic-mode = <0>`, so the active path is analog. Main mic is the one at `AIN0`, reported as `Built-In Mic` at `bottom`.

Android policy attaches `Built-In Mic` and `Built-In Back Mic` to the primary module in both `vendor/etc/audio_policy_configuration.xml` and `system_ext/etc/audio_policy_configuration.xml`; the vendor file also attaches `Earpiece`, `Speaker`, `Voice Call In`, `Echo Ref In` and FM ports.

### 2.4 GPIO and pinctrl

MediaTek pinctrl encodes the pin in the high byte of `pinmux`, mode in the low byte. Values from `fdt.dts:2521-2725`:

| State | Pin (decimal) | Mode | Name |
|---|---|---|---|
| `aud_clk_mosi` | 169 (0xa9) | 1 | Audio clock master out |
| `aud_clk_miso` | 173 (0xad) | 1 | Audio clock master in |
| `aud_dat_mosi` | 171 (0xab), 172 (0xac) | 1 | Audio data master out |
| `aud_dat_miso0` | 175 (0xaf) | 1 | Audio data master in 0 |
| `aud_dat_miso1` | 176 (0xb0) | 1 | Audio data master in 1 |
| `aud_gpio_i2s0` | 53 (0x35) | 2 | I2S0 GPIO |
| `aud_gpio_i2s3` | 50 (0x32), 51 (0x33), 52 (0x34) | 2 | I2S3 GPIO, the speaker path in `/sound` |
| `vow_dat_miso` | 175 (0xaf) | 4 | VoW data |
| `vow_clk_miso` | 176 (0xb0) | 1 | VoW clock |

The `0x49` GPIO phandle is the main MediaTek PIO controller (`gpio-ranges = <0x49 0 0 0xbc>`).

### 2.5 Kernel modules and firmware

Live modules from `modules.txt`, loaded at boot from `vendor_dlkm/lib/modules/modules.load` unless marked ODM:

| Module | Size (bytes) | Used by |
|---|---|---|
| `mt6789_mt6366` | not in module list head | machine driver, pulls AFE and codec |
| `snd_soc_mt6366` | 151552 | `mt6789_mt6366` |
| `snd_soc_mt6789_afe` | 311296 | `mt6789_mt6366` |
| `snd_soc_mtk_common` | 81920 | AFE and DSP common |
| `snd_soc_aw87xxx` | 110592 | `tran_audio_control` |
| `snd_soc_fs1599` | 90112 | `tran_audio_control` |
| `snd_soc_fs1894` | 122880 | `tran_audio_control`, `mtk_sp_spk_amp` |
| `snd_soc_oca72xxx` | 94208 | `tran_audio_control` |
| `mtk_sp_spk_amp` | 24576 | machine driver and the amp drivers |
| `snd_soc_audiodsp_common`, `audio_ipi`, `mtk_scp_audiocommon`, `adsp` | 135168, 339968, 40960, 118784 | DSP and SCP link |
| `mtk_btcvsd` | 32768 | BT voice |
| `mt6358_accdet`, `splitswitch_gpio` | 49152, 16384 | accessory / headset detection |
| `mtk_vow`, `mtk_scp_vow`, `snd_soc_mtk_scp_ultra`, `mtk_scp_ultra` | 69632, 12288, 49152, 20480 | VoW and SCP ultra |
| `clk_mt6789_audiosys` | 12288 | audio clock controller |
| `tran_audio_control` (vendor), `tc_audio` + `tran_audio_typec` (ODM) | 28672, 12288, 57344 | Transsion glue |

`snd_soc_rt5512` does not appear in `modules.txt`; the RT5512 DAI is bound through the MediaTek machine driver.

Firmware, exact sizes:

| File | Size |
|---|---|
| `odm/firmware/aw87xxx_acf.bin` | 5988 |
| `odm/firmware/awinic_params.bin` | 41288 |
| `odm/etc/vconfig/audio/pa_param/AW_DSP.bin` | 43628 |
| `odm/etc/vconfig/audio/pa_param/AW_SINWAVE_DSP.bin` | 14596 |
| `awinic_monitor.bin` | string in `snd-soc-aw87xxx.ko`, file absent from dump |

Kernel config: `CONFIG_SOUND`, `CONFIG_SND`, `CONFIG_SND_PCM`, `CONFIG_SND_COMPRESS_OFFLOAD`, `CONFIG_SND_COMPRESS_ACCEL`, `CONFIG_SND_SOC`, `CONFIG_SND_SOC_COMPRESS`, `CONFIG_SND_SOC_TOPOLOGY`, `CONFIG_SND_USB_AUDIO` are set. GKI hides some SND/SOC symbols.

Android userspace at dump time: `audioserver` and `vendor.audio-hal-aidl` running; service `/vendor/bin/hw/android.hardware.audio.service-aidl.mediatek`; manifest declares `android.hardware.audio.core` IModule `default`, `r_submix`, `bluetooth` and IConfig `default`; `vendor/lib64/hw/audio.primary.mt6789.so` is a symlink to `audio.primary.mediatek.so`.

## 3. Reference-tree cross-check

All XNU work below is **reference-only** for X6886 unless stated otherwise. None of it is compiled into or running on X6886.

| Repo | Audio work found | Hardware target | Status for X6886 |
|---|---|---|---|
| `refs/hakios-common/` | `components/audio/audio_plugin.c` + `.plist` (`HakiosAudio` AudioServerPlugIn), `drivers/board/{audio,speaker,mic,sound,virtio_sound}.c`, `workbench/audio_*_test.c`, `tools/sound.py`, rc.jobs for sound/speakers/mic | Snapdragon "renoir" phone (Qualcomm ADSP APR services AFE/ASM/ADM, AW88261 amplifier, SPMI) and QEMU virtio-sound | REFERENCE-ONLY for the CoreAudio plug-in pattern and the `/dev/hakios-audio` contract. Speaker/mic drivers are Qualcomm-specific. `userland/README.md:51` reports sound working on the reference phone; `Documentation/qemu.md:41` reports virtio sound under QEMU |
| `refs/xnu-12377-hakios/` | `bsd/dev/board/{audio,speaker,mic,sound,virtio_sound}.c`, built objects under `BUILD/obj/DEVELOPMENT_ARM64_BCM2837/` | QEMU BCM2837 and the Qualcomm reference board | REFERENCE-ONLY. Object paths prove a QEMU/reference build, not MT6789 |
| `refs/hakios-mtk/` | `xnu/files/bsd/dev/redwood_{audio,speaker,mic,sound}.c`, `userland/src/audio_plugin.c`, `prebuilt/userland/RedwoodAudio` | Qualcomm "redwood" phone | REFERENCE-ONLY. No MTK AFE or MT6366 binding |
| `refs/oosexgang-xnu27/` | `kernel/overlay/bsd/dev/hakios_audio.c`, `hakios_virtio_sound.c`, `userland/src/audio_plugin.c`, `docs/boot-args.md:119-125`, `docs/drivers.md:37,41` | Same Hakios/QEMU design | REFERENCE-ONLY. `ringer sounding at 29 s` in `docs/air.md:592` is a Qualcomm reference log |
| `refs/klios716/` | MT6789 PWRAP and MT6366 AUXADC probes verified on hardware; `xnu/NOTES.md:631-638` lists Sound as item 5 after buttons, USB, battery and vibration. `NOTES.md:112,152-153` documents moving `VirtualAudio.plugin` out of the HAL to stop a mediaserverd SIGSEGV loop | Another MT6789 device, not X6886 | REFERENCE-ONLY for PWRAP/PMIC access and the VirtualAudio workaround. ABSENT for audio hardware drivers |
| `refs/heliodarwin/` | MT6789 GPU power and input docs only | MT6789 / X676C | ABSENT for audio |
| `refs/ios-jellystar/` | No audio source in `xnu/` or `shim/` | iOS 27 reference | ABSENT |
| `refs/unixnu-mt6789/` | `configs/mt6789.config`, `dts/mt6789.dtsi` cover CPU, GIC and timer. README says "Work in progress ... not a complete SoC driver implementation" | Xiaomi Redmi Pad `yunluo` | ABSENT for audio |
| `refs/theos-sdks/` | `iPhoneOS16.5.sdk` declares CoreAudio and AudioToolbox `.tbd` stubs | SDK only | REFERENCE-ONLY; link-time headers, no driver |
| `refs/akita/` | `tools/usbmux-sound.py` host utility | Host tool | ABSENT for device audio |

No MT6366 or RT5682 audio driver exists in the inspected reference trees. The filename scan covered every repo under `refs/`; source inspection covered the audio files listed above.

## 4. XNU and CoreAudio readiness on X6886

| Capability | On X6886 today | What the references offer | Phase 4 status |
|---|---|---|---|
| Playback through speaker | No AFE driver, no I2S sink, no `/dev/hakios-audio` node | `/dev/hakios-audio` contract (`hakios-common/include/board/audio.h`): 48 kHz, 2 channels, signed 16-bit LE; QEMU virtio-sound sink works | NOT-PORTED |
| Earpiece / speaker switching | No XNU kcontrol equivalent. Android uses `RCV Mux` versus `LOL Mux` in `audio_device.xml` | No reference implements MediaTek mixer-string switching | NOT-PORTED |
| Microphone capture | No capture path. Two analog mics are mapped in Android metadata only | `mic.c` references use Qualcomm LPASS / SoundWire and one main mic | NOT-PORTED |
| CoreAudio device | No `HakiosAudio` plug-in injected on X6886 | `components/audio/audio_plugin.plist` registers an AudioServerPlugIn factory; userland README describes injection into mediaserverd | NOT-PORTED |
| audioserver stability | Not tested on X6886 | `klios716/xnu/NOTES.md:153` records the VirtualAudio.plugin workaround for a missing audio device | NOT-PORTED |
| BT / USB / FM audio | Android HAL declares these ports | No X6886 XNU work | NOT-PORTED |

`docs/driver-matrix.md:15` marks audio UNVERIFIED and says no X6886 audio DT node was cited. The live FDT does contain the nodes listed in section 2. Hardware presence is verified; the XNU port is not.

## 5. Ordered steps for Phase 4

These are future work. None has been performed. Each step stays read-only until its gate passes.

1. **Freeze the hardware map as board data.** Copy the MMIO, IRQ, I2C addresses, pinctrl pins and supply name from section 2 into the X6886 board description. Gate: a read-only probe returns AFE identity at `0x11210000` and MT6366 SWCID `0x6610` through the existing PWRAP helper.
2. **Establish the MT6366 register path.** Reuse the serialized PWRAP transaction helper from Phase 3. Gate: a known PMIC register reads back with the expected FSM state; `vaud28` is confirmed at 2.8 V.
3. **Enable the minimum audio clock set.** `clk_mt6789_audiosys`, topckgen, apmixedsys, infracfg and the I2S3 pinctrl states. Gate: `aud_afe_clk` and one APLL read as enabled, no clock written before its source is stable.
4. **Probe the speaker amplifiers.** Read chip IDs at i2c6 `0x58` and `0x59`, load `aw87xxx_acf.bin` and `awinic_params.bin`, keep both amps muted and powered down. Gate: both IDs match a known Awinic part; `awinic_monitor.bin` is handled as absent.
5. **Bring up the speaker DAI.** Bind RT5512 at i2c6 `0x5c` to I2S3 through the machine-driver description. Gate: DAI probe succeeds with the amps still muted.
6. **Create the XNU audio device.** Port the `/dev/hakios-audio` write path with a sink and the `HAKIOS_AUDIO_SET_VOLUME` ioctl. Use the reference 48 kHz / 2ch / 16-bit format. Gate: a user-space test writes frames and `rec_sound` produces a WAV with the expected tone.
7. **Wire the CoreAudio plug-in.** Adapt `HakiosAudio` from `components/audio/`, register one stereo output, arrange mediaserverd injection. Gate: CoreAudio lists one output device and a tone plays without a mediaserverd restart loop.
8. **Add output switching.** Map `receiver_output` to `RCV Mux` and `speaker_output` to `LOL Mux` in an XNU-side switch, keeping the amps muted during the transition. Gate: earpiece and speaker can be selected independently and the inactive path is idle.
9. **Add capture.** Configure MT6366 ADC L/R with PGA `AIN0` for the primary mic and `AIN2` for the back mic; keep `MTKAIF_DMIC` off. Gate: a recorded buffer correlates with a known injected sound at the primary mic.
10. **Add accessory and secondary paths.** Headset mic on `AIN1` through accdet, then VoW, BT voice, USB audio and FM. Gate: each path is exercised after steps 6 to 9 hold.

## 6. Open gaps

- No X6886 XNU audio driver, sink, plug-in or board data exists in this tree.
- No XNU proof of MT6789 I2S3 or AFE register access on this device. The PMIC PWRAP path is proven on a different MT6789 board in `klios716`.
- The dump cannot show whether an RT5512 driver is built into the stock kernel or handled by the MediaTek machine driver without source.
- `awinic_monitor.bin` is referenced by the stock amp driver but absent from the dump.
- The stock `vendor/etc/aurisys_config.xml` and `audio_policy_configuration.xml` are Android-side artifacts. They give routing intent, not an XNU contract.
- The exact audio clock dividers and APLL settings are not recorded in the dump; they live in stock kernel code that is not present here.
