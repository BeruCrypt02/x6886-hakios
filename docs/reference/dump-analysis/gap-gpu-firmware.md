# GPU / Mali firmware gap — X6886 (MT6789, Mali-G57 MC2 Valhall r54p1)

**Analysis date:** 2026-11-23
**Mode:** READ-ONLY on `/home/berucrypt00/otadump`. No phone access, no writes to the dump, no driver code written.
**Sources used (only):** `/home/berucrypt00/otadump`, `/home/berucrypt00/hakios-x6886/devices/x6886/evidence/{dmesg.txt,modules.txt,getprop.txt}`.
**Tools:** `find`, `grep`, `strings -a -t x`, `xxd`, `dd`, `readelf -S`, `file`, `python3` (byte/struct inspection).

---

## VERDICT

**RESOLVED LOCALLY** — the exact `request_firmware` names are recovered from the dump itself. No live device is needed to answer "what is the real name".

The driver requests **two** firmware files:

| # | Filename | Full on-device path (from SELinux policy in the dump) | Evidence |
|---|---|---|---|
| 1 | `valhall-1691526.wa` | `/vendor/firmware/valhall-1691526.wa` | literal string in the Mali `.ko` **and** explicit SELinux file context |
| 2 | `mali_csffw.bin` | `/vendor/firmware/mali_csffw.bin` | explicit SELinux file context; the earlier `sffw.bin` hit is its tail |

**Important secondary result:** neither payload file exists in this dump. The `sffw.bin` bytes in `vendor_boot.img` are **not** a firmware blob — they are compressed literal fragments of `vendor_file_contexts` (proof in §4). The XNU bring-up must therefore source the two blobs from stock firmware, not from a live probe of the name.

---

## 1. The driver and its exact strings

**Module** (VERIFIED): `/home/berucrypt00/otadump/vendor_dlkm/lib/modules/mali_kbase_mt6789_a16w_jm.ko`
size `1,605,128` bytes; sibling modules `mali_mgm_mt6789_a16w_jm.ko`, `mali_prot_alloc_mt6789_a16w_jm.ko` contain **no** firmware-name strings.

`strings -a -t x` on the `.ko` gives (offsets are file offsets):

```
  92458 power
  9245e valhall-1691526.wa
  9a105 KBASE_TL_KBASE_CSFFW_FW_RELOADING
  9a14f csffw_cycle
  9a163 KBASE_TL_KBASE_CSFFW_FW_ENABLING
  9a1bf KBASE_TL_KBASE_CSFFW_FW_REQUEST_SLEEP
  9a222 KBASE_TL_KBASE_CSFFW_FW_REQUEST_WAKEUP
  9a288 KBASE_TL_KBASE_CSFFW_FW_REQUEST_HALT
  9a2e9 KBASE_TL_KBASE_CSFFW_FW_DISABLING
  9a347 KBASE_TL_KBASE_CSFFW_FW_OFF
  9a3bf An overflow has happened with the CSFFW Timeline stream
  9be9b version=r54p1-12eac0 (UK version 11.46)
  a6a27 WA blob missing. Please refer to the Arm Mali DDK Valhall Release Notes,
        Part number DC-06002 or contact support-mali@arm.com - driver probe will be failed
 180695 kbase_dummy_job_wa_load.wa_name
 181a3a kbase_device_firmware_init_once
       request_firmware
       release_firmware
```

Bytes at the firmware-name string (VERIFIED, `xxd -s 0x923c0 -l 320`):

```
00092450: 0000 0000 0000 0000 706f 7765 7200 7661  ........power.va
00092460: 6c68 616c 6c2d 3136 3931 3532 362e 7761  lhall-1691526.wa
00092470: 0000 0000 0000 0000 0000 0000 0000 0000  ................
```

So `valhall-1691526.wa` is a standalone NUL-terminated `.rodata` string sitting next to `power`, in the same string pool as `kbase_dummy_job_wa_load.wa_name` (the tracepoint field that reports the WA-blob name). Combined with `WA blob missing ... driver probe will be failed`, this is the **WA (workaround) blob** the driver loads with `request_firmware` during `kbase_device_firmware_init_once`.

`CSFFW` is **not** a filename. Every `sffw` hit inside the `.ko` is the trace field `csffw_cycle` / `KBASE_TL_KBASE_CSFFW_*` (e.g. lowercase `sffw` at file offsets `0x9a150`, `0x9a1ac`, `0x9a20f`, `0x9a275`, `0x9a2d6`, all inside `csffw_cycle`). There is **no** `mali_csffw.bin` literal in this `.ko`.

`readelf -S` shows only standard module sections (`.text` `0x6cd70`, `.rodata` `0xd990`, `.rodata.str` `0x225c`, `.rodata.str1.1` `0xdc8c`, etc.) — **no embedded firmware section**, so the blobs are not built into the module.

---

## 2. Decisive filename evidence: SELinux file contexts (VERIFIED)

Three independent copies in the dump agree on both full paths:

