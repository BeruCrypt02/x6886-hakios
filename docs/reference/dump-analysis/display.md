# X6886 Display Panel — Stock Dump Analysis

**Target device:** Infinix X6886 (HOT 60 Pro+), MT6789, Android 16, kernel `6.12.38-android16-5-gcc51d883045d-4k`
**Analysis date:** 2026-11-23
**Mode:** READ-ONLY on `/home/berucrypt00/otadump`. Nothing there was modified. No phone access.
**Purpose:** Supply exact panel/DSI/DSC data for an XNU (HakiOS) DSI driver port.

Sources used:

- `/home/berucrypt00/otadump/vendor_boot/ramdisk/lib/modules/*.ko`
- `/home/berucrypt00/otadump/odm/etc/vconfig/display/*.xml`
- `/home/berucrypt00/otadump/kernel_config.txt`
- `/home/berucrypt00/hakios-x6886/devices/x6886/evidence/fdt.dts` (live FDT decompile)
- `/home/berucrypt00/hakios-x6886/devices/x6886/prebuilt/device-tree.dts` (stock DT decompile)
- `/home/berucrypt00/hakios-x6886/devices/x6886/evidence/modules.txt`, `getprop.txt`, `display.txt`, `dmesg.txt`

Tools: `file`, `modinfo`, `strings`, `readelf -S/-r/-x`, `nm -n -S`, `xxd`, plus inline byte-array decoding (analysis only, no code files produced).

---

## 0. Important corrections to the task brief (VERIFIED)

| Task brief said | Actual finding |
|---|---|
| Active module `nt37706a_fhdp_dsi_vdo_dsc_boe_144hz_cn5c.ko` | That file exists, but the **active** one is `nt37706a_fhdp_dsi_vdo_dsc_boe_boe_144hz_x6886.ko` — its OF alias is byte-identical to the live FDT `touch,lcm_name` string `nt37706a_fhdp_dsi_vdo_dsc_boe_boe_144hz_x6886`. |
| `mtk_drm_panel_i2c.ko` | No such file. The real module is `tran_drm_panel_i2c.ko` (Transsion-authored I2C panel-EID/TDDI helper). |
| — | A second X6886-specific panel module exists: `vtdr6126a_fhdp_dsi_vdo_dsc_tm_tm_144hz_x6886.ko` (TM supplier, DT `panel2@0`). |

---

## 1. Exact modules driving the panel (VERIFIED)

### 1.1 Active panel driver

```
Path:   /home/berucrypt00/otadump/vendor_boot/ramdisk/lib/modules/nt37706a_fhdp_dsi_vdo_dsc_boe_boe_144hz_x6886.ko
Size:   359,416 bytes (0x57BC8)
Format: ELF 64-bit LSB relocatable, ARM aarch64, version 1 (SYSV), not stripped
BuildID (SHA1): e8b7af8e3989759492306a113329ebd83aa55e48
```

`modinfo` / embedded `.modinfo` strings:

```
built_with=DDK
author=baofu.zhang@transsion.com
description=nt37706a VDO 144hz AMOLED Panel Driver
license=GPL v2
name=nt37706a_fhdp_dsi_vdo_dsc_boe_boe_144hz_x6886
depends=mtk_panel_ext,mtk-cmdq-drv-ext
alias=of:N*T*Cnt37706a,fhdp,dsi,vdo,dsc,boe,boe,144hz,x6886
alias=of:N*T*Cnt37706a,fhdp,dsi,vdo,dsc,boe,boe,144hz,x6886C*
vermagic=6.12.38-android16-5-gcc51d883045d-4k SMP preempt mod_unload modversions aarch64
```

Compiler string from `.comment`:
`Android (12833971, +pgo, +bolt, +lto, +mlgo, based on r536225) clang version 19.0.1`

**Binding evidence (VERIFIED):** live FDT `fdt.dts:69` has `touch,lcm_name = "nt37706a_fhdp_dsi_vdo_dsc_boe_boe_144hz_x6886";` and `fdt.dts:1608` has `supplier-0 = "nt37706a_fhdp_dsi_vdo_dsc_boe_boe_144hz_x6886, focaltech, ft3682g";`. DT `panel1@0` compatible is the same string (see §7). `/proc/modules` dump shows this module loaded (`331776` bytes resident). All other panel modules are loaded too (MediaTek registers every built-in panel driver) but only the matching `of` alias binds.

### 1.2 Alternate-supplier panel driver (VERIFIED, alternate hardware)

```
Path:   /home/berucrypt00/otadump/vendor_boot/ramdisk/lib/modules/vtdr6126a_fhdp_dsi_vdo_dsc_tm_tm_144hz_x6886.ko
Size:   353,440 bytes (0x564C0)
BuildID: 7ccbb47ac37e816d9d270d4bf851ad390a6cf60e
author=bing.he<bing.he@transsion.com>
description=VTDR6126A VDO 144hz AMOLED Panel Driver
depends=mtk_panel_ext,mtk-cmdq-drv-ext
alias=of:N*T*Cvtdr6126a,fhdp,dsi,vdo,dsc,tm,tm,144hz,x6886
```

Live FDT `supplier-1 = "vtdr6126a_fhdp_dsi_vdo_dsc_tm_tm_144hz_x6886, chipsemi, chct5560";`, DT `panel2@0`. This is a different panel IC (TM), not the active one on the analysed device. Its DSC parameter block is byte-identical to the BOE panel's (see §5).

### 1.3 Companion / infrastructure modules (VERIFIED)

