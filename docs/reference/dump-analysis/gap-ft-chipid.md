# Gap: FT3682g vs FT3683G touch IC — resolution

Scope: **dump-only sources**. Stock dump `/home/berucrypt00/otadump` plus live-device data
already pulled into `/home/berucrypt00/hakios-x6886/devices/x6886/evidence/`. No phone access, no writes
to the dump. `Documents/port-tools` and the reference-port trees were excluded per instruction.

---

## VERDICT: RESOLVED LOCALLY (high confidence)

The touch controller fitted/driven on this X6886 is **FT3683G**. `ft3682g` is a Transsion/ODM
**board-label string** in the display+TP supplier descriptor (and one leftover alternate IC-name
string inside the driver), **not** the identity of the driver or the production test config.

The earlier note (`touch.md` §4/§10: "cannot be settled from static files") missed two decisive
dump artifacts. With those included, the gap closes without a live read.

---

## 1. Decisive local evidence

### 1.1 Factory test config names the IC explicitly — and the driver consumes that exact file

`/home/berucrypt00/otadump/odm/firmware/Conf_MultipleTest.ini` (CRLF text), section `[Interface]`:

```
[Valid_File]
OnlyMultipleTest=1
[Interface]
IC_Type=FT3683G          <-- line 23
Normalize_Type=0
CascadingType=0
Interface_Type=1
...
Max_Points=10
ixMaxPixel=1080
iyMaxPixel=2400
```

This is the production-line TP test configuration shipped in `odm/firmware` for this build.
It is not a stray file: the loaded FocalTech module itself contains both literals:

```
strings -a -t x odm_dlkm/lib/modules/focaltech_ft3683g.ko
  0x33027 Conf_MultipleTest.ini
  0x35587 IC_Type
  0x2bf58 [FTS_TS][TEST]%s:FTS_INI_FILE_PATH:%s
  0x2e1ff ini_file_name:%s
```

i.e. `focaltech_ft3683g.ko`'s `focaltech_test_ini.c` path parses `IC_Type` from
`Conf_MultipleTest.ini`, and the value in this device's copy is `FT3683G`. `touch.md` §2 claimed
"no FTS .ini test config in the dump" — that was wrong; the file is present at
`odm/firmware/Conf_MultipleTest.ini`.

An exhaustive case-insensitive `grep -r -a` for `IC_Type|Conf_MultipleTest|FT3683G|FT3682G`
across `odm/`, `odm_dlkm/`, `vendor/`, `vendor_boot/` and `vendor_dlkm/` returns exactly two
relevant hits and nothing else:

```
odm/firmware/Conf_MultipleTest.ini:23:IC_Type=FT3683G
odm_dlkm/lib/modules/focaltech_ft3683g.ko:2209:Conf_MultipleTest.ini  short_get_adcdata_incell ...
```

`FT3682G` matches **zero** files. (The remaining raw hits are substring noise inside large
Mali/camera/audio/Clang `.so` string tables, unrelated to touch.)

### 1.2 Live-device `modules.txt` proves the FT3683G driver is actually loaded

`/home/berucrypt00/hakios-x6886/devices/x6886/evidence/modules.txt` (pulled from the phone, 2026-11-23):

```
69: chipsemi_chsc5xxx_old 245760 0 - Live ... (OE)
70: focaltech_ft3683g     262144 0 - Live ... (OE)
71: adaptive_ts           155648 2 chipsemi_chsc5xxx_old,focaltech_ft3683g, Live ... (OE)
```

`focaltech_ft3683g` is `Live` on the running stock device. `getprop.txt` corroborates the touch
insmod oneshot ran: `[ro.boottime.insmod_touch_sh]: [4224343615]`,
`[persist.sys.ostats_tpd.enable]: [1]`.

### 1.3 Every hard identifier in the dump says 3683G; only soft labels say 3682g

