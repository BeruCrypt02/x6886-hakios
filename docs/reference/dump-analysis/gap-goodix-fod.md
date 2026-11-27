# Goodix optical FOD (X6886) — closed-gap forensic report

Analysis date: 2026-11-23
Sources (read-only, **dump-only** per instruction):
- `/home/berucrypt00/otadump/...` (stock OTA dump: kernel modules, mcRegistry TAs, HALs, props, rc)
- `/home/berucrypt00/hakios-x6886/devices/x6886/evidence/{dmesg.txt,getprop.txt,modules.txt,HARDWARE-IDENTITY.md}` (live-device captures)

Explicitly excluded: `/home/berucrypt00/Documents/port-tools/` (per correction), reference ports, phone access, driver code. Companion prior work: `fingerprint.md` in this directory.

Legend: **[V]** = verified from the named dump file. **[I]** = inference/interpretation. **[M]** = mechanically decoded from machine code (instruction bytes), semantics not proven.

---

## 0. Verdict (up front)

1. **No Goodix (GF39xx) register map, SPI opcode table, or sensor init sequence exists in this dump.** The kernel driver is a transparent SPI pipe. Verified three ways: exhaustive `strings -a -t x -n 3` on both `.ko` files, `modinfo`/`readelf -s` symbol inventory, and a mechanical decode of every immediate constant in the Goodix SPI + chip-ID functions. **Important qualification (§4b):** one TEE trustlet is *partially* plaintext and does reveal a concrete register-write/delay sequence — but for **JV0307 (Focaltech)**, not for Goodix. The Goodix branch there is only a named `tran_read_goodix_chipid` handler; its bytes remain unrecovered.
2. **Vendor is Goodix; the most likely part is GF3976, but the exact SKU is still not provable statically.** The live stack is the `tran_fp.ko` / `/dev/goodix_fp` / `gf_fingerprint.default.so` path using the **generic ODM TA** (`05060000….tabin`). The chip-specific GF3976 TA in `/vendor/app/mcRegistry` belongs to the **inactive** `tran_fp_v2` stack.
3. **No portable matcher path exists locally.** No libfprint, BOZORTH3, Goodix algorithm source, or any open-source fingerprint matcher exists in the dump. Matching is 100% inside the TEE.
4. **Genuinely impossible without TA decryption or a live SPI capture:** command/opcode bytes, register addresses, init/power-on programming sequence, image preprocessing, matcher algorithm, template format, and template keys. See §7 for the precise boundary, including what is *not* impossible (numeric ioctl layouts are recoverable with a proper aarch64 disassembler — not attempted here because the host has no such tool).

---

## 1. Chip / vendor identity — VERIFIED

**[V] Live module state** — `/home/berucrypt00/hakios-x6886/devices/x6886/evidence/modules.txt`:
```
tran_fp      102400  2  - Live ... (OE)          <-- refcount 2 = in use
tran_fp_v2    57344  0  - Live ... (OE)          <-- refcount 0 = loaded, unbound
spi_mt65xx    45056  3  gt9896s,tran_fp,tran_fp_v2
mcDrvModule  311296 13  trusted_mem,tran_fp_v2,rpmb_mtk,...
```
This closes open question #2 of `fingerprint.md`: the **legacy `tran_fp.ko` is the active driver**; `tran_fp_v2.ko` has no consumers. The FDT (`fdt.dts:4558-4591`, `compatible="tran_fp_legacy"`, `goodix;`, `fod_location_xy="540,2210"`) agrees.