| Module | Size | BuildID / notes |
|---|---|---|
| `mtk_panel_ext.ko` | 19,200 | BuildID `6862803d1fb3ea8891dd344868d6a6994b920b55`; author Tai-Hua Tseng (MediaTek); "MTK DRM panel infrastructure"; exports `mtk_panel_ext_create`, `find_panel_ctx`, `find_panel_ext`, `mtk_panel_tch_rst`, `mtk_drm_get_lcm_version`. |
| `tran_drm_panel_i2c.ko` | 24,496 | BuildID `89affa5b3d13b9b76f20eba9f63b1cc729f23b74`; author you.tan@transsion.com; "mediatek, panel i2c driver" (panel ID / TDDI over I2C). |
| `mediatek_drm_v1.ko` | 7,208,880 | BuildID `26d8bb37b1912ec22a31213a4325ae84c5ccf4a0`; main MTK DRM/KMS driver; contains DSI + MIPI TX + DSC sequencing. |
| `clk-mt6789-disp_dsc.ko` | 14,456 | BuildID `5830ca81c378937bfc9f7a533df0fc2ddd82d6a8`; `name=clk_mt6789_disp_dsc`, `depends=clk-common`. |
| `pwm-mtk-disp.ko` | 31,616 | disp PWM (backlight IC control). |
| `leds-mtk-disp.ko` | 20,072 | backlight LED class driver. |
| `mtk_disp_notify.ko` | 18,040 | display notify. |
| `mtk_disp_sec.ko` (in `vendor_dlkm/lib/modules`) | 43,112 | secure display; **only** display-related module in `vendor_dlkm`. |

All are uncompressed aarch64 relocatable ELFs.

### 1.4 All panel/LCM driver modules in `vendor_boot/ramdisk/lib/modules` (VERIFIED)

Active X6886 pair marked `*`; other filenames are for different Transsion projects and are not active here.

```
icnl9922c_fhdp_dsi_vdo_txd_boe_120hz_lj6.ko                      246064
nt37703a_fhdp_dsi_vdo_dsc_tm_chct_120hz_cm5.ko                    200168
nt37703a_fhdp_dsi_vdo_dsc_tm_tm_120hz_cl6.ko                      279496
nt37703a_fhdp_dsi_vdo_dsc_tm_tm_120hz_cla6.ko                     192160
nt37703a_fhdp_dsi_vdo_dsc_tm_tm_120hz_cm5.ko                      208296
nt37703a_fhdp_dsi_vdo_dsc_tm_tm_120hz_cm6.ko                      200104
nt37703a_fhdp_dsi_vdo_dsc_tm_tm_144hz_x6855.ko                    282536
nt37703a_fhdp_dsi_vdo_dsc_tm_tm_144hz_x6858.ko                    282536
nt37703a_fhdp_dsi_vdo_tm_tm_120hz_x6850.ko                        192600
nt37706a_fhdp_dsi_vdo_dsc_boe_144hz_cn5c.ko                       431184
nt37706a_fhdp_dsi_vdo_dsc_boe_144hz_cn5.ko                        492624
* nt37706a_fhdp_dsi_vdo_dsc_boe_boe_144hz_x6886.ko                 359416
nt37706a_fhdp_dsi_vdo_dsc_vxn_vxn_144hz_x6885.ko                  342608
rm69220_fhdp_dsi_vdo_dsc_csot_csot_120hz_x6850.ko                  203264
rm69220_fhdp_dsi_vdo_dsc_csot_csot_120hz_x6853.ko                  204312
rm69220_fhdp_dsi_vdo_dsc_csot_csot_144hz_x6858.ko                  300112
* vtdr6126a_fhdp_dsi_vdo_dsc_tm_tm_144hz_x6886.ko                 353440
vtdr6126a_fhdp_dsi_vdo_dsc_tm_144hz_cn5c.ko                       441728
vtdr6126a_fhdp_dsi_vdo_dsc_tm_144hz_cn5.ko                        365944
vtdr6126a_fhdp_dsi_vdo_dsc_tm_tm_144hz_x6885.ko                   350272
vtdr6126_fhdp_dsi_vdo_csot_panda_120hz_cl6.ko                     261792
vtdr6126_fhdp_dsi_vdo_dsc_gvo_gvo_120hz_cm5.ko                    192552
vtdr6126_fhdp_dsi_vdo_dsc_wxn_gvo_120hz_x6853.ko                  189224
```

No other `nt37706a`/`vtdr6126a`/`lcm`/`panel` `.ko` files exist under `vendor_dlkm/lib/modules` — only `mtk_disp_sec.ko`.

### 1.5 Compression (VERIFIED)

`file(1)` classifies every module above as a plain **ELF 64-bit LSB relocatable, ARM aarch64, not stripped** — none are gzip/xz/zstd compressed, and none are `.ko.xz`/`.ko.gz`. No decompression is needed; `modinfo`, `readelf`, `nm` and `xxd` read them directly.

For reference only: if a compressed `.ko` were encountered, the standard handling is `xz -dk file.ko.xz` / `zstd -d file.ko.zst` / `gzip -dk file.ko.gz` (or `modprobe`/`modinfo` transparently via `.ko.xz` support), then re-run `file`. Not applicable to this dump.

---

## 2. Raw init-sequence blob — structure and exact location (VERIFIED)

The panel driver stores its DCS init table as a static C array. Its symbol is `lcm_initialization_setting`.

### 2.1 Section/symbol geometry (active BOE module)

```
ELF section .data : file offset 0x000040, size 0x04a980
ELF section .rodata: file offset 0x4dc30, size 0x0007b5
```

`nm -n -S` (values are section-relative offsets in this relocatable ELF):

