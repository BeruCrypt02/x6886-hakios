# Stock LK + U-Boot in boot_b — feasibility (Infinix X6886 / MT6789)

Analysis only. No device access. No writes to `/home/berucrypt00/otadump` or `device-dump`.
Evidence is read from the dump, the live-data text files, and the reference port doc.
Skill route: `aosp-root-router` → `aosp-bootloader-lk`.

## Verdict

**BLOCKED for the plan as stated.** The mechanical part is FEASIBLE — stock LK will parse a
properly built Android boot image and jump to its "kernel" payload, and it can decompress LZ4
legacy or gzip. Three independent gates stop an unsigned U-Boot image in `boot_b` from running
on this device as-is:

1. **AVB covers `boot`.** Device is locked, `verifiedbootstate=green`, `veritymode=enforcing`.
   Main vbmeta chains to `boot`; `boot.img` carries its own embedded vbmeta + AVB footer.
   A modified `boot_b` cannot pass verification without the OEM signing key.
2. **Slot stays A.** Writing `boot_b` does not change the active slot. Live device reports
   `ro.boot.slot_suffix = [_a]`. Switching requires `fastboot set_active b` (not executed,
   needs explicit approval).
3. **PayJoy fastboot gate.** LK contains an explicit `fastboot disabled by payjoy` path and the
   device exposes `ro.vendor.tran.anti.crack.p7.support=1`. Whether the flag is set on this unit
   is not determinable from the dump.

If those gates are resolved (unlock with approval, or an authorised resigning path, plus an
approved slot switch), the format path below is workable.

---

## 1. Boot images in the dump — VERIFIED

Files: `/home/berucrypt00/otadump/images/boot.img` (67,108,864 B = 0x4000000),
`vendor_boot.img` (0x4000000), `vbmeta.img` (12,288 B), plus
`/home/berucrypt00/otadump/boot/kernel` (18,885,902 B) and `boot/header_info.json`.
There is **no `boot_a` / `boot_b` image file** in the dump; the OTA provides one `boot.img`
(mapped to `boot_a` in the scatter).

`images/boot.img` header raw bytes:

```
00000000: 414e 4452 4f49 4421 0e2d 2001 0000 0000  ANDROID!.- .....
00000010: 0000 0000 3006 0000 0000 0000 0000 0000  ....0............
00000020: 0000 0000 0000 0000 0400 0000 0000 0000  ................
```

| Field | Offset | Value | Meaning |
|---|---|---|---|
| magic | 0x00 | `ANDROID!` | Android boot image |
| kernel_size | 0x08 | `0e 2d 20 01` = **0x01202d0e = 18,885,902** | exactly `boot/kernel` size |
| ramdisk_size | 0x0c | 0 | GKI: generic ramdisk empty |
| os_version | 0x10 | 0 | |
| header_size | 0x14 | 0x630 = **1584** | v4 header size |
| header_version | 0x28 | **4** | **Android boot header v4** |
| cmdline / extra_cmdline | 0x2c / 0x250 | all zero | LK builds cmdline itself |

`boot/header_info.json` (dump-side tool output) agrees:
`header_version: 4`, `page_size: 4096`, `kernel.format: lz4_legacy`, `ramdisk.size: 0`.

`vendor_boot.img` header: magic `VNDRBOOT`, version 4, page size 0x1000, cmdline starts
`bootopt=64S3,32N2,64N2 bootconfig`. `vendor_boot/header_info.json` and extracted
`dtb` (183,850 B), `bootconfig`, `ramdisk/`, `recovery_ramdisk/` exist.

**Kernel payload format — VERIFIED.** `boot/kernel` starts `02 21 4c 18` = LZ4 *legacy* frame
magic 0x184C2102, not gzip (`1f 8b`) and not a bare `ARMd` Image.
`lz4 -d` of that payload (into `/tmp`, read-only source) yields 40,024,576 B with arm64 Image
header `code0 = 0xfa405a4d`, `text_offset = 0x0`, `magic @0x38 = ARMd`. So LK receives an
**LZ4-legacy-compressed 64-bit Linux Image** and decompresses it before jumping.

Layout in the 64 MB partition (VERIFIED by header + footer):
- kernel starts at **0x1000** (byte-for-byte comparison of `boot.img[0x1000:0x1040]` with
  `boot/kernel[0:0x40]` = MATCH).
- kernel occupies 0x1000 .. 0x1000+18,885,902; ramdisk/dtb sizes in the v4 header are 0.
- embedded AVB vbmeta at **0x1208000** (`AVB0` magic there).
- AVB footer `AVBf` at **0x3ffffc0** (64 MB − 64 B), carrying original size 0x1208000 and
  vbmeta offset 0x1208000 (both BE u64).