**[V] Live properties** — `getprop.txt`:
```
[gf.debug.fp_serial]: [55534B3139372E0049A952D6]      # -> ASCII "USK197." + 00 49 A9 52 D6
[persist.vendor.fingerprint.sensor_type]: [optical]
[persist.vendor.fingerprint.optical.sensorlocation]: [540::2172]
[persist.vendor.sys.fp.goodix.spmt.flag]: [PASS]
[ro.fingerprint_support]: [1]
[ro.optical_fingerprint_support]: [1]
[init.svc.vendor.fingerprint-default]: [running]
```
`gf.debug.fp_serial` is owned by `odm/lib64/libgf_hal.so` (string present) → the Goodix HAL ran and set the SPMT flag to PASS. The runtime FOD centre is `540::2172` (the DTB says `540,2210` — a runtime override, both consistent with a bottom-centred circle on 1080-wide panel).

**[V] DT + userspace glue** (also in `fingerprint.md`): `/soc/spi3@11013000/fingerprint@0`, 27 MHz, CS0, IRQ GPIO13, reset GPIO119, power GPIO59, six `tran_fp_*` pinctrl states, `goodix;` flag; alias `goodix_fp`; `/dev/goodix_fp` 0660 `system:root` in `odm/etc/init/vendor.goodix.rc`; AIDL v3 fingerprint HAL.

**[V] Chip-name tables in the dump:**
- `vendor_boot/ramdisk/lib/modules/tran_fp.ko`: `GOODIX_GF3956`, `GOODIX_GF3976`, `GOODIX_GF3986`, `GOODIX_GW9578`, `GOODIX_GW9598`, `GF3956`, `GF3976`, `GF3986`, `goodix_tee`, `goodix_opt_tee`, `goodix_fp`.
- `vendor/lib64/libtran_hal.so`: `T_GF3976`, `GF3976`, `GF3986`, `GF3656`, `GW9578`, `GW9598`, `T_GSL7002`, `T_FPC1561`, `T_GSL6195`, `T_JV0307`, `T_FT9391`, `T_FT9399`.
- `vendor_boot/ramdisk/lib/modules/tran_fp_v2.ko`: `GF3656`, `GF3976`, `GF3986`, `GW9578`, `GW9598`.

**[I] Best part-number candidate remains GF3976** (repeated as the primary Goodix type in both HAL/driver tables), but GF3956 / GF3986 / GF3656 TAs are shipped for multi-SKU compatibility. The FDT gives only a generic `goodix;` flag; the silicon ID is read at runtime (`tran_read_fp_chip_id`, HAL `getVendorChipId`). Static evidence cannot raise this above "probable".

---

## 2. Kernel modules — exhaustive evidence for "no protocol constants"

### 2.1 `modinfo` (both modules) — VERIFIED
```
tran_fp.ko:    name=tran_fp     depends=transsion_tranlog,mtk_disp_notify,spi-mt65xx
               alias=of:N*T*Ctran_fp_legacy  parm=bufsiz (data bytes in biggest SPI message)
               vermagic=6.12.38-android16-5-...-4k SMP preempt mod_unload modversions aarch64
tran_fp_v2.ko: name=tran_fp_v2  depends=mtk_disp_notify,mcDrvModule,spi-mt65xx
               alias=of:N*T*Ctran_fp   (same vermagic / parm)
```
Both are plain uncompressed AArch64 ELF relocatables, `file` = "ELF 64-bit LSB relocatable, ARM aarch64, not stripped". Only `tran_fp.ko` has a matching OF node.

### 2.2 Exhaustive string scan — VERIFIED
Commands: `strings -a -t x -n 3 <ko>` (1845 lines for `tran_fp.ko`, 1049 for `tran_fp_v2.ko`), then filtered for register/opcode/chip/buffer/command patterns. Complete result of the hex/constant filter:
```
  fc04  3[Tran_FP][Error][%s][%d]sw90xx SPI transfer failed
 10dfd  3[Tran_FP][Error][%s][%d]8205 sw9006 SPI transfer failed
 11190  3[Tran_FP][Error][%s][%d]sw9006 SPI transfer failed
 114d0  3[Tran_FP][Error][%s][%d][FP_ANC] jv0307 call, sensor_chip_id:%x, is not 0x46544301
 11a03  3[Tran_FP][Error][%s][%d]sw9022 SPI transfer failed
```
The **only** chip-ID magic constant is `0x46544301` = ASCII `"FTC\x01"` — a **Focaltech** ID used by the ANC/Focaltech handler, not Goodix. `sw9006/sw9022/sw90xx/8205` are Sunwave sensor names. Goodix strings are only part names, vendor flags and command/transport symbols. There is **no** `0x80xx`-style register address, no Goodix command byte string, no `/dev/goodix_fp` register/sysfs node beyond the misc device, and no power-on register table in either module.