```
0000000000000000 000000000000003c D <...>_range_min_qp
000000000000003c 000000000000003c D <...>_range_max_qp
0000000000000078 000000000000003c D <...>_range_bpg_ofs
00000000000001d0 0000000000000200 r lcm_suspend_setting          (.rodata)
00000000000003d0 0000000000000078 r switch_mode_144hz            (.rodata)
00000000000003dc 0000000000000003 d bl_tb0                       (.data)
00000000000003e0 0000000000007f00 d lcm_initialization_setting   (.data)  <-- THE INIT BLOB
0000000000000448 0000000000000078 r switch_mode_120hz
00000000000004c0 0000000000000078 r switch_mode_90hz
0000000000000538 0000000000000078 r switch_mode_60hz
00000000000005b0 0000000000000200 r lcm_pcd_on_setting
00000000000082e0 0000000000000038 d <...>_buf_thresh
0000000000008318 0000000000010958 d ext_params_120hz
0000000000018c78 0000000000010958 d ext_params_144hz
00000000000295d0 0000000000010958 d ext_params_90hz
0000000000039f28 0000000000010958 d ext_params_60hz
000000000004a880 0000000000000080 d hbm_mode_enter_setting
000000000004a900 0000000000000080 d hbm_mode_exit_setting
```

File offsets (`0x40 + .data symbol offset`; `0x4dc30 + .rodata symbol offset`):

| Array | File offset | Length | Entries |
|---|---|---|---|
| `lcm_initialization_setting` | **0x0420** | **0x7F00 (32,512 B)** | 254 × 0x80 |
| `lcm_suspend_setting` | 0x4DE00 | 0x200 | 4 × 0x80 |
| `switch_mode_144hz` | 0x4E000 | 0x78 | raw 120-byte payload |
| `switch_mode_120hz` | 0x4E078 | 0x78 | raw payload |
| `switch_mode_90hz` | 0x4E0F0 | 0x78 | raw payload |
| `switch_mode_60hz` | 0x4E168 | 0x78 | raw payload |
| `lcm_pcd_on_setting` | 0x4E1E0 | 0x200 | 4 × 0x80 |
| `bl_tb0` | 0x41C | 0x3 | `51 0d bb` |
| `hbm_mode_enter_setting` | 0x4A8C0 | 0x80 | 1 × 0x80 |
| `hbm_mode_exit_setting` | 0x4A940 | 0x80 | 1 × 0x80 |
| `range_min_qp` | 0x40 | 0x3C | 15 × u32 |
| `range_max_qp` | 0x7C | 0x3C | 15 × u32 |
| `range_bpg_ofs` | 0xB8 | 0x3C | 15 × i32 |
| `buf_thresh` | 0x8320 | 0x38 | 14 × u32 |

### 2.2 Entry layout (VERIFIED by raw bytes)

Each entry is **0x80 (128) bytes** and decodes as:

```
offset 0x00 : u32  cmd   (DCS command, little-endian)
offset 0x04 : u8   count (number of parameter bytes)
offset 0x05 : u8[123] para (zero-padded to the 128-byte stride)
```

Raw example at file offset `0x4A0`:
`f0 00 00 00 05 55 aa 52 08 07 00 00 ...` → `cmd=0xF0, count=5, params=55 AA 52 08 07`.

`xxd -g1 -s 0x420 ...` first bytes of the blob:
```
00000420: 2f 00 00 00 01 00 00 00 ...   -> cmd 0x2F, cnt 1, para 00
000004a0: f0 00 00 00 05 55 aa 52 08 07 ...
00000520: d2 00 00 00 01 01 00 00 ...
00000620: d3 00 00 00 30 68 04 67 04 00 ...   -> cmd 0xD3, cnt 0x30 (48)
```

**This is the blob to port.** Copy 0x7F00 bytes from file offset 0x420 and walk it with the struct above; no symbol table is needed at runtime.

### 2.3 DCS command histogram of `lcm_initialization_setting` (VERIFIED)

252 non-empty entries, plus 2 delay markers:

```
0x0011 : 1     0x0017 : 1     0x0029 : 1     0x002a : 1     0x002b : 1
0x002f : 1     0x0035 : 1     0x003b : 3     0x0051 : 1     0x0053 : 1
0x005f : 1     0x006f : 31    0x0088 : 1     0x008f : 4     0x0090 : 1
0x0091 : 1     0x00b0 : 3     0x00b2 : 2     0x00ba : 5     0x00bf : 4
0x00c0 : 2     0x00c1 : 1     0x00c3 : 2     0x00c6 : 1     0x00c7 : 3
0x00cb : 1     0x00d2 : 79    0x00d3 : 77    0x00d8 : 1     0x00df : 1
0x00ea : 5     0x00f0 : 14
delay marker 0xFFFC at entry 251 (count 120) and entry 253 (count 20)
```

`0xD2` (79×) + `0xD3` (77×) form a repeated "select register page, then write 48-byte payload" pattern — this is the bulk of the panel-IC register programming (gamma / source-driver / timing).

### 2.4 Notable exact entries (VERIFIED)

```
idx   005  cmd=D2 cnt=1   data=01
idx   006  cmd=D2 cnt=2   data=0100
idx   007  cmd=D3 cnt=48  data=6804670400000000000000005b1c5a200400ffdef5dadca6b5fa599948d080c788705683642641600601642643685708
idx   008  cmd=D2 cnt=2   data=0101
idx   009  cmd=D3 cnt=48  data=78c8108d4999a5fb66caddb5efdfc4000000552654260300ffdeb1ce5ada9507c66405404803c03402802001c0140100
...
idx   163  cmd=B0 cnt=20  data=000100ba012c019b02d7043f044008510b6a0dbb
idx   164  cmd=B0 cnt=20  data=3ff03ff03ff03ff03ff03ff03ff03ff03ff03ff0
idx   180  cmd=2A cnt=4   data=00000437      <-- column address 0..0x437 = 0..1079 (1080 columns)
idx   181  cmd=2B cnt=4   data=0000095F      <-- row address    0..0x95F = 0..2399 (2400 rows)
idx   183  cmd=53 cnt=1   data=20             <-- Write CTRL Display
idx   185  cmd=35 cnt=1   data=00             <-- TE ON
idx   186  cmd=5F cnt=2   data=0000
idx   187  cmd=51 cnt=2   data=0000           <-- DCS brightness = 0 at init
idx   188  cmd=90 cnt=2   data=0343
idx   189  cmd=91 cnt=18  data=89a8000cc200020e011f000708bb087a10f0
...
idx   250  cmd=11 cnt=0                       <-- Sleep Out
idx   251  cmd=FFFC cnt=120                   <-- delay marker (120 ms)
idx   252  cmd=29 cnt=0                       <-- Display On
idx   253  cmd=FFFC cnt=20                    <-- delay marker (20 ms)
```