| Source | Exact evidence | Path / offset |
|---|---|---|
| Module filename | `focaltech_ft3683g.ko` | `odm_dlkm/lib/modules/` (also `odm_dlkm_b/`) |
| modinfo name | `name=focaltech_ft3683g` | `.ko` file offset `0x37124` |
| OF alias | `alias of:N*T*Cfocaltech,fts focaltech_ft3683g` | `odm_dlkm/lib/modules/modules.alias:25-26` |
| modules.dep | `.../focaltech_ft3683g.ko: .../adaptive-ts.ko` | `modules.dep:17` |
| modules.load | `focaltech_ft3683g.ko` (first entry) | `modules.load:1` |
| init modprobe | `modprobe\|ft3683g.ko` | `vendor/etc/init.insmod.touch.cfg:12` |
| Source tree | `.../transsion/vendor/focaltech/ft3683g_spi/*.c` | multiple strings, e.g. `0x2c2f8` |
| Test IC file | `.../focaltech_test/supported_ic/focaltech_test_ft3683g.c` | `0x2a7e8` |
| Controller string | `ft3683g_controller`, `ft3683g` | `0x822ab`, `0x34bf2` |
| Version | `FocalTech V4.1 20230424` | `0x35859` |
| Factory config | `IC_Type=FT3683G` | `odm/firmware/Conf_MultipleTest.ini:23` |
| Live loaded module | `focaltech_ft3683g ... Live` | `devices/x6886/evidence/modules.txt:70` |

The **only** two `3682g` occurrences anywhere:

1. Live FDT supplier string (`devices/x6886/evidence/fdt.dts`, `touch` node):
   `supplier-0 = "nt37706a_fhdp_dsi_vdo_dsc_boe_boe_144hz_x6886, focaltech, ft3682g";`
   — this is a panel+TP procurement label in the display config, concatenated with the FW name
   `supplier-vendor-fwname-0 = "focaltech_ts_fw.bin"`.
2. One standalone string in the driver at `.ko` file offset `0x2B613`:
   bytes `66 74 33 36 38 32 67 00` = `"ft3682g\0"`, immediately followed by `fts_set_edg_level`.

There is **no** module file, no `modules.alias`, no source path, no `.ini` and no `.cfg` anywhere
in the dump naming an `ft3682g` driver. A whole-dump filename scan (`all_files.txt`, 682 KB)
finds only `focaltech_ft3683g.ko` and no `ft3682` file at all.

### 1.4 Live dmesg cannot contradict this (but does not help)

`devices/x6886/evidence/dmesg.txt` covers uptime `31277.1s → 31436.4s` (~2.5 minutes of a late rolling
buffer, ~8.7 h after boot). Counts: `DISP`=160, `STP-PSM`=52, **`FTS_TS`=0, `fts`=0, `tpd`=0**.
The 52 `TP` hits are all `[STP-PSM]` (MediaTek connectivity), not touch panel. All FTS
`chip id` / `boot id` / probe lines were boot-time and are long gone from this capture. This is
why the live read previously looked necessary — it is a buffer-window problem, not missing evidence.

`devices/x6886/evidence/input.txt` shows `/sys/class/input/event4/device/name: mtk-tpd`. Note: `mtk-tpd`
appears in **both** `focaltech_ft3683g.ko` (`fts_ts_id`) and `chipsemi_chsc5xxx_old.ko`, so this
alone is **not** decisive; it only confirms a Transsion THP touch input is registered.

---

## 2. Firmware binary — exhaustive analysis (no chip ID encoded, as expected)

File: `/home/berucrypt00/otadump/odm/firmware/focaltech_ts_fw.bin`
237,568 bytes (`0x3A000`), SHA-256 `41486e2f6e47d16ce1e5d2ec5a8e35b33a510e0eb713c8e99513d9ababe133bd`.

### `strings -a -t x -n 3`
2,804 strings total. After filtering the 8051 opcode garbage (strings like `@9f?`, `<l7`, `?_'k`),
the **only** meaningful ASCII is:

```
     140 x6886
     160 Transsion
  0x385dd x6886        (second copy in the config/param block near end of image)
  0x385fd Transsion
```

Zero occurrences of `FT`, `ft`, `IC`, `VIP`, `PRJ`, `PROJECT`, `3682`, `3683`. No 4-byte
project/vendor code that decodes to a chip number.