### 2.3 Symbol inventory (`readelf -sW`) — VERIFIED
Goodix-side functions in `tran_fp.ko`: `gf_spi_transfer` (0x701c, 160 B), `gf_spi_read_bytes_ree` (0x70bc, 156 B), `gf_fp_irq_init`, `gf_fb_notifier_callback`, `gf_irq`, `gf_proc_init/deinit`, `gf_sysfs_entries`, `gf_debug_show/store`, `g_fp_vendor_id`.
Chip-ID functions: `tran_get_fp_sensor_chip_id` (0x45c8, 3276 B), `tran_read_fp_chip_id` (0x5298, 2356 B). Transport: `tran_fp_spi_transfer` (0x5bd0), `tran_spi_transfer_p_arr`, `spi_sync`.
Note: `slf_write_reg` (0x9f58, 652 B) exists — the driver *does* contain a literal register-write helper, but its prefix is **`slf_` (Silead)**, not `gf_`. The Goodix branch has no equivalent. This contrast is direct evidence that the Goodix path is a pure SPI pipe.

### 2.4 Mechanical immediate decode of the code — **[M]** (not semantic proof)
Host has no AArch64 disassembler (`objdump` = x86_64 only; `llvm-objdump` absent; `capstone` absent), so I decoded MOVZ/MOVK/MOVN/bitmask/ADD/SUB/CMP/LDR-literal immediates directly from `.text` bytes (`.text` file offset `0x3000`; symbol `st_value` added). Results:

- `tran_get_fp_sensor_chip_id`: builds the **vendor name strings inline** from immediates — decoded 16-bit chunks reconstruct `GOODIX`, `CHIPONE`, `FOCALTECH`, `SILEAD`, `CDFINGER`, `FORTSENSE`, and `GF…`/`GW…` part strings. Also small indices (0x336, 0x341, 0x33b, 0x34a), a repeat count `0x11` (17) and timeouts `0x2710` (10000), `0x3e8` (1000), `0x7d0` (2000), `0x1388` (5000).
- `tran_read_fp_chip_id`: a **chip-ID comparison table** — `MOVZ` immediates `0x9362`, `0x9391`, `0x9395`, `0x9396`, `0x9399`, `0x95a8`, plus `0x4132`, `0x4232`, `0x4536`, `0x141`, `0xe4`, `0xb5`, `0x494`; many `CMP` immediates are ASCII byte codes (`0x46 'F'`, `0x54 'T'`, `0x43 'C'`, `0x73 's'`, `0x61 'a'`, …). **[I]** These are compared ID/string constants (e.g. `0x9391` ↔ `FT9391`, `0x4132`/`0x4232` ↔ ASCII `A2`/`B2`), not register addresses.
- `gf_spi_transfer`: only `0x2710` (10000, twice — timeout), no command bytes.
- `gf_spi_read_bytes_ree`: only `0xf0` (240), `0x50` (80), `0xf1` (241), `3`, `5`. **[I]** transfer-length/count constants, not SPI opcodes.
- `slf_write_reg` (Silead only): `0xff`, `0x98`, `0x9b`, `0x4b40` — plausible register-address-shaped constants, but on the Silead branch only.

**Conclusion:** no mechanical constant in any Goodix-path function has the shape or context of a register address or SPI opcode. The driver's job ends at `spi_sync()` on caller-supplied buffers.