The `0xF0` unlock command always carries `55 AA 52 08 xx`, where `xx` selects a register bank:
observed bank bytes `00, 01, 03, 05, 08` (e.g. `f0 55aa520807`, `f0 55aa520808`, `f0 55aa520800`, `...01`, `...03`, `...05`).

### 2.5 Other sequence blobs (VERIFIED)

`lcm_suspend_setting` (4 entries):
```
cmd=28 cnt=0                      (Display Off)
cmd=FFFC cnt=20                   (delay 20 ms)
cmd=10 cnt=0                      (Sleep In)
cmd=FFFC cnt=100                  (delay 100 ms)
```

`lcm_pcd_on_setting` (4 entries):
```
cmd=F0 cnt=5  data=55aa520801
cmd=6F cnt=1  data=01
cmd=D0 cnt=5  data=8000300001
cmd=11 cnt=0                      (Sleep Out)
```

`hbm_mode_enter_setting` (1 entry):
```
cmd=A9 cnt=18 data=0209b000001101008b010101010087000025
```

`hbm_mode_exit_setting` (1 entry):
```
cmd=A9 cnt=18 data=0209b000000101008b010100010087000000
```

Second-supplier (TM) panel init blob is at file offset **0x420** too (its `.data` also starts at file 0x40), length **0x3C00** (120 entries). It uses a different command set (e.g. `6C 01`, `03 01`, `35 00`, `53 20`, `51 0000`, `72 00`, `6D 00`, `6F 01`, `59 09`, … `B4` <24 bytes>, `F0 AA14`, `65/CA` pairs), ending with `cmd=11`, delay 100 ms, `cmd=29`, delay 20 ms. Its `bl_tb0` is also `51 0d bb`.

### 2.6 Delay-marker semantics (INFERENCE, strong)

`0xFFFC` is not a standard MCS DCS command. It always appears where the vendor power sequencing requires a wait (after `0x11`/`0x28`/`0x29`), and its `count` field holds 120/100/20. The module imports `msleep`, `cmdq_pkt_sleep`, `__udelay`, `__const_udelay`. Therefore `cmd=0xFFFC, count=N` is an out-of-band **N-millisecond delay directive**, not a DCS packet. Host `objdump` cannot disassemble aarch64, so this was not confirmed by instruction decoding — it is inferred from placement and imported primitives.

---

## 3. Panel modes / refresh rate / video vs DSC (VERIFIED + INFERENCE)

### 3.1 Modes present (VERIFIED by symbols and strings)

The module contains four complete parameter sets:

```
ext_params_60hz   / switch_mode_60hz
ext_params_90hz   / switch_mode_90hz
ext_params_120hz  / switch_mode_120hz
ext_params_144hz  / switch_mode_144hz
```

Related log strings confirm dynamic refresh switching:
```
6[LCM] %s FPS form %d to %d begin
6[LCM] %s FPS=%dHz en=%d, mapped_level=%d, aod=%d
6[LCM] %s level is %u,aod_mapped_level is %u
6[LCM] Updated min frequency to %d kHz
6[LCM] Locked CPU %d min frequency to %d kHz
```
and symbols `get_mode_by_id`, `lcm_get_modes`, `g_lcm_fresh_mode`, `drm_mode_vrefresh`.

### 3.2 Per-refresh payloads (VERIFIED raw bytes; interpretation is INFERENCE)

`switch_mode_*` are 120-byte raw payloads (not 0x80-byte setting-table entries). First 20 bytes:

```
144Hz: 77 d2 06 00 38 04 c0 04 c4 04 d4 04 00 00 60 09 a6 09 a8 09 d0 09
120Hz: 56 27 06 00 38 04 28 05 2c 05 3c 05 00 00 60 09 a2 09 a4 09 cc 09
 90Hz: 56 27 06 00 38 04 28 05 2c 05 3c 05 00 00 60 09 e6 0c e8 0c 10 0d
 60Hz: 56 27 06 00 38 04 28 05 2c 05 3c 05 00 00 60 09 6e 13 70 13 98 13
```

Read as little-endian u16 after the first byte, INFERENCE:
- `0x0438 = 1080` (active width), present in all four.
- 144 Hz: `0x04C0=1216`, `0x04C4=1220`, `0x04D4=1236`, `0x0960=2400`, `0x09A6=2454`, `0x09A8=2456`, `0x09D0=2480` (small HFP/VFP → high rate).
- 120 Hz: `0x0528=1320`, `0x052C=1324`, `0x053C=1340`, `0x0960=2400`, `0x09A2=2450`, `0x09A4=2452`, `0x09CC=2484`.
- 90 Hz: same first group as 120 Hz; `0x0CE6=3302`, `0x0CE8=3304`, `0x0D10=3344`.
- 60 Hz: `0x136E=4974`, `0x1370=4976`, `0x1398=5016` (~2× total line time vs 120 Hz).
- Common `0x0960 = 2400` = active height in the 144 Hz case.