Scatter (`mtk/MT6789_Android_scatter.txt`): `boot_a` @0x33200000 and `boot_b` @0x55200000,
each `partition_size: 0x4000000` (64 MB). `lk_a` @0x31600000 with `file_name: lk.img`,
`lk_b @0x53600000 file_name: NONE`; `vbmeta_a` has `vbmeta.img`, `vbmeta_b` is `NONE`.

## 2. Stock LK binary — VERIFIED (strings, file offsets in `images/lk.img`)

`images/lk.img` is 3,149,792 B. First bytes `88 16 88 58 78 7c 0e 00 6c 6b 00 00…`
→ MTK wrapper magic 0x58881688, size 0x000e7c78, name `lk`; a second MTK header
`89 16 89 58` at 0x30. String offsets below are offsets in this wrapped file; LK's own
string pool is duplicated (e.g. about 0xa0000–0xc0000 and again 0x180000–0x264000).

### Fastboot command surface
```
  a22da private partition '%s' forbidden to fastboot
  a4651 fastboot_init
  a465f reboot-bootloader
  a4671 current-slot
  a5796 set_active:
  a8935 Failed to unlock the device! Will return to fastboot in 3s
  a89d0 INFO: fastboot disabled by payjoy!!!
  ac136 boot                         <- standalone "boot" command (fastboot boot)
  b0581 boot-fastboot
  b0592 flashing get_unlock_ability
  b0603 oem tran_unlock
  b061b allow_fastboot
  b159b getvar:
  b2b88 set_active_slot
  b39b2 fastboot: %s[len:%d]
  b3a0b download:
  b4568 Device will continue to bootup in %ds
  b495d reboot-fastboot
  b5995 [unknown command]*[%s]*
  b59e5 Prohibit unlock operation
  b5a24 unlock_ability is false
  b7e70 fastboot
  b7e30 fastboot:[download_base:%p][download_size:0x%x]
  b8ccc continue
  be21d unknown command
  bf6fd flash:
  c2c38 fastboot:[cmd:%s]-[arg:%s]
  c3dec fastboot: set state to STATE_RETURN
```
`getvar`, `download:`, `flash:`, `continue`, `reboot`, `boot`, `set_active`, `current-slot`,
`oem tran_unlock`, `flashing get_unlock_ability` are all present. Unknown commands are rejected
(`unknown command`, `[unknown command]*[%s]*`). No custom "boot U-Boot" fastboot command exists;
`fastboot boot` is the ordinary temporary-boot path.

### PayJoy / Transsion gate
```
  a2e4b [tran] tran_tkv_get_payjoy_flag is failed
  a2e76 check_payjoy_flag
  a89d0 INFO: fastboot disabled by payjoy!!!
  ad2c7 is_unlock_allowed = 0x%x
  ae7c5 set slot b failed!
  ad2ad oem tran_skip_confirm_key
  b59e5 Prohibit unlock operation
  c3f47 transsion oem unlock failed
```
Live props: `ro.vendor.tran.anti.crack.p7.support = [1]`, `ro.boot.flash.locked = [1]`,
`ro.oem_unlock_supported = [1]`. No explicit PayJoy-enabled property appears in `getprop.txt`.
**INFERENCE:** if the PayJoy/TKV flag is set, LK refuses fastboot; this cannot be resolved from
the dump and must be checked on the device.

### Boot image loading / kernel handoff
```
  ae5a2 decompress kernel image fail!!!
  ae593f %s:%d: kernel_addr (0x%p) is not taken from mb (0xllx)
  a590e %s:%d: reserved memory mb_kernel does not exist
  af4ad met set_fdt skip if user load with boot state not orange
  b0165 Load '%s' partition to 0x%p (%d bytes in %d ms)
  b0649 %s:%d: init_boot image header loaded
  b1694 %s:%d: lk finished --> jump to linux kernel %s
  b5ade %s:%d: boot image header loaded
  b5b63 %s:%d: kernel_load_addr=0x%08lx, zimage_size=0x%08x
  b5b3f gzip header is not correct. Error!
  b5baa load boot image
  b6df2 lz4 decompress failure
  b6e0a gunzip
  b6e93 64 bit kernel can't boot at 0x%08lx
  b7a2c app/mt_boot/avb/load_vfy_boot.c
  b88ea [AVB] invalid boot image type
  bc2f7 unlz4
  bc33d %s:%d: decompress kernel image...
  c2cc2 decompress_kernel
  c2cd4 gzip decompress failure
```
`unlz4` / `lz4 decompress failure` and `gunzip` / `gzip decompress failure` together prove LK
supports both LZ4 legacy and gzip kernel payloads. Kernel is placed into the `mb_kernel`
reserved memory block from the DT and entered as a 64-bit image.