---

## 3. MobiCore TA headers — VERIFIED

Both registries contain only MCLF containers (`4d 43 4c 46` = "MCLF"). Full 512-byte xxd was taken for the four principal TAs; the common layout is:

```
0x00  "MCLF"
0x04  05 00      format/version word 0x0005
0x06  02 00      word 0x0002
0x08  3c 00      0x3c = 60-byte header (0x38 = 56 on some generic TAs)
0x0c  02 00 / 0x10 03 00 / 0x14 01 00
0x18  16-byte TA UUID  (matches filename exactly)
0x28  00 10 00 00     0x00001000 (4096) — recurring MCLF constant
0x2c..0x47  little-endian size/offset fields, e.g. for GF3976:
            0x002343e0, 0x00237000, 0x0041dde0, 0x000760a8, 0x000010d0
0x80  a second MCLF-like sub-header (05 00 02 00 3c 00 …) -> nested container
```

**[V] ASCII part names do NOT appear in the headers.** UUID bytes 2–3 encode the chip ID numerically, e.g. GF3976 → `05 06 39 76` (0x3976), GF3956 → `05 06 39 56` (0x3956), GF3986 → `05 06 39 86` (0x3986). Any claim that "GF3976 appears as a string in the TA header" would be false.

**[V] Full UUID inventory of `/vendor/app/mcRegistry/`** (each file's bytes @0x18 equal its filename; all magic MCLF). Mapping to part names comes from `libtran_hal.so` chip table (§1):

| UUID prefix | Part (from HAL table) | File / size |
|---|---|---|
| `05060307…` | JV0307 (CTFJV0307; cf. kernel `jv0307`) | `.tabin` 7,353,275 B |
| `05061561…` | FPC1561 | `.tabin` 1,473,195 B |
| `05063656…` | GF3656 | `.tabin` 5,954,123 B |
| `05063956…` | GF3956 | `.tabin` 5,740,443 B |
| `05063976…` | **GF3976** | `.tabin` 6,628,299 B |
| `05063986…` | GF3986 | `.tabin` 6,720,835 B |
| `05066195…` | GSL6195 | `.tabin` 1,634,027 B |
| `05067002…` | GSL7002 | `.tabin` 4,364,891 B |
| `05069391…` | FT9391 | `.tabin` 1,754,947 B |
| `05069399…` | FT9399 | `.tabin` 1,759,123 B |
| `05060000…` | generic Goodix blob | `.tabin` 12,090,819 B |

**[V] `/odm/app/mcRegistry/` contains only two files** — `05060000000000000000000000000000.tabin` (12,142,443 B, header size 0x38) and `05070000000000000000000000000000.drbin` (5,667 B). This is the registry path that `odm/lib64/libgf_hal.so` explicitly sets via `TEEC_TT_SetLocalRegistryPath` (`[%s] add open-session ta path /odm/app/mcRegistry`).

**[V] Plaintext build strings recovered from the small utility TAs** (the only readable non-MCLF metadata in any TA):
```
odm/app/mcRegistry/05070000000000000000000000000000.drbin:
  [<t Driver DrGoodixUtils] %d.%d, Build Apr 28 2026, 11:20:21
vendor/app/mcRegistry/05070000000000000000000000000000.drbin:
  [<t Driver DrTranssionUtils] %d.%d, Build Oct  2 2026, 18:04:21
```
The ODM drbin is Goodix-specific — independent confirmation that the ODM registry is the Goodix stack's registry. All large `*3956/3976/3986*.tabin` payloads yield only apparent AArch64 machine code/garbage with `strings`; no version or opcode table is plaintext.

**[I] Which TA is actually used:** active driver `tran_fp.ko` + `/dev/goodix_fp` + `gf_fingerprint.default.so` + `libgf_hal.so` + ODM registry path ⇒ the live TA is the **generic ODM `05060000….tabin`**. The chip-specific GF3976 TA in `/vendor/app/mcRegistry` belongs to the inactive `tran_fp_v2` / `libtran_hal.so` stack. This is a strong inference from live module refcounts and the HAL's hardcoded registry path, not absolute proof (a live TA-open log would settle it).

---

## 4. HAL-side command surface — VERIFIED (but no matcher)

`odm/lib64/libgf_hal.so` (580,296 B) plain strings:
```
GF_CMD_SENSOR_INIT / CAPTURE_IMG / DETECT_SENSOR / PRE_DETECT / DATA / GET_IRQ_TYPE / SLEEP / WAKEUP
GF_CMD_AUTH_* / GF_CMD_ALGO_* (AUTHENTICATE, ENROLL, INIT, POST_*)
CMD_TEST_SZ_* (FT_*_CAPTURE_*, SET_HBM_MODE, DUMP_TEMPLATE, ... factory test enums)
GF_ERROR_CHIP_ID_NOT_CORRECT, GF_ERROR_FARGO_CHIP_ID_NOT_CORRECT,
GF_ERROR_SPI_RAW_DATA_CRC_FAILED, GF_ERROR_SPI_FW_DOWNLOAD_FAILED,
GF_ERROR_SPI_CFG_DOWNLOAD_FAILED, GF_ERROR_INIT_ALGO_REGISTER_INFO,
GF_ERROR_INVALID_OTP_VERSION, GF_ERROR_INVALID_PREPROCESS_VERSION
GdxGetWBVersionNum ; [%s] Version = %08x
TEEC_InitializeContext / OpenSession / InvokeCommand / AllocateSharedMemory / … / TEEC_TT_SetLocalRegistryPath
/dev/goodix_fp ; /odm/app/mcRegistry ; persist.vendor.fp.sensorUid ; /mnt/vendor/persist/fpdata/
```
`odm/lib64/hw/gf_fingerprint.default.so` (216,816 B): `Goodix Fingerprint HAL`, `Goodix`, `goodixExtCmd`, C++ symbols `goodix::FingerprintCore::{authenticate,enroll,...}`, `goodix::Sensor`, `goodix::ProductTest`, `/dev/goodix_fp`, `GF_IOC_*` (per `fingerprint.md`).

**[V]** All of these are high-level TEE command enums / error codes sent via `TEEC_InvokeCommand` on shared memory. No image-processing primitive, feature extractor, template structure, or SPI register write exists in either HAL. `GF_ERROR_FARGO_CHIP_ID_NOT_CORRECT` and the `CMD_TEST_SZ_FT_*` factory enums are the only additional vocabulary, and they name operations, not implementations.

---

## 4b. TEE-side trustlets — partially readable, closes part of the gap (VERIFIED)

The dump-wide binary grep surfaced two mcRegistry files that were not in the first pass. They change the picture: the kernel is **not** the only SPI path — the secure world has its own SPI driver and chip-ID trustlet.

**[V] `vendor/etc/init/trustonic.rc`** — MobiCore daemon `mcDriverDaemon` command file, with an explicit t-Drv name map:
```
-r /vendor/app/mcRegistry/05070000000000000000000000000000.drbin     ...
# t-Drv naming
# 05070000000000000000000000000000.drbin : goodix_fp
# 030b0000000000000000000000000000.drbin : spi
# 050d0000000000000000000000000000.drbin : drspi_fp
```
Of the ~28 `*.drbin` files referenced, only 6 are actually present in the dump (`020f…`, `051200…0001`, `050700…`, `50201701…`, `050d…`, `072100…`). The generic TEE SPI driver `030b…drbin` is **missing**, so the secure-world SPI implementation is only partially recoverable.

**[V] `vendor/app/mcRegistry/05090000000000000000000000000000.tabin`** (23,051 B, MCLF) — `tran_little_ta: Build %s %s` (18:04:47, Oct 2 2026). Unlike the large encrypted TAs, its strings are **plaintext**, including named handlers and a concrete op sequence:
```
CMD_TRAN_READ_CHIPID enter!!!
tran_read_chipid / tran_read_goodix_chipid / tran_read_silead_chipid /
tran_read_fpc_chipid / tran_read_focaltech_chipid
tran_spi_write / tran_spi_read / drSpiSend / spi_read_image
tran_fp goodix IC name is GF3976 / GF3656 / GF3986 / GW9578 / GW9598
tran_fp fpc IC name is fpc1561 ; GSL IC name is GSL6195 / GSL7002
[tran_spi][%s] chip ID read successfully: 0x%08X
[tran_spi][%s] pcfg->addr = %04x ,pcfg->val= %04x, rxbuf = %04x
```
It contains one **explicit register-level sequence**, but only for `jv0307` (CTFJV0307 / Focaltech):
```
fail to write 0x08
wait 5ms
fail to write 0x13 0xfd 0x00 0xc9
fail to write 0x13 0x20 0x00 0x9a
wait 3ms
fail to write 0x13 0x6f 0x%x 0x%x
fail to write/read 0x15 0x02 0xcf
fail to write/read 0x15 0x03 0xc8
fail to write/read 0x15 0x04 0xdd
fail to write/read 0x15 0x05 0xda
sensor_chip_id:%x, value.a:%x
```
**[I]** This is a real write-with-delay init/chip-ID sequence at the TEE level. It is the first concrete register/opcode evidence in the whole investigation — but it is vendor-specific to JV0307 (matching the kernel string `jv0307 call, sensor_chip_id:%x, is not 0x46544301`). **No equivalent byte sequence exists for GF3976/GF3956/GF3986** — the Goodix handler is named but its bytes are not exposed in plaintext.

**[V] `vendor/app/mcRegistry/050d0000000000000000000000000000.drbin`** — `[SPI][<t Driver Drspi] %d.%d, Build Oct 2 2026, 18:04:21`, a **secure-world SPI driver**. Plaintext strings name MediaTek SPI controller registers (not sensor registers):
```
SPI_REG_CFG0 / CFG1 / CFG2 / TX_SRC / RX_DST / TX_SRC_64 / RX_DST_64 / CMD / PAD_SEL
SpiMapRegister / drApiMapPhysicalBuffer / spi base addr: 0x%0x
Setup packet length, header_len, packet_size, packet_loop
CMD_SPI_DRV_SEND / FID_DR_SPI_INIT / FID_DR_EXECUTE / FID_DR_GETONEIMG_EXECUTE
spi_read_image / spi_transfer timeout / wait IRQ handle finish
```
**[I]** Matching/enrolment is still entirely inside the encrypted large TAs; this driver only moves bytes. It does provide the TEE-side register names for the MT6789 SPI block, which is useful for an XNU port of the transport, but it contains no Goodix sensor command bytes.

**[V] `odm/bin/fhpd`** (51,872 B, stripped ARM64 PIE) is a userspace FHP (fingerprint/palm/touch) status daemon, not a sensor driver: `FHP Status:`, `FOD Status : %u`, `ioctl(FHP_INPUT_IOCTL_REPORT_FOD/PALM/TOUCH/GET_CFG/SET_ALGO_VERSION)`. No sensor commands.

---

## 5. Portable matcher availability — VERIFIED

Recursive filename search over the complete dump for `*fprint*`, `*bozorth*`, `*finger*print*`, `*goodix*`, `*gf*_fp*` returned only Android/Transsion artefacts:
```
system_ext/framework/fingerprintservice.jar{,.odex,.vdex,.fsv_meta}
vendor/lib64/android.hardware.biometrics.fingerprint-V3-ndk.so
vendor/lib64/vendor.hardware.biometrics.extension.fingerprint-V1-ndk.so
vendor/lib64/vendor.transsion.hardware.biometrics.fingerprint-V1-ndk.so
vendor/bin/hw/android.hardware.biometrics.fingerprint-service
odm/lib64/libvendor.transsion.hardware.biometrics.fingerprint.so
odm/lib64/hw/gf_fingerprint.default.so ; vendor/lib64/hw/tran_fingerprint.default.so
odm/etc/init/vendor.goodix.rc ; odm/etc/init/fingerprint-default.rc
odm/etc/vintf/manifest/fingerprint-default.xml ; odm/etc/permissions/android.hardware.fingerprint.xml
```
**[V] No libfprint, no BOZORTH3, no Goodix/open-source matcher source, no reusable algorithm binary anywhere in the dump.** The only matcher is the encrypted TA.

---

## 6. Live kernel log — VERIFIED (negative result)

`devices/x6886/evidence/dmesg.txt` is 28,854 lines but is a **rolling tail starting at `[31277.110124]` (~8.7 h uptime)**, not a boot log. Case-insensitive counts:
```
spi: 0   SPI: 0   fp: 0   FP: 0   finger: 0   goodix: 0   fp_spi: 0   mcDrv: 0   tee: 0
tran: 335  (unrelated Transsion drivers)
```
**[V]** No fingerprint init, chip-ID, SPI-transfer, TEE or MobiCore line is present in the captured window. Absence here is expected and carries no information about the sensor. A fresh boot log (`dmesg` immediately after `insmod`/HAL start) would be needed.

---

## 7. What remains genuinely impossible (precise boundary)

**Impossible with only this dump — requires the TA decrypt key or a live capture:**
1. **Goodix command/opcode table** — the actual bytes a GF39xx sensor accepts (init, capture, read-image, CRC/config download). Constructed inside the encrypted TA. (The one readable sequence in `05090000.tabin` is JV0307-specific, §4b.)
2. **Goodix register addresses** — no `0x80xx`-style Goodix register map appears in any string, symbol, header, or decoded immediate. `050d0000.drbin` names only the **MT6789 SPI controller** registers (`SPI_REG_CFG0…CMD/PAD_SEL`), not sensor registers; `05090000.tabin` exposes register writes only for JV0307 (`0x08`, `0x13 …`, `0x15 …`). Any external "typical Goodix GF3xxx command set" would be **general knowledge, not evidence from this dump**; none is asserted here.
3. **Power-on / init sequence** — GPIO power/reset timing and clock gating exist as *symbol names* (`tran_fp_power_onoff`, `tran_fp_reset_sensor`, `mt_spi_enable_master_clk`), but the register writes that configure the sensor are in the TA.
4. **Capture parameters** — exposure, gain, HBM timing values, image dimensions/format handed to the algorithm.
5. **Matcher + template format** — enrolment/matching algorithm, feature representation, template encryption, and the keys protecting `/mnt/vendor/persist/fpdata/` and `/data/vendor/goodix/gf_data`. Existing enrolled fingerprints cannot be migrated to XNU.
6. **TA execution on XNU** — MobiCore/TrustZone client, TA validation inside the secure world, and `ro.vendor.tran.anti.crack.p7.support=1` / `ta.crashdebug.support=1` trust chain. Disabling vbmeta does not bypass this; TAs are validated in the TEE, not by AVB.
7. **Exact part number** — GF3976 is probable, not proven; needs a live `getVendorChipId` / chip-ID read result.

**Not impossible, but not completed here (tooling limitation):**
- Numeric `_IOC` codes and ioctl struct layouts for `/dev/goodix_fp` and `/dev/tran_fp` are recoverable by disassembling `tran_fp_ioctl` / `tran_fp_v2_ioctl` (both modules are unstripped). The host has no AArch64 disassembler, so only symbolic names were extracted. This is a solvable gap, not a fundamental one.
- The exact power/clock GPIO sequence can be reconstructed by disassembly of `tran_fp_power_init` / `tran_fp_power_onoff` plus the FDT pinctrl states already captured.

---

## 8. Evidence index (exact paths)

```
/home/berucrypt00/otadump/vendor_boot/ramdisk/lib/modules/tran_fp.ko
/home/berucrypt00/otadump/vendor_boot/ramdisk/lib/modules/tran_fp_v2.ko
/home/berucrypt00/otadump/odm/app/mcRegistry/05060000000000000000000000000000.tabin
/home/berucrypt00/otadump/odm/app/mcRegistry/05070000000000000000000000000000.drbin
/home/berucrypt00/otadump/vendor/app/mcRegistry/05063956000000000000000000000000.tabin   (GF3956)
/home/berucrypt00/otadump/vendor/app/mcRegistry/05063976000000000000000000000000.tabin   (GF3976)
/home/berucrypt00/otadump/vendor/app/mcRegistry/05063986000000000000000000000000.tabin   (GF3986)
/home/berucrypt00/otadump/vendor/app/mcRegistry/05060307000000000000000000000000.tabin   (JV0307)
/home/berucrypt00/otadump/vendor/app/mcRegistry/05061561000000000000000000000000.tabin   (FPC1561)
/home/berucrypt00/otadump/vendor/app/mcRegistry/05066195000000000000000000000000.tabin   (GSL6195)
/home/berucrypt00/otadump/vendor/app/mcRegistry/05067002000000000000000000000000.tabin   (GSL7002)
/home/berucrypt00/otadump/vendor/app/mcRegistry/05069391000000000000000000000000.tabin   (FT9391)
/home/berucrypt00/otadump/vendor/app/mcRegistry/05069399000000000000000000000000.tabin   (FT9399)
/home/berucrypt00/otadump/vendor/app/mcRegistry/05070000000000000000000000000000.drbin    (goodix_fp per trustonic.rc)
/home/berucrypt00/otadump/vendor/app/mcRegistry/05090000000000000000000000000000.tabin    (tran_little_ta: CMD_TRAN_READ_CHIPID, JV0307 writes)
/home/berucrypt00/otadump/vendor/app/mcRegistry/050d0000000000000000000000000000.drbin    (drspi_fp: secure-world SPI driver)
/home/berucrypt00/otadump/vendor/etc/init/trustonic.rc  (t-Drv name map: 05070000=goodix_fp, 050d0000=drspi_fp, 030b0000=spi)
/home/berucrypt00/otadump/odm/bin/fhpd              (FHP status daemon, no sensor commands)
/home/berucrypt00/otadump/vendor/bin/factory         (factory AT+ test harness, no sensor opcodes)
/home/berucrypt00/otadump/odm/lib64/libgf_hal.so
/home/berucrypt00/otadump/odm/lib64/hw/gf_fingerprint.default.so
/home/berucrypt00/otadump/vendor/lib64/libtran_hal.so
/home/berucrypt00/otadump/vendor/lib64/hw/tran_fingerprint.default.so
/home/berucrypt00/otadump/odm/etc/init/vendor.goodix.rc
/home/berucrypt00/otadump/product/etc/build.prop  (ro.fingerprint_support=1, wakeup opt)
/home/berucrypt00/otadump/tr_product/etc/build.prop (ro.tr_fingerprint.optical.feature.support=1)
/home/berucrypt00/hakios-x6886/devices/x6886/evidence/dmesg.txt        (rolling tail, no FP lines)
/home/berucrypt00/hakios-x6886/devices/x6886/evidence/getprop.txt      (goodix SPMT PASS, fp props)
/home/berucrypt00/hakios-x6886/devices/x6886/evidence/modules.txt      (tran_fp refcount 2, v2 refcount 0)
/home/berucrypt00/hakios-x6886/devices/x6886/evidence/HARDWARE-IDENTITY.md (lines 75-119)
/home/berucrypt00/hakios-x6886/devices/x6886/evidence/fdt.dts          (lines 4558-4591, 9188)
```