These look like panel-internal timing-controller register values (HSA/HBP/HFP/VSA/VBP/VFP) sent on FPS change. The exact register map of the NT37706A IC is not present in the dump.

### 3.3 `ext_params_*` comparison (VERIFIED)

The four `ext_params_*` arrays are 0x10958 bytes each and are **almost identical**. Only two u32 fields differ:

```
offset 0xA0 : 120Hz=0x0000002F   144Hz=0x0000032F   90Hz=0x0000012F   60Hz=0x0000022F
               (bytes: 2f 00 00 00 / 2f 03 00 00 / 2f 01 00 00 / 2f 02 00 00)
offset 0xEC : 120Hz=0x00880101   144Hz=0x00890101   90Hz=0x00880101   60Hz=0x00880101
               (byte 3 only: 0x88 vs 0x89)
```
INFERENCE: byte 1 at +0xA0 is a 4-value mode index (0=120, 1=90, 2=60, 3=144), byte 0 (`0x2F`) is constant; +0xEC byte 3 is a 144 Hz-specific flag/divisor.

### 3.4 Video mode + DSC evidence (VERIFIED)

- Module name and DT compatible contain `dsi,vdo,dsc` — **DSI video mode with DSC**, not command mode.
- `modinfo depends: mtk_panel_ext,mtk-cmdq-drv-ext`; imports `mipi_dsi_dcs_write_buffer`, `mipi_dsi_generic_write`, `mipi_dsi_dcs_read`, `cmdq_pkt_sleep`.
- `mediatek_drm_v1.ko` strings:
  ```
  DSI_Config_VDO_Timing_with_DSC
  %s, data_rate=%d, dsc_enable=%d, mode=%d
  6[DISP]lane_num:%d,Ext_TE_EN:%d,Ext_TE_Edge:%d,HSTX_CKLP_EN:%d
  6[DISP]DSI_HSA / DSI_HBP / DSI_HFP / DSI_VSA / DSI_VBP / DSI_VFP / DSI_VACT / DSI_BLLP
  3[DISP][E]%s error, HFP:%u < DPHY HFP minimum limitation:%u
  6[DISP]%s, da_hs_zero=0x%x, ps_wc=0x%x, lane_num=0x%x
  6[DISP]%s, lpx=0x%x, da_hs_exit=0x%x, da_hs_prep=0x%x
  6[DISP]BL/RG/BG TX timing: LPX, HS_PRPR, HS_ZERO, HS_TRAIL, DA_HS_EXIT
  6[DISP]MIPITX Clock:%d
  6[DISP]set mipitx's data rate: %lu Hz
  6[DISP]MIPITX Lane Swap mapping / PN Swap mapping
  6[DISP]== MIPI REGS:0x%pa ==  /  ===== mt6382 MIPI%d REGS =====
  MIPI_TX_REG / DSI2_REG / TX_CMDQ_REG    (register-table labels)
  ```
- DCS `0x2A`/`0x2B` in the init table explicitly set the 1080×2400 window (§2.4).
- `CONFIG_DRM_MIPI_DSI=y` in kernel config; `CONFIG_GENERIC_PHY_MIPI_DPHY=y`. The MTK DRM/DSC stack itself is out-of-tree modules (not reflected in `kernel_config.txt`).

### 3.5 ATA / ESD panel-ID check (VERIFIED)

Strings/symbols: `panel_ata_check`, `ATA read data %x %x %x`, `ATA expect read data is %x %x %x`, `mipi_dsi_dcs_read`, `panel_driver_status`, `6%s+ skip probe due to not current lcm`, `6No panel connected,skip probe lcm`. The driver reads a 3-byte ID over DSI and compares against an expected value before attaching — relevant if an XNU driver wants to detect the panel.

---

## 4. Backlight (VERIFIED + INFERENCE)

| Evidence | Value |
|---|---|
| `bl_tb0` in active module, file offset 0x41C | raw bytes **`51 0d bb`** = DCS `0x51` (Write Display Brightness) with 16-bit payload `0x0DBB` (3515 decimal) as the template/default |
| `bl_tb0` in TM module, same offset | identical `51 0d bb` |
| Init table entry 187 | `cmd=0x51 cnt=2 data=0000` → brightness zeroed at init |
| Live FDT `mtk-leds/backlight` | `max-brightness = <0x13ff>` (5119), `max-hw-brightness = <0x13ff>`, `label = "lcd-backlight"`, `led-mode = <0x04>`, `pwm_config = <0 1 0 0 0>` |
| Init table entry 163 (`B0` bank) | contains `...08 51 0b 6a 0d bb` — `0x0DBB` also appears as a register value |
| DT `doze_backlight` on both panel nodes | `<0x03 0x02 0x0f 0x1d>` |
| Driver strings | `doze_backlight`, `panel_doze_enable`, `panel_doze_disable`, `panel_set_aod_light_mode`, `6[LCM] %s lcm_doze_backlight:%d;%d;%d;%d;`, `6[LCM] %s level = %d, mapped_level = %d`, `lcm_setbacklight_cmdq`, `of_find_backlight_by_node` import |
| Loaded modules | `pwm_mtk_disp`, `leds_mtk_disp`, `leds_mtk_pwm`, `mtk_pwm`, `mediatek_drm_v1` |
| `disp_pwm0@1100e000` | compatible `mediatek,disp_pwm0` / `mediatek,mt6789-disp-pwm`, `#pwm-cells = <2>` |