### AVB / signature verification
```
  a2d5e [AVB] Failed to alloc memory for avb heap, err = 0x%x
  a3050 : Error loading vbmeta data.
  a4445 INVALID_VBMETA_HEADER
  a6294 androidboot.verifiedbootstate=orange
  a6569 external/lib/libavb/avb_slot_verify.c
  a76f7 SIGNATURE_MISMATCH
  a8780 androidboot.vbmeta.device_state
  a9a2c %s Error verifying vbmeta image=%s errorCode=%d
  aacb5 AVBf
  aacbe Hash check failed.
  abbcb dm-verity corruption
  b2584 external/lib/libavb_user/avb_user_verification.c
  b45b4 Red State
  b483b Hash does not match!
  b572f img_auth_required = %d
  b7a2c app/mt_boot/avb/load_vfy_boot.c
  b7a4c androidboot.verifiedbootstate=green
  b89b7 [SEC] verify '%s' (%d bytes in %d ms)
  b9a4f [AVB] Failed to update avb group, ret = 0x%x
```
Full libavb (`avb_slot_verify.c`, `avb_rsa.c`, `avb_vbmeta_image.c`, `avb_hash_descriptor.c`,
`avb_hashtree_descriptor.c`) plus MTK's `app/mt_boot/avb/load_vfy_boot.c` are linked in.
`Red State`, `Hash does not match!`, `SIGNATURE_MISMATCH`, `[AVB] invalid boot image type`
show verification can abort the boot. **INFERENCE:** LK verifies `boot` before the jump; the
exact call site is not disassembled, but the vbmeta chain below makes `boot` an AVB-verified
partition.

### Slot selection
```
  a220f %s:%d: Get BOOTCTRL_NUM_SLOTS fail
  a2233 slot-count
  a223e slot-retry-count:a
  a2251 slot-retry-count:b
  a4a0b %s:%d: [LK_AB] boot_ctrl magic number is match, compare priority %d, %d
  a67a3 slot-unbootable:a
  a6ac9 check_suffix_with_slot
  a75bb boot_a
  a7b97 get_suffix
  aaeff slot-successful:a
  a8ba0 %s:%d: [LK_AB] wrong slot number %d
  ae423 set slot b failed!
  af20a boot_b
  b2b3b %s:%d: [LK_AB] boot_ctrl magic number is not match %x , BOOTCTRL_MAGIC = %x
  b28c0 androidboot.slot_suffix=%s
  b39c8 slot-unbootable:b
  b459a [AVB] Fail to get suffix
  bd197 slot-successful:b
  bf645 set slot a failed!
  c0a1d %s:%d: [LK_AB] open misc fail
```
Slot choice is driven by bootctrl metadata + `misc` (BCB) with priority / tries_remaining /
unbootable, and `set_active_slot` is a separate fastboot operation. Verified live:
`ro.boot.slot_suffix = [_a]`.

## 3. Does LK verify boot_b's kernel? — VERIFIED evidence, INFERENCE on call path

* Main `images/vbmeta.img` parses as AVB0 (big-endian libavb format), major 1, descriptors
  region 0x140..0x2538. At **0x240** there is a **chain descriptor (tag 4) whose name is
  `boot`** (num_bytes_following 600); further chain descriptors name `vbmeta_system` (0x4a8)
  and `vbmeta_vendor` (0x720). So `boot` is explicitly chained into the verified-boot graph.
* `images/boot.img` contains an embedded vbmeta at 0x1208000 with `AVB0`, avbtool 1.3.0,
  `sha256`, property descriptors `com.android.build.boot.*`, and an AVB footer at 0x3ffffc0
  (`AVBf`, original size and vbmeta offset both 0x1208000). This is the standard "boot carries
  its own vbmeta" layout that LK verifies via the chain descriptor.
* Live state: `ro.boot.vbmeta.device_state = [locked]`, `ro.boot.verifiedbootstate = [green]`,
  `ro.boot.veritymode = [enforcing]`, `ro.boot.flash.locked = [1]`,
  `ro.boot.vbmeta.invalidate_on_error = [yes]`, `ro.boot.vbmeta.digest = 1ce79ad5…`,
  `ro.boot.vbmeta.size = [6528]`, `ro.boot.avb_version = [1.3]`.
* AVB header flags word = 1 (bit 0, `HASHTREE_DISABLED`); bit 1 (`VERIFICATION_DISABLED`) is
  not set. So image verification remains enabled.

**Conclusion (INFERENCE, high confidence):** a modified/unsigned `boot_b` will fail AVB and a
locked device will take the `Red State` path — LK will not jump to the U-Boot payload. To boot
it, one of these is required: an authorised bootloader unlock (wipes userdata, changes state to
orange), a resigning path with the OEM key (key not present in the dump), or an equivalent
approved AVB exception. Flashing a stock-signed boot image cannot carry U-Boot.