`/home/berucrypt00/otadump/vendor/etc/selinux/vendor_file_contexts`
```
1208:/vendor/firmware/mali_csffw.bin     u:object_r:same_process_hal_file:s0
1261:/vendor/firmware/valhall-1691526.wa u:object_r:same_process_hal_file:s0
```

`/home/berucrypt00/otadump/vendor_boot/ramdisk/vendor_file_contexts`
```
1179:/vendor/firmware/mali_csffw.bin     u:object_r:same_process_hal_file:s0
1232:/vendor/firmware/valhall-1691526.wa u:object_r:same_process_hal_file:s0
```

`/home/berucrypt00/otadump/vendor_boot/recovery_ramdisk/vendor_file_contexts`
```
1208:/vendor/firmware/mali_csffw.bin     u:object_r:same_process_hal_file:s0
1261:/vendor/firmware/valhall-1691526.wa u:object_r:same_process_hal_file:s0
```

This is a vendor policy rule with `same_process_hal_file`, i.e. these are real files the GPU process is expected to open from `/vendor/firmware/`. It resolves the earlier "tail of a CSF firmware filename" inference: the tail `sffw.bin` belongs to **`mali_csffw.bin`** (CSF = Command Stream Firmware, matching the `CSFFW_*` trace names).

---

## 3. Exhaustive absence check — payload files are NOT in the dump (VERIFIED)

| Search | Result |
|---|---|
| `find /home/berucrypt00/otadump -iname '*sffw*' -o -iname '*csffw*' -o -iname '*mali*' -o -iname '*.wa'` | only Mali `.ko`/`.so`/`mali_platform.config` — **no firmware file** |
| `vendor/firmware/` listing | 78 files: WiFi, BT, FM, touch (`gt9896s_*`, `novatek_ts_fw*`), NFC, SPU (`SEC_THN31_*`), `firmware_slm_567.bin`, etc. — **no Mali/GPU** |
| `odm/firmware/` listing | 9 files: Awinic, chipsemi/focaltech touch, ffu_bin/, wifi.cfg — **no Mali/GPU** |
| `all_files.txt` firmware entries | 94 entries; unique names contain no `mali`, `gpu`, `sffw`, `csffw`, `valhall`, `.wa` |
| `grep -rinE 'sffw\|csffw\|valhall-1691526\|.wa' --include=*.rc/*.cfg/*.prop/*.txt/*.json/*.xml/*.conf/*.sh` over the dump | only the SELinux contexts in §2 (plus unrelated `wait`/`wakeup` matches) |
| `/home/berucrypt00/otadump/vendor_boot/ramdisk/` (463 extracted files) | **zero** `*.bin`, **zero** `*.wa`, no `firmware/` directory |
| `/home/berucrypt00/otadump/images/*.img` scanned with `grep -aob` for `sffw`, `csffw`, `valhall-1691526`, `r54p1` | only `vendor_boot.img` and `vendor_boot-debug.img` (see §4) |
| **whole-dump** `grep -ril 'sffw' /home/berucrypt00/otadump/` (recursive, binary-as-text) | relevant hits only: `vendor/etc/selinux/vendor_file_contexts`, `vendor_boot/ramdisk/vendor_file_contexts`, `vendor_boot/recovery_ramdisk/vendor_file_contexts`, `vendor_dlkm/lib/modules/mali_kbase_mt6789_a16w_jm.ko`. All other hits (~40) are coincidental byte substrings in APKs, `.git` LFS/pack objects, `.mdla3_0` model files, launcher JSON, zips and unrelated binaries — no GPU firmware file among them. |
| `grep -inE 'mali_csffw\|valhall-1691526\|csffw' all_files.txt` | **zero** manifest entries — neither name is an OTA payload file |
| `grep -ral 'mali_csffw.bin'` over `vendor/ vendor_dlkm/ odm/ odm_dlkm/ system/ system_ext/ product/ vendor_boot/` | only the **3** `vendor_file_contexts` copies; no firmware file |
| `grep -ral 'valhall-1691526.wa'` over the same dirs | the **3** `vendor_file_contexts` copies **+** `vendor_dlkm/lib/modules/mali_kbase_mt6789_a16w_jm.ko`; no firmware file |

Captured live data cross-check: `/home/berucrypt00/hakios-x6886/devices/x6886/evidence/modules.txt` confirms the module is loaded on the device (`mali_kbase_mt6789_a16w_jm 991232 74 - Live ... (OE)`), but `dmesg.txt` is a **late tail starting at `31277.110124`** with **zero** `mali`/`gpu` lines, so it contains no boot-time `request_firmware` failure line. `getprop.txt` only has generic GPU props (`[ro.hardware.vulkan]: [mali]`, etc.).

---

## 4. The vendor_boot `sffw.bin` bytes are compressed filename text, not a blob (VERIFIED)

`/home/berucrypt00/otadump/images/vendor_boot.img` — size `67,108,864`; header at `0x0` is `56 4e 44 52 42 4f 4f 54` = `VNDRBOOT`, version `4`, page size `4096`; the ramdisk region starting at `0x1000` is detected by `file` as `LZ4 compressed data (v0.1-v0.9)`.

Reported offsets, extracted with `dd bs=1 skip=<off> count=512 | xxd`:

**Offset 29,682,356 (`0x1c4eab4`)**
```
00000000: 7366 6677 2e62 696e 8082 1709 5d43 0f02  sffw.bin....]C..
00000010: 4b03 0e3e 0a0f fd4a 0c01 6a62 00df 0301  K..>...J..jb....
00000020: d212 0f96 6205 3f64 6462 934d 070a ce04  ....b.?ddb.M....
...
```

**Offset 32,891,762 (`0x1f5e372`)**
```
00000000: 7366 6677 2e62 696e 2b86 1709 8d45 0fa4  sffw.bin+....E..
00000010: 4d03 0e3e 0a0f 9f4d 0c01 e665 00df 0301  M..>...M...e....
...
```

Exactly **+800 bytes** later in both cases:

**29,683,156 (`sffw.bin` + 800)**
```
00000000: 7661 6c68 616c 6c2d 3136 3931 3532 362e  valhall-1691526.
00000010: 7761 4800 1200 1402 004c 1720 756c fd0f  waH......L. ul..
```

**32,892,553 (`sffw.bin` + 791)**
```
00000000: 7661 6c68 616c 6c2d 3136 3931 3532 362e  valhall-1691526.
00000010: 7761 4800 1200 1402 004c 1720 756cfd0f   waH......L. ul..
```

Same pattern in `images/vendor_boot-debug.img`: `sffw` at `29,940,692` and `31,133,919`, `valhall-1691526` at `29,941,492` and `31,134,710`.

**Why this is not a firmware payload (VERIFIED + INFERENCE):**

- At each offset the ASCII `sffw.bin` is the tail of `mali_csffw.bin`, and `valhall-1691526.wa` is the other filename from the same policy file. There is **no** header at any of these offsets: no gzip (`1f 8b`), zstd (`28 b5 2f fd`), xz (`fd 37 7a 58`), LZ4 frame (`04 22 4d 18`), ELF (`7f 45 4c 46`), or Mali/RO_ROM magic. The surrounding bytes are high-entropy LZ4 match/token data.
- The **only** file in the extracted ramdisk containing `mali_csffw` or `valhall-1691526` is `/home/berucrypt00/otadump/vendor_boot/ramdisk/vendor_file_contexts` — verified with `grep -ral` over all 463 files. In that plaintext file the names sit at byte offsets `105,573` and `109,749` (delta `4,176` bytes); in the LZ4 stream they are `800`/`791` bytes apart, exactly what compression of repetitive SELinux context lines produces.
- Neighbouring compressed literals from the same region (`adb_keys`, `/product/etc/security/`, `/system/`, `m_vpu[1-3]`, `tcpc/`, `haptic`, `wakeu`) are also all `vendor_file_contexts` content.
- The extracted ramdisk has **no** `*.bin`/`*.wa` files at all.

CONCLUSION (INFERENCE, high confidence): the two hits are compressed string literals from `vendor_file_contexts`, not a GPU blob. No GPU firmware payload exists anywhere in `/home/berucrypt00/otadump`.

---

## 5. Request semantics

- `valhall-1691526.wa` — requested by `mali_kbase_mt6789_a16w_jm.ko` via `request_firmware` inside `kbase_device_firmware_init_once`; the driver's own failure text says a missing WA blob fails probe (`WA blob missing ... driver probe will be failed`). Default firmware search path resolves the bare name to `/vendor/firmware/valhall-1691526.wa`, which is exactly the path in the SELinux rule.
- `mali_csffw.bin` — CSF (Command Stream Firmware) blob at `/vendor/firmware/mali_csffw.bin`, also with an explicit SELinux rule. No binary in the dump contains the literal, so its name is likely assembled at runtime (prefix `mali_` + `csffw` + `.bin`) or requested by a userspace GPU component not present as a plain string in this dump. The policy path is nevertheless unambiguous.

---

## 6. Last resort only — live verification command (NOT executed)

The name is already resolved locally. If Beru ever wants to confirm payload presence/size on a live device:

```sh
adb shell 'ls -l /vendor/firmware/mali_csffw.bin /vendor/firmware/valhall-1691526.wa; \
  dmesg | grep -iE "mali|csffw|valhall|request_firmware"; \
  grep -iE "mali_csffw|valhall" /vendor/etc/selinux/vendor_file_contexts'
```

If a boot log must show the exact `request_firmware` name:

```sh
adb shell 'dmesg -w | grep -iE "mali|firmware|csffw|valhall"'
# then reboot; or, as root:
adb shell 'strace -f -e trace=openat -s 128 -p $(pidof <gpu-process>) 2>&1 | grep -i firmware'
```

---

## 7. Update to the existing analysis

`/home/berucrypt00/hakios-x6886/docs/reference/dump-analysis/firmware.md` §2 and `FINDINGS.md` ("GPU: no firmware blob in dump") remain **correct about the blob's absence**, but the earlier inference that the name needed live confirmation is now superseded: the two names are `mali_csffw.bin` and `valhall-1691526.wa`, both recorded in the dump's own SELinux policy and (for the `.wa`) in the driver binary.