**Verified:** the panel brightness itself is set by **DCS 0x51** over the DSI link (the driver writes `bl_tb0`, which starts with `0x51`). The SoC's `disp_pwm` block exists and feeds the external backlight/boost hardware, but it is not the DCS payload written to the panel IC.
**Inference:** Android brightness (0…0x13FF) is scaled into the two parameter bytes of `bl_tb0[1..2]`; the exact scaling/gamma curve lives in `mtk_drm_v1.ko` / userspace and was not recoverable with the available read-only tooling. `0x0DBB` is likely the maximum DSC brightness code used by this panel (3515 of 4095 possible 12-bit codes), but this is not proven — the init table sets `0x0000` and the DT reports 5119 levels.

The `cust_color.xml` files are **PQ/color tuning, not backlight level tables**: `<u4Brightness>4</...>` are PQ index selects, and `<BRIGHTNESS>0x3C0 0x3D0 … 0x450</...>` / `0x400 ×10` are neutral gain values (0x400 = 1.0) inside `hdr_pqindex` / `disp_game_pqindex`.

---

## 5. DSC parameters (VERIFIED values, INFERENCE for field names)

### 5.1 QP/BPG/threshold arrays (exact 32-bit little-endian values)

`range_min_qp` (15 u32, file 0x40):
```
[0, 0, 1, 1, 3, 3, 3, 3, 3, 3, 5, 5, 5, 9, 12]
```

`range_max_qp` (15 u32, file 0x7C):
```
[4, 4, 5, 6, 7, 7, 7, 8, 9, 10, 10, 11, 11, 12, 13]
```

`range_bpg_ofs` (15 i32, file 0xB8) — signed:
```
[2, 0, 0, -2, -4, -6, -8, -8, -8, -10, -10, -12, -12, -12, -12]
```
(raw hex: `02 00 00 00 | 00 | 00 | fe ff ff ff | fc ff ff ff | fa | f8 | f8 | f8 | f6 | f6 | f4 | f4 | f4 | f4`)

`buf_thresh` (14 u32, file 0x8320):
```
[0x380, 0x700, 0xA80, 0xE00, 0x1180, 0x1500, 0x1880, 0x1A40, 0x1C00, 0x1DC0, 0x1E40, 0x1EC0, 0x1F40, 0x1F80]
= [896, 1792, 2688, 3584, 4480, 5376, 6272, 6720, 7168, 7616, 7744, 7872, 8000, 8064]
```
These are the standard DSC RC buffer thresholds for a 540-pixel slice width.

### 5.2 DSC config block inside each `ext_params_*` (VERIFIED numbers)

At `ext_params + 0xB80` there is a contiguous block of 31 u32 values, followed by four pointers (relocations) to `buf_thresh`, `range_min_qp`, `range_max_qp`, `range_bpg_ofs` at `ext_params + 0xBF8 / 0xC08 / 0xC18 / 0xC28`.

Values (identical in the BOE and TM modules, and in all four refresh-rate variants):
```
offset : value (hex / decimal)
+b80 : 0x08 (8)
+b84 : 0x09 (9)
+b88 : 0x01 (1)
+b8c : 0x80 (128)
+b90 : 0x960 (2400)
+b94 : 0x438 (1080)
+b98 : 0x0C (12)
+b9c : 0x21C (540)
+ba0 : 0x21C (540)
+ba4 : 0x200 (512)
+ba8 : 0x20E (526)
+bac : 0x20 (32)
+bb0 : 0x11F (287)
+bb4 : 0x07 (7)
+bb8 : 0x0C (12)
+bbc : 0x8BB (2235)
+bc0 : 0x87A (2170)
+bc4 : 0x1800 (6144)
+bc8 : 0x10F0 (4336)
+bcc : 0x03 (3)
+bd0 : 0x0C (12)
+bd4 : 0x2000 (8192)
+bd8 : 0x06 (6)
+bdc : 0x0B (11)
+be0 : 0x0B (11)
+be4 : 0x03 (3)
+be8 : 0x03 (3)
+bec : 0x00
+bf0 : 0x01
+bf4 : 0x00
```

**INFERENCE — field mapping by value signature** (matches MediaTek `struct mtk_panel_dsc_params`):
```
8      = bits_per_component
9      = line_buf_depth
1      = block_pred_enable
2400   = pic_height / max_height
1080   = pic_width / max_width
12     = bpp (12 bits per pixel)
540    = slice_width (×2 slices => 1080)
540    = max_width
512    = initial_xmit_delay
526    = initial_dec_delay
32     = initial_scale_value
287    = scale_increment_interval
7      = scale_decrement_interval
12     = first_line_bpg_offset
2235   = nfl_bpg_offset
2170   = slice_bpg_offset
6144   = initial_offset
4336   = final_offset
8192   = rc_model_size
6      = rc_edge_factor
11, 11 = rc_quant_incr_limit0 / limit1
3, 3   = rc_tgt_offset_hi / lo
3, 12  = flatness_min_qp / flatness_max_qp (order uncertain)
```
`128` at +0xB8C and the trailing `0,1,0` are not confidently mapped. DSC version is probably 1.2 but no explicit version string exists in the module; do not treat that as verified.

### 5.3 DSC hardware block in DT (VERIFIED)

```
disp_dsc_wrap0@14012000 {
    compatible = "mediatek,disp_dsc_wrap", "mediatek,mt6789-disp-dsc";
    reg = <0x0 0x14012000 0x0 0x1000>;
    interrupts = <0x0 0x12c 0x4 0x0>;     /* IRQ 300 */
    clocks = <&mmsys 0x0b>;               /* mm_disp_dsc_wrap0 */
};
```

---

## 6. Clocks and DSI PHY / register map (VERIFIED)

### 6.1 Clock drivers