## 4. Slot behaviour — VERIFIED / INFERENCE

* VERIFIED: bootctrl/misc metadata exists in LK (`[LK_AB]`, `boot_ctrl magic`, `slot-*` vars,
  `misc`), and `set_active_slot` / `set_active:` is a separate fastboot handler from `flash:`.
* VERIFIED: live slot is `_a`; scatter maps `boot.img` to `boot_a` and leaves `boot_b` as
  `file_name: NONE`.
* INFERENCE (standard A/B behaviour, consistent with all strings): writing bytes to `boot_b`
  **does not** make B active. An explicit `fastboot set_active b` (or an OTA bootctrl update)
  is needed. Not executed, and it requires Beru's explicit approval.

## 5. Load address / position requirements — VERIFIED + INFERENCE

* `devices/x6886/evidence/iomem.txt`: `40010000-421effff : Kernel code`, `42670000-429affff : Kernel data`.
* `devices/x6886/evidence/cmdline.txt`: `bootopt=64S3,32N2,64N2`, `kvm-arm.mode=protected`, `gpt=1`,
  `ramoops.mem_address=0x48090000`; UFS boot device in `ro.boot.boot_devices`
  (`soc/11270000.ufshci`).
* Decompressed stock kernel Image has `text_offset = 0x0`, `magic ARMd` at 0x38 — modern
  position-independent-style header; LK still places it at a fixed `kernel_load_addr` from
  `mb_kernel` (strings in §2).
* Reference X6826B had kernel code at 0x40080000; X6886 is **0x40010000**. Do not reuse the
  reference constant.

**INFERENCE:** U-Boot must either be linked at the exact address where LK deposits and enters
the decompressed payload (most likely 0x40000000 with kernel text appearing at 0x40010000, or
0x40010000 directly — needs one confirmation from LK's `kernel_load_addr` log or from the
`mb_kernel` DT reservation), or be built position-independent/PIE so it runs wherever LK puts
it. LK enters with MMU and caches off and `x0` = LK's device tree (reference doc, same LK
family) — U-Boot's early assembly must tolerate that and must not assume the Linux boot
protocol beyond the register handoff.

## 6. What transfers from `refs/hakios-mtk/docs/mtk-porting.md` — VERIFIED

The reference boots the X6826B (MT6768, eMMC, v2 boot header, gzip kernel, slot A, kernel at
0x40080000). Its chainboot idea transfers; its constants do not.

Transfers:
- The one-partition strategy: keep stock `vendor_boot`/DTBO/ramdisk and build a custom boot
  image whose "kernel" is our own first-stage code followed by padding and the original
  Android Image / payload (`tools/chainboot.py` idea).
- Run logistics: log out through ramoops/pstore when there is no serial; keep a fallback path
  that does not depend on the partition being overwritten. The doc explicitly warns that a
  hanging `boot_a` removes fastbootd/recovery and needs boot-ROM download mode as rescue — on
  X6886 that risk moves to whichever slot is active.
- "Read every address from your own phone" rule; the doc's X6826B table is not reusable.

Does not transfer / needs rework for X6886:
- Slot: reference writes only `boot_a`; this plan needs `boot_b` plus an approved slot switch.
- Boot header: v2 → **v4**, page 4096, kernel offset 0x1000, LZ4 legacy instead of gzip.
- SoC/DRAM: MT6768 eMMC MSDC0 @0x11230000 → MT6789 **UFS** (`ufshci`), load at ~0x40010000,
  different GIC/UART/ramoops/framebuffer addresses (all in `devices/x6886/evidence/fdt.dts` if needed).
- Security: reference assumes an unlocked bootloader and Magisk in boot_a. X6886 is **locked**,
  AVB green/enforcing, with a PayJoy fastboot gate. This is the main new blocker.
- The `mtkloader` chain format (arm64 header with `res2`/`res3` carrying Image length and
  payload offset) is a *payload* convention; it still has to be wrapped in a v4 boot image with
  an LZ4/gzip-compressed "kernel" for LK.

## Open items (no device action taken)

1. Confirm whether the PayJoy flag actually disables fastboot on this unit (device-only check).
2. Confirm LK's exact `kernel_load_addr` / `mb_kernel` reservation before linking U-Boot.
3. Decide, with Beru, whether an unlock + slot switch is acceptable (data wipe, approval required).
4. If an authorised path exists, rebuild vbmeta/hash descriptors consistently — reusing the
   stock `boot_b` hash with different content is not viable.

No flash commands are given as instructions; this document is analysis only.