### Headers / hexdumps
- `0x000–0x05F`: vector table — reset vector `00 00 20 60`, then repeated `00 03 8c 7c`
  default-handler entries. Code starts `0x060`: `e0 60 3c 00 e1 f0 cc 02 ...` (8051 `MOVX`, `LJMP`).
- `0x100–0x13F`: config block — `e8 00 17 ff 8b 08 74 f7 00 00 00 00 00 00 29 d6`,
  zeros, then `70 8f 00 ff da 25 ff ff 00 ff ff ff 00 ff 00 ff`,
  `29 01 01 fe 08 f7 03 fc ff ff ff ff ff ff ff 00`. `29 d6` is a marker, not a chip code.
- `0x140`: `78 36 38 38 36 00` = `"x6886\0"`, then `FE` padding.
- `0x160`: `54 72 61 6e 73 73 69 6f 6e 00` = `"Transsion\0"`.
- `0x1A0–0x385B0`: zeros / 8051 code / calibration tables; no project-code field.
- `0x385BB–0x38620`: second copy of the same project block plus config arrays.

### Byte-pattern search (`36 82`, `36 83`, `82 36`, `83 36`, `A8`)
```
36 82 : 0xd260, 0x22d7a, 0x22d94   (3 hits)
36 83 : NONE
82 36 : 0xc759, 0x1e6bb, 0x1e727, 0x1e7d5, 0x1e829
83 36 : 0x21f69, 0x2275f
A8    : 326 hits (opcode/data noise)
```

Context of all three `36 82` hits proves they are 8051 instructions, not an ID field:

```
0xd260   : e0 18 36 82 f0 38 3c 6c 3e 6d e3 ff c6 7f   (MOVX A,@R0 ; ADDC A,@R0 ; ANL C,/bit ; MOVX @R0,A ...)
0x22d7a  : 3f 9e e0 0e 36 82 3f 9e e0 40 b7 9b c5 01
0x22d94  : 3f 9e e0 0e 36 82 3f 9e e8 40 3d 7b e0 40
```

`36` = `ADDC A,@R0`, `82` = `ANL C,/bit` in 8051 — identical opcode sequences at `0x22d7a` and
`0x22d94`. `36 83` (the value that would support "3683G encoded in FW") occurs **zero** times.
Therefore the FW image carries no chip ID by design; the driver reads it over SPI at runtime.

This matches the driver strings:
```
0x28fae  [FTS_TS]%s:chip id read invalid, read:0x%02x%02x
0x2d509  [FTS_TS/I]%s:read boot id:0x%02x%02x
0x2d993  [FTS_TS/I]%s:get ic information, chip id = 0x%02x%02x(cid type=0x%x)
0x32cac  focaltech_ft3683g
```

---

## 3. Are FT3682g and FT3683g the same silicon/protocol family?

**Dump-supported conclusion: yes — same FocalTech FT3xxx SPI family, same protocol and firmware.**

Evidence (all from this dump):
1. A **single** FW binary `focaltech_ts_fw.bin` is declared for the panel whose supplier string
   says `ft3682g` (`supplier-vendor-fwname-0`), and that same image is the only FocalTech FW in
   the dump. Transsion ships one image for both labels.
2. A **single** driver binary carries both names: the `ft3682g` string (`0x2B613`) sits inside
   the `ft3683g_spi` source tree build. `ft3682g` is an alternate/project alias, not a separate
   driver.
3. Bus binding is the **generic** FocalTech compatible `focaltech,fts` (not a chip-specific
   compatible), implemented by `focaltech_spi_v2.c`; the test-support file is specifically
   `focaltech_test_ft3683g.c`. So the protocol is the generic FTS SPI-v2 used by the FT3683G
   branch.

Caveat: this is inference from dump artifacts, not a vendor datasheet. The literal silicon die
marking was never available locally. Practically, for the XNU port the correct choice is the
FT3683G FTS SPI-v2 protocol path — there is nothing in the dump that requires a distinct
FT3682G protocol.

---

## 4. Contingency: the one live read that would confirm silicon level

Not required to close this gap — the local verdict above stands. But if Beru wants the 2-byte
IC ID from the IC itself, this is the single cheapest read, derived from the driver's own strings:

**Best option — make the driver print its own ID (one reset, no register address needed):**
```sh
adb shell 'dmesg -w | grep -iE "chip id|boot id|ic information"' &
# then trigger one TP reset via the driver's own sysfs node:
adb shell 'echo 1 > /sys/class/input/event4/device/fts_hw_reset'
```
The driver will log one of:
```
[FTS_TS/I]...:get ic information, chip id = 0x%02x%02x(cid type=0x%x)
[FTS_TS/I]...:read boot id:0x%02x%02x
```
Expected for FT3683G-family silicon is a 2-byte ID whose first byte is `0x36` (FT3xxx prefix);
capture the exact value and compare with the cid-type table compiled into `fts_check_cid()`.

**Alternative — raw register read via the driver's sysfs node:**
```
/sys/class/input/event4/device/fts_rw_reg      (may also be /sys/bus/spi/devices/spi1.0/fts_rw_reg)
```
The node's formats, verbatim from the `.ko` near `0x2e520`:
```
Read %02X: %02X\n
Write Reg: [%02X]-[%02X]\n
fts_tprwreg_store
```
Write the chip-ID register address to select it, then read the node back for `Read ADDR: VALUE`.
**Important limit:** the chip-ID register address is a compiled immediate inside
`fts_get_ic_information()`/`fts_check_cid()`, not present in any string. `0xA8` does **not**
appear as a register name in this driver (its logged registers are `0x01, 0x06, 0x0A, 0x1c,
0x1d, 0x54, 0x5B, 0x8B, 0x91, 0xBD, 0xC0, 0xC1, 0xD0, 0xFC`). So do not guess a register
number from the dump — use the reset-and-read-dmesg method above, which needs no address.

To locate the exact node on a live device without guessing:
```sh
adb shell 'find /sys -name "fts_rw_reg" -o -name "fts_driver_info" 2>/dev/null'
```

**Addendum — reference ports only (NOT used for the dump-only verdict):**
A completed `grep -r -i "ft368"` over `refs/` finds **no FT3682 or FT3683 anywhere** — only an
existing **FT3680** FocalTech implementation in `refs/hakios-common/drivers/board/goodix.c`
(and its copies in `xnu-12377-hakios`, `hakios-mtk`, `hakios-common/Documentation`). That is
further support for the "one FT3xxx SPI family, one protocol" conclusion: the XNU side already
has a single generic FocalTech path, not one driver per 368x variant.

That reference also documents the exact FT3xxx ID registers, which answers the "which register?"
question without guessing:

```c
#define FOCALTECH_CHIP_ID_H   0xa3
#define FOCALTECH_CHIP_ID_L   0x9f     /* runtime ID = 0x56 0x62 on FT3680 */

/* boot-ROM ID: write 0xAA to reg 0x55, wait 12 ms, read 2 bytes from reg 0x90 */
#define FTS_CMD_START1        0x55
#define FTS_CMD_START2        0xaa
#define FTS_CMD_READ_BOOT_ID  0x90     /* ROM ID = 0x56 0x62 on FT3680 */
```

This matches this dump's driver strings exactly in shape (`chip id = 0x%02x%02x`,
`read boot id:0x%02x%02x`, plus `write FC=0xAA`/`write FC=0x55` from `touch.md` §7.1/§7.2).
So if a raw register read is ever wanted, the two candidates are **`0xA3`/`0x9F`** (runtime ID)
and **`0x90`** after `0x55`→`0xAA` + 12 ms (boot ID). Expected values for FT3683G are not in
any local source — capture and record them, do not assume the FT3680 `0x56 0x62` value applies.

---

## 5. Bottom line for the XNU port

- Bind/implement the **FT3683G FTS SPI-v2** protocol (`focaltech,fts`, SPI1 @ 8 MHz), not an
  FT3682G-specific path.
- Treat `ft3682g` in the FDT supplier string as a board-label alias. Do not propagate it into
  driver naming or FW selection.
- Use `focaltech_ts_fw.bin` verbatim (0x3A000 bytes, bare vector table at offset 0) — no header
  rewrite, no chip patching.
- The remaining uncertainty is only the exact 2-byte silicon ID/cid-type, which has no bearing
  on protocol choice and can be confirmed later with the single reset+dmesg read in §4.