`clk-mt6789-disp_dsc.ko` (`name=clk_mt6789_disp_dsc`, `depends=clk-common`):
```
strings: disp_dsc_dsc_en / disp_ck / clk-mt6789-disp_dsc / mediatek,mt6789-disp_dsc
symbols: disp_dsc_mcd (.rodata+0x190, 0x10), disp_dsc_cg_regs (.rodata+0x1A0, 0x0C),
         disp_dsc_clks (.rodata+0x1B0, 0x50)
uses: mtk_clk_simple_probe, mtk_clk_gate_ops_no_setclr_inv
```
It exposes a single gate named **`disp_dsc_dsc_en`** with parent **`disp_ck`**. The raw `disp_dsc_cg_regs` bytes in the file are zero (register offsets are likely supplied/fixed at runtime or in the parent syscon) — no reliable numeric offset can be reported from the file.

`clk-mt6789-mmsys.ko` gate-name pool (extracted in order from `.rela.rodata`) gives the DT clock indices:
```
index 0x0B (11) -> mm_disp_dsc_wrap0   (parent disp_ck)
index 0x10 (16) -> mm_dsi0             (parent disp_ck)
index 0x15 (21) -> mm_dsi0_dsi_domain  (parent dsi_occ_ck)
index 0x05 (5)  -> mm_disp_wdma0
... plus mm_disp_ovl0, mm_disp_rdma0, mm_disp_aal0, mm_disp_ccorr0,
    mm_disp_color0, mm_disp_gamma0, mm_disp_dither0, mm_disp_postmask0,
    mm_disp_mutex0, mm_smi_*, mm_disp_26m_ck, mdp_*
```

### 6.2 DT clock/PHY wiring (VERIFIED, from `fdt.dts`)

```
syscon@14000000  compatible = "mediatek,mt6789-mmsys","syscon"; #clock-cells = <1>; phandle 0x36
syscon@...(mipi tx) mipi_tx_config@11f60000:
    compatible = "mediatek,mipi_tx_config0","mediatek,mt6789-mipi-tx";
    reg = <0x0 0x11f60000 0x0 0x1000>;
    clocks = <0x54>;
    #clock-cells = <0>; #phy-cells = <0>;
    clock-output-names = "mipi_tx0_pll";  phandle 0xA4

dsi@14013000:
    compatible = "mediatek,dsi0","mediatek,mt6789-dsi";
    reg = <0x0 0x14013000 0x0 0x1000>;
    interrupts = <0x0 0x12d 0x4 0x0>;      /* IRQ 301 */
    clocks = <0x36 0x10>, <0x36 0x15>, <0xa4>;
    clock-names = "engine", "digital", "hs";
    phys = <0xa4>;  phy-names = "dphy";    phandle 0x19F

dsi_te: compatible = "mediatek, dsi_te-eint"; status = "okay";
        interrupt-parent = <0x49>; interrupts = <0x53 0x1>;   /* GPIO irq 83 */
```

So: DSI engine clock = `mm_dsi0`, digital clock = `mm_dsi0_dsi_domain`, HS clock = `mipi_tx0_pll` from the MIPI TX PHY at `0x11F60000`, DSC wrapper clock = `mm_disp_dsc_wrap0`. MIPI TX base `0x11F60000`, DSI base `0x14013000`, DSC base `0x14012000`, display syscon `0x14000000`, disp PWM `0x1100E000`.

### 6.3 PHY registers referenced by the DRM driver (VERIFIED strings, no numeric offsets recovered)

`mediatek_drm_v1.ko` contains register-table labels `MIPI_TX_REG`, `DSI2_REG`, `TX_CMDQ_REG` and debug dumps `== MIPI REGS:0x%pa ==`, `===== mt6382 MIPI%d REGS ======`, `===== mt6382 DSI%d REGS =====`, `- DSI CMD REGS -`. It references PHY timing fields by name: `lane_num`, `data_rate`, `ap_tx_data_rate`, `da_hs_zero`, `ps_wc`, `lpx`, `da_hs_exit`, `da_hs_prep`, `hs_thssettle`, `fjump_deskew_reg`, `ddl_cntr_ref_reg`, `Ext_TE_EN`, `Ext_TE_Edge`, `HSTX_CKLP_EN`, `MIPITX Clock`, lane swap and PN swap. Numeric register offsets for the MIPI TX are encoded in tables, not in strings, and were not decoded here (host objdump cannot disassemble aarch64). The DTS MMIO addresses above are the authoritative base addresses.

---

## 7. Device-tree panel nodes (VERIFIED, live FDT)

```
dsi@14013000 {
    ... (see §6.2)
    #address-cells = <1>; #size-cells = <0>;

    panel1@0 {                                   /* ACTIVE */
        compatible = "nt37706a,fhdp,dsi,vdo,dsc,boe,boe,144hz,x6886";
        reg = <0x0>;
        vddi-gpios   = <0x49 0x31 0x0>;          /* GPIO 49 */
        dvdd-supply  = <0x1e5>;
        vci-gpios    = <0x49 0x3d 0x0>;          /* GPIO 61 */
        reset-gpios  = <0x49 0x55 0x0>;          /* GPIO 85 */
        pinctrl-names = "default";
        doze_backlight = <0x03 0x02 0x0f 0x1d>;
        port { endpoint { remote-endpoint = <0x1e6>; phandle = <0x1e7>; }; };
    };

    panel2@0 {                                   /* alternate supplier, not populated */
        compatible = "vtdr6126a,fhdp,dsi,vdo,dsc,tm,tm,144hz,x6886";
        reg = <0x1>;
        (same GPIOs / supplies / doze_backlight)
        port { endpoint { remote-endpoint = <0x1e6>; phandle = <0x223>; }; };
    };
};
```

Power supplies: `vddi-gpios`, `vci-gpios` and a `dvdd-supply` regulator. Module strings confirm runtime DVDD handling:
```
6<%s,%d> lcm dvdd regulator get fail!
<%s,%d> lcm dvdd set vol fail!ret[%d].
<%s:%d> set lcm dvdd vol error!ret[%d]
6<%s,%d> lcm dvdd enable error!ret[%d]
```
GPIO reset is driven via `gpiod_set_value`; `lcm_rst_out0_gpio`, `lcm_rst_out1_gpio`, `mode_te_te` pinctrl states exist in the DSI node's pinctrl list.

Note: `x6886-base.dts` (stock DT) has `dsi_te { status = "disabled" }` and no panel children — the panel nodes and TE enable come from an overlay present in the live FDT.

---

## 8. Other verified display facts useful for the port

- Kernel: `6.12.38-android16-5-gcc51d883045d-4k`, `SMP preempt mod_unload modversions aarch64`; built with Clang 19.0.1 (Android r536225, LTO+PGO+BOLT+MLGO).
- `kernel_config.txt`: `CONFIG_DRM=y`, `CONFIG_DRM_MIPI_DSI=y`, `CONFIG_DRM_PANEL=y`, `CONFIG_BACKLIGHT_CLASS_DEVICE=y`, `CONFIG_PWM=y`, `CONFIG_GENERIC_PHY_MIPI_DPHY=y`, `CONFIG_COMMON_CLK=y`. The MediaTek DRM/DSC/panel drivers are out-of-tree modules.
- DRM connector exposed on the live device: `/sys/class/drm/card0-DSI-1` (under `/devices/platform/soc/14000000.dispsys_config/drm/card0`); `/sys/class/backlight` is empty in the dump snapshot, while DT declares `lcd-backlight`.
- `getprop.txt` confirms resolution: `persist.oplus.display.screen.widthgroup=[1080,...]`, `heightgroup=[2400,...]`, `ro.vendor.camera.lcm.width=1080`, `height=2400`; `ro.display.screen_type=0`; `ro.odm.tr_display_panel_type_config=2`.
- No `display.config` or `device_features` blob exists in the stock dump (consistent with the project notes).
- DT `helper-name` list (also present as feature strings in `mediatek_drm_v1.ko`) includes `MTK_DRM_OPT_LFR`, `MTK_DRM_OPT_DYN_MIPI_CHANGE`, `MTK_DRM_OPT_HBM`, `MTK_DRM_OPT_AOD`, `MTK_DRM_OPT_ESD_CHECK_RECOVERY`, `MTK_DRM_OPT_IDLEMGR_ENTER_ULPS`, `MTK_DRM_OPT_IDLEMGR_KEEP_LP11` — dynamic FPS, HBM, AOD and ESD recovery are all active features.

---

## 9. Concrete porting handoff (what to copy, and from where)

1. **Panel init blob** — copy **0x7F00 bytes from file offset 0x420** of
   `/home/berucrypt00/otadump/vendor_boot/ramdisk/lib/modules/nt37706a_fhdp_dsi_vdo_dsc_boe_boe_144hz_x6886.ko`.
   Walk 254 entries of 0x80 bytes: `u32 cmd; u8 count; u8 para[123]`. Treat `cmd==0xFFFC` as a `count`-millisecond delay, skip zero entries. The final `0x11 → delay 120 → 0x29 → delay 20` must be kept.
2. **Resolution window** — DCS `0x2A {00,00,04,37}` and `0x2B {00,00,09,5F}` (1080×2400) are already in the blob.
3. **Backlight** — DCS `0x51` with 2 bytes; template `51 0D BB`. Scale Android 0..0x13FF into the two bytes; init value `00 00`.
4. **HBM** — DCS `0xA9` with the exact 18-byte enter/exit payloads in §2.5.
5. **Sleep** — `0x28`, 20 ms, `0x10`, 100 ms (§2.5).
6. **FPS switching** — use the four 120-byte `switch_mode_*` payloads in §3.2; all four `ext_params_*` DSC configs are identical (§5.2).
7. **DSC** — use the 15-entry min-QP/max-QP/BPG-offset arrays and 14-entry `buf_thresh` in §5.1, with the 31 u32 parameter block in §5.2. Slice geometry 1080 = 2 × 540, bpp 12, 8 bpc.
8. **Clocks/PHY** — bring up `mm_disp_dsc_wrap0`, `mm_dsi0`, `mm_dsi0_dsi_domain` and `mipi_tx0_pll`; MMIO bases 0x14000000 / 0x14012000 / 0x14013000 / 0x11F60000; IRQs 300 (DSC) and 301 (DSI); TE GPIO IRQ 83.
9. **Panel GPIOs** — reset 85, vddi 49, vci 61, dvdd regulator (DT phandle 0x1E5).
10. If the alternate supplier must be supported, repeat steps 1–6 with its 0x3C00-byte init blob at file offset 0x420 of `vtdr6126a_fhdp_dsi_vdo_dsc_tm_tm_144hz_x6886.ko`; its DSC block is identical.

---

## 10. Limitations / not verified (honest boundaries)

- Host `objdump` is x86-only ("can't disassemble for architecture UNKNOWN"), so no instruction-level confirmation of the `0xFFFC` delay marker, brightness scaling, or MIPI TX numeric register offsets was possible. `readelf`/`nm`/`xxd`/`strings` and byte-pattern decoding were used instead.
- Exact DSC **version** (1.1 vs 1.2), DSI lane count, and pixel/HS data rate are not stored as readable numbers in the panel module; they live in `mediatek_drm_v1.ko` tables/calculations. INFERENCE from DSC parameters: 4 lanes is typical for 1080p120/144 with DSC, but this is **not** verified here.
- `VBID`/panel-EDID contents and the expected 3-byte ATA ID values are inside code constants (`panel_ata_check`) that were not decoded.
- The `cust_color.xml` files are 194 KB PQ/color LUTs; they are not needed for basic panel bring-up and contain no panel init or backlight-level tables.
