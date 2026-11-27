# Independent safety review — `build/boot-x6886.img` before flashing `boot_a`

Reviewer: independent subagent. Date: 2026-11-23.
Scope: **read-only** analysis of files on disk. The phone was **not** touched. `/home/berucrypt00/Documents/port-tools` was **not** used. No flash, no slot change, no writes to any image.

---

## VERDICT: **UNSAFE TO FLASH (as-is)**

Do **not** flash this image to `boot_a` while the device is in the state recorded in this repository (`locked` / `green` / `enforcing`).

The image is structurally well-formed at the boot-header and payload level, but it has been **stripped of every AVB structure** that the stock `boot.img` carries. On a locked MT6789 with AVB verifying `boot`, LK will reject it before it ever jumps to the loader. This is not a "might not boot" issue — it is a known, reproducible verification failure path documented in this repo's own research.

It is **not** a brick-by-itself image in the sense of damaging hardware: the expected outcome is AVB Red State / no boot / bootloop, recoverable through BROM download mode — **but the recovery tool is not staged** (`mtkclient`/`pyserial` absent), and a broken `boot_a` removes fastboot and recovery on an A/B device. So the practical risk is high.

Verdict can only change to "conditional" after a **read-only** device check proves AVB verification is disabled or the bootloader is unlocked, and after rescue tooling is staged. Both are out of scope for this read-only review.

---

## 1. Boot header parse (all fields, v4)

Command used:

```sh
python3 /tmp/hakios-x6886/parse_boot.py
# reads d[:8], u32 @8/12/16/20/24/28/32/36/40/44, name @48, cmdline @64,
# id/SHA1 @576, extra @608, extra region @1632..1680
```

Exact output (abridged to the fields that matter):

| Offset | Field | STOCK `boot.img` | OURS `boot-x6886.img` | Consistent? |
|---|---|---|---|---|
| 0 | magic | `ANDROID!` | `ANDROID!` | yes |
| 8 | kernel_size | `0x01202d0e` = 18,885,902 | `0x015b70a1` = 22,769,825 | expected change |
| 12 | kernel_addr | 0 | 0 | copied |
| 16 | ramdisk_size | **0** | **0** | yes |
| 20 | ramdisk_addr | `0x630` (1584) | `0x630` (1584) | copied (odd, but stock) |
| 24 | second_size | 0 | 0 | yes |
| 28 | second_addr | 0 | 0 | yes |
| 32 | tags_addr | 0 | 0 | copied |
| 36 | page_size | **0** | **4096** | **DELTA** (see below) |
| 40 | header_version | **4** | **4** | yes |
| 44 | os_version | 0 | 0 | yes |
| 48 | name[16] | empty | empty | yes |
| 64 | cmdline[512] | empty | empty | yes |
| 576 | id / SHA1[32] | all zeros | `845b643240bb1ccb29c35d4d3c905c00ffc6ade5` + zeros | **DELTA** (see below) |
| 608 | extra_cmdline | empty | empty | yes |
| 1632 | v2 extra field | 0 | 0 | yes |
| 1648 | v2 extra field | 0 | 0 | yes |
| 1652 | **v4 rdo_size** | **0** | **0** | yes |
| 1668 | **v4 dtb_size** | **0** | **0** | yes |

Other v4-region words 1636/1640/1644/1656/1660/1664/1672/1676/1680 are all zero in both images.

**Findings**

- **VERIFIED — header is internally coherent.** magic, v4, all size/offset fields agree with the physical layout; the kernel actually starts at file offset 4096 and is exactly `kernel_size` bytes.
- **VERIFIED — `page_size` 0 → 4096 is a deliberate change** made by `chainboot-x6886.py` line 88 (`struct.pack_into('<I', hdr, 36, page)`). The dump's header has `page_size = 0`, yet the kernel bytes begin at offset 4096 and stock boots. So LK does not trust that field (or default-zero maps to 4096). Setting it to 4096 matches the real layout and is probably harmless — but it is a deviation from stock and is **INFERENCE** that LK handles it identically. Low severity.
- **VERIFIED — the SHA1 field at 576 uses a non-standard algorithm.** The build script hashes `kernel, ramdisk, second, rdo, dtb` (5 parts, each with a 4-byte length). AOSP standard for v1 is 3 parts and for v4 is 4 parts:

  ```
  AOSP v1  (kernel,ramdisk,second)      644ed423dcebbc4d99add1e453781fe9186eae08  != stored
  AOSP v4  (kernel,ramdisk,second,dtb)  2e96c427de2bc056d0fe292889ed5b25f40fbb58  != stored
  build script (5 parts)                845b643240bb1ccb29c35d4d3c905c00ffc6ade5  == stored
  ```

  The stock field is all zeros, so LK demonstrably does **not** check this field. Low severity, but it means the field gives no protection and is not AOSP-conformant.
- **VERIFIED — no cmdline in boot.img.** Correct for this layout; cmdline lives in `vendor_boot.img`.

---

## 2. Kernel / mtkloader / XNUP payload verification

Command used:

```sh
python3 /tmp/hakios-x6886/parse_kernel.py
python3 /tmp/hakios-x6886/parse_more.py
python3 /tmp/hakios-x6886/parse_xnup.py
```

Exact key outputs:

```
page=4096 ksize=22769825
kernel first bytes: 1f8b080000000000
decompressed blob: 62255536 bytes

--- loader file: 36256 bytes ---
loader magic @0x38: b'ARMd'   code0 @0: 10000014
loader text_offset @0x08: 0x80000
loader image_size @0x10: 0x2000000 (33554432)      [file]
loader res2 @0x28: 0x0                              [file]
loader res3 @0x30: 0x0                              [file]

--- blob layout ---
CHAIN_OFFSET=0x40000; padding after loader = 225888 bytes (all zero? True)
Android Image magic @CHAIN+0x38: b'ARMd'
Android Image text_offset @+8: 0x0
Android Image image_size @+0x10: 0x26e0000 (40763392)
loader-in-blob image_size@0x10: 0x3c00000 (62914560)
loader-in-blob res2 @0x28: 40024576
loader-in-blob res3 @0x30: 0x266ba00 (40286720)
Image region: 0x40000 .. 0x266ba00 (40024576 bytes)
expected payload_off = CHAIN + align16(len_image) = 0x266ba00; actual res3=0x266ba00 -> match? True
blob total == payload_off + len(payload) == 62255536

--- XNUP payload ---
magic=b'XNUP' size_field=21968808 file_len=21968816 file_len-8=21968808 match=True
@0x8      name=b'kernel'   size=17774432 (0x10f3760) res=0 data@0x20     first=cffaedfe (Mach-O)
@0x10f3780 name=b'ramdisk'  size=4194304  (0x400000)  res=0 data@0x10f3798
@0x14f3798 END entry (size=0 res=0) remaining=24
```

Additional integrity checks:

```
stock decompressed Image len: 40024576 (0x262ba00)
stock Image image_size @0x10: 0x26e0000 (40763392)   <-- stock itself declares more than it contains
blob Image segment byte-identical to stock decompressed? True
stock sha256 e673c662f51617ef19887c1e221402416801ae819fe1d256da69fae808f25391
blob  sha256 e673c662f51617ef19887c1e221402416801ae819fe1d256da69fae808f25391
kernel entry sha256 d34da23417ea6098... == refs/xnu-12377-hakios/BUILD/obj/mach.development.bcm2837
ramdisk entry sha256 a4699cfaf3fa6225... == ios27/root26-darwin.apfs
```

Loader prefix diff versus the on-disk loader file: exactly 8 differing bytes at offsets `0x12,0x13` (image_size), `0x29,0x2a,0x2b` (res2), `0x31,0x32,0x33` (res3) — i.e. **only** the three fields the build script is supposed to patch.

**Findings**

- **VERIFIED — the chain container is correct.** mtkloader padded to `0x40000`, Android Image byte-identical to the stock decompressed Image, XNUP payload appended with correct 16-byte alignment, `res3` exactly equals the real payload offset, total blob size exactly equals `payload_off + payload_len`.
- **VERIFIED — mtkloader is linked at `0x40080000`.** `link.ld` sets `. = KERNEL_LOAD`; build.sh passes `--defsym=KERNEL_LOAD=0x40080000` from `build/boards/x6886/hakios_platform.h` line 61 (`#define HAKIOS_LOADER_KERNEL_LOAD 0x40080000ULL`). `start.S` carries `text_offset = 0x80000`, flags `0xa`, `ARM\x64` magic. When LK loads at `0x40080000`, `_start = 0x40080000` and `res3`-relative addressing lands on the payload correctly.
- **VERIFIED — XNUP header and both entries are present and correctly sized.** Magic, size field, kernel (Mach-O `0xfeedfacf`), ramdisk (4 MiB APFS), and END marker all validate; both entries hash-match their source files.
- **VERIFIED — a 62 MB decompressed blob is produced** (stock Image ~40 MB + 21.97 MB payload). `image_size` is written as 60 MiB rounded up from 62,255,536 bytes — larger than the blob, so no under-declaration.
- **INFERENCE, MEDIUM — the blob is ~1.5× the stock decompressed kernel size.** The stock Image itself declares `image_size = 0x26e0000` (40.7 MB) while its lz4 stream yields 40,024,576 bytes; our Image segment is byte-identical to that, so Android should behave the same. But whether MTK LK's in-place gzip destination region tolerates a 62 MB stream is not proven from static analysis. The board's XNU window (`0xc0000000`) is elsewhere, so DRAM space is likely sufficient. No evidence of an overflow, but also no proof of safety.
- **VERIFIED — default boot path is Android, not XNU.** Loader strings contain `HAKIOS:BOOT-XNU` (the opt-in marker) and `starting Android`; porting.md §"The boot chain" states the default is Android and a hung XNU kernel costs a power cycle. Good safety property of the design, independent of this image's AVB problem.

---

## 3. Comparison with other working ports

Files read:

```sh
cat refs/oosexgang-xnu27/loader/chainboot.py
cat refs/oosexgang-xnu27/tools/xnu-cycle.sh
cat refs/akita/mk-first-boot.sh
cat refs/akita/u-boot/files/board/samsung/exynos-mobile/hakios-akita.env
cat refs/oosexgang-xnu27/loader/build.sh
```

**Our `chainboot-x6886.py` is byte-for-byte identical to `refs/oosexgang-xnu27/loader/chainboot.py`.** So it is not an independent implementation — every bug/design choice of the reference is inherited verbatim. That includes the non-standard SHA1 algorithm and the complete absence of AVB handling.

| Aspect | oosexgang / our image | akita (Pixel 8a, U-Boot) |
|---|---|---|
| Bootloader used | stock MTK LK, untouched | HakiOS U-Boot chainloaded |
| Delivery | **flashes `boot_a` every run** | `fastboot boot` only, **nothing flashed** |
| Container | Android boot.img v4, gzip of mtkloader+Image+XNUP | boot image whose **ramdisk** carries a U-Boot container (`tools/mkubootimg.py`) |
| Fallback | other slot / BROM only | slot b holds the installed image; slot a stays Android |
| Safety net | default Android; watchdog on spare core (`XNU_WATCH`) | U-Boot watchdog net: no home screen in 4 min → reboot to fastboot (`hakios_wdt_net`, `bootcmd`/`hakios_failed`) |
| HW init | loader does UFS, framebuffer, marker itself | U-Boot env powers touch, USB, UFS, disables watchdogs before handoff |
| AVB | **not handled at all by the packer** | different bootloader/device; not comparable |

**Missing steps / artifacts in our image that the other flows provide or preserve:**

1. **CRITICAL — AVB signing / footer / embedded vbmeta.** Stock boot.img has embedded `AVB0` at `0x1204000`, `0x12048c0`, `0x1208000` and an `AVBf` footer at `0x3ffffc0` (original size and vbmeta offset both `0x1208000`). Our image contains **no `AVB0` and no `AVBf` anywhere** (verified by byte scan). `chainboot.py` copies `d[:page]` (header only), then writes a fresh kernel and zero-length ramdisk/second/rdo/dtb, then pads. The embedded vbmeta that lived inside the stock kernel region is overwritten by the larger new kernel. No re-signing step exists anywhere in the build.
2. **CRITICAL — no verified-boot state check is gated in the build.** Neither `chainboot.py`, `build.sh`, nor `xnu-cycle.sh` checks `fastboot getvar unlocked` / `verified` before flashing. xnu-cycle.sh only checks `current-slot == a` and the product name.
3. **MEDIUM — no `fastboot boot` dry-run path.** porting.md explicitly says "A boot from RAM alone (`fastboot boot` of such an image, nothing written) has not been made to work with this loader: every run writes `boot_a`." So there is no safe rehearsal.
4. **MEDIUM — no backup image is produced by this build.** `xnu-cycle.sh` relies on `backup/boot_a_magisk.img` existing (built by `build_flashed.py`). This build README has no equivalent step.
5. **LOW — no dtbo/vendor_boot handling needed** (correctly): vendor_boot and dtbo are separate images and are left untouched. See §4.

---

## 4. Is `rsize = 0`, `dtb_size = 0` valid for MTK LK?

Command used:

```sh
python3 /tmp/hakios-x6886/parse_vendor2.py
```

Verified:

```
=== vendor_boot.img (67108864 bytes) ===
magic: b'VNDRBOOT'
@8  = 4            (header version)
@12 = 4096         (page size)
@16 = 0x40000000   (kernel addr)
@20 = 0x66f00000   (ramdisk addr)
@24 = 0x01f63b9e   (32914334) vendor ramdisk size
name/cmdline: "bootopt=64S3,32N2,64N2 bootconfig"
```

**VERIFIED — zero ramdisk and zero dtb in boot.img is normal for this phone.** The stock `boot.img` itself has `ramdisk_size = 0`, `dtb_size = 0`, empty cmdline, and boots. The real ramdisk (~31 MB) and DTB live in `vendor_boot.img`, which we do not modify. Our image matches the stock values exactly, so LK will find nothing missing relative to stock. The build script's comment ("the real ramdisk+dtb live in vendor_boot") is correct.

**VERIFIED — `dtbo.img` (8 MB) is a separate partition** and is untouched.

So: no issue here. The problem is AVB, not the GKI-style split.

---

## 5. Brick risk and recovery

Evidence read:

```sh
grep -n -i -E 'boot rom|download mode|mtkclient|rescue|fastboot' refs/oosexgang-xnu27/docs/porting.md
sed -n '60,100p' refs/oosexgang-xnu27/docs/troubleshooting.md
sed -n '110,140p' refs/oosexgang-xnu27/docs/building.md
sed -n '73,112p' refs/akita/STATUS.md
command -v mtkclient mtk.py        # empty
python3 -c 'import serial,usb'     # ModuleNotFoundError: No module named 'serial'
```

**Worst case, as documented by the reference project:**

- porting.md §"What the phone must allow", item 4: *"On an A/B phone recovery and fastbootd live inside `boot`, so a `boot_a` that hangs before LK's keys are read leaves no fastboot. On the first phone that happened once and the boot ROM's download mode with mtkclient (`mtk.py w boot_a <the rooted image>`) was the rescue. Check that your phone's boot ROM is reachable **before** the first flash, and keep the rooted image at hand. Slot B is never written by anything here."*
- building.md §"If it does not come back": *"A kernel that hangs costs a power cycle... If `boot_a` itself is broken, the way back is the boot ROM's download mode (mtkclient, `mtk.py w boot_a backup/boot_a_magisk.img`); check that you can reach it before the first flash."*
- porting.md table: *"The phone boots only into a hung XNU — an image that always started XNU; the boot ROM was the way back."*

**VERIFIED:**

- The reference design deliberately avoids "always XNU" — default is Android, XNU is a one-shot opt-in via `HAKIOS:BOOT-XNU` in pmsg. A hung XNU therefore normally costs a power cycle, not BROM.
- **`mtkclient`/`mtk.py`/`pyserial` are NOT installed** on this machine (`command -v` empty, `import serial` fails).
- The repo's own `plan/DECISION.md` and `plan/lk-bootb-feasibility.md` record live state: `locked`, `green`, `enforcing`, `flash.locked=1`, and flag "No local rescue" as a hard gate.
- akita's safety net (auto-reboot to fastboot after 4 minutes, `fastboot boot` only) does **not** exist in this X6886 flow.

**INFERENCE (skeptical reading):**

- **Boot ROM download mode is not guaranteed on MT6789 just because the SoC is MediaTek.** It depends on the exact ROM/SecCtrl configuration and whether the test points / key combination are accessible. The reference docs say to *check reachability before the first flash* — which has not been done here. Treat BROM as unproven, not as a safety net.
- With a locked bootloader and AVB covering `boot`, the expected failure is not a silent bootloop but LK's **Red State** path before handing off. Depending on LK policy that can be a hard stop requiring BROM, not a retry.
- A/B fallback is also not a reliable rescue: current slot is `_a`; slot `_b` is not prepared with a working rooted image, and the scatter marks `boot_b`/`vbmeta_b` `NONE`/`INVISIBLE`/`is_download:false` (per `plan/lk-bootb-feasibility.md`).

** Exact recovery procedure if the flash goes wrong (from the docs, not executed here):**

1. Power off; force boot ROM / download mode (MT6789 test-point or key sequence — **must be verified on-device first**).
2. On a host with `mtkclient` installed: enumerate the MT6789 download device read-only first.
3. Restore the backed-up rooted boot image: `python3 mtk.py w boot_a backup/boot_a_magisk.img` (or write the stock `boot.img` from the dump).
4. Verify the device returns to fastboot/Android; do not switch slots.

**This procedure is currently impossible on this host** because the tooling is absent. That alone is sufficient to block the flash.

---

## 6. Touch firmware / missing `fw-touch/hdl_firmware.img`

Evidence:

```sh
cat refs/oosexgang-xnu27/loader/build.sh
sed -n '2174,2180p' refs/oosexgang-xnu27/loader/mtkloader.c
strings -a refs/oosexgang-xnu27/build/loader/mtkloader.bin | grep -i touch
```

Exact relevant code:

```c
if (TOUCH_FW_SIZE == 0) {       /* (loader/build.sh found no firmware of the phone's to carry) */
    out("touch: this loader was built without the controller's firmware: no touch screen\n");
    return 0;
}
```

and build.sh:

```sh
FW=${TOUCH_FW:-$ROOT/fw-touch/hdl_firmware.img}
if [ -f "$FW" ]; then ... else
    echo "build.sh: no $FW: the loader is built without the touch controller's firmware"
    ... '#define TOUCH_FW_SIZE 0u' ...
fi
```

**VERIFIED — building without touch firmware is safe from a boot/hang perspective.** The missing file is handled explicitly: the loader logs "no touch screen" and returns. There is no wait loop, no panic, no unconditional dependency on the firmware blob in the boot path. The loader binary's string table does not even contain that message because `features = ["ufs"]` only — the touch-probe code is not compiled into this board's build. The build warning is about **functionality** (no touch under XNU), not about boot safety.

**INFERENCE, LOW — the board file is internally inconsistent:** `devices/x6886/board.toml` says `features = ["ufs"]` while `devices/x6886/README.md` says `features = []`. Also the marker/panic-log/scratch addresses are documented as *placeholders* to be confirmed on-device. Wrong marker addresses could corrupt a region used by Android at runtime; not a pre-boot hang, but it reinforces that this is a first bring-up, not a proven image.

---

## Issue register

| # | Severity | Status | Issue |
|---|---|---|---|
| 1 | **CRITICAL / BLOCKER** | VERIFIED | Our boot.img has **no embedded AVB vbmeta and no AVB footer**. Stock has `AVB0` at `0x1204000/0x12048c0/0x1208000` and `AVBf` at `0x3ffffc0`. vbmeta chains `boot` (chain descriptor at 0x240, name `boot` at 0x29c, nb=600). On a locked/enforcing device LK will reject the image → Red State, no boot. |
| 2 | **CRITICAL / BLOCKER** | VERIFIED | No rescue tooling staged (`mtkclient`, `pyserial`, `pyusb` absent). Documented recovery path (BROM + `mtk.py w boot_a`) cannot be executed. |
| 3 | **HIGH** | INFERENCE (device state recorded earlier, not re-verified now) | Device recorded as locked/green/enforcing. `plan/DECISION.md` says "vbmeta معطل" for the ROM plan, which conflicts with that record — must be resolved by read-only `fastboot getvar all` before any flash. |
| 4 | **MEDIUM** | VERIFIED | No `fastboot boot` rehearsal possible with this loader; every run writes `boot_a`. |
| 5 | **MEDIUM** | VERIFIED | Build produces no backup of the previously working `boot_a`. |
| 6 | **MEDIUM** | INFERENCE | 62 MB decompressed blob vs ~40 MB stock; LK's decompress destination tolerance not proven. |
| 7 | **LOW** | VERIFIED | `page_size` changed 0 → 4096 (probably correct, but a deviation from stock). |
| 8 | **LOW** | VERIFIED | SHA1 @576 uses a non-AOSP 5-part algorithm; stock field is zero so LK ignores it. |
| 9 | **LOW** | VERIFIED | No touch firmware; safe to boot, touch simply will not work. |
| 10 | **LOW** | VERIFIED | Board README vs board.toml `features` mismatch; marker addresses are placeholders. |

---

## What must happen before this image can be flashed

1. **Resolve the AVB gate.** Either confirm read-only that the device is unlocked / verification disabled, or obtain an authorised signing path. Do not rely on the "vbmeta disabled" note without a live check.
2. **Stage rescue tooling** (`mtkclient` + `pyserial`/`pyusb`) in a local venv and verify read-only enumeration of the MT6789 download device. Confirm BROM reachability before the first flash.
3. **Back up the working `boot_a`** to a file outside the phone.
4. **Decide whether to preserve/rebuild AVB metadata** in the image; current build strips it entirely. This is a build change, not a flashing trick.
5. Only then consider a single flash to `boot_a`, with `current-slot == a` verified first.

Until all five are done: **do not flash.**

---

## Commands appendix (exact, read-only)

```sh
python3 /tmp/hakios-x6886/parse_boot.py                 # header fields of both images
python3 /tmp/hakios-x6886/parse_kernel.py               # gzip decompress, loader/res fields, XNUP scan, SHA1
python3 /tmp/hakios-x6886/parse_more.py                 # stock lz4 decompress, loader diff, XNUP hexdump
python3 /tmp/hakios-x6886/parse_xnup.py                 # XNUP entry walk
python3 /tmp/hakios-x6886/parse_vendor2.py              # vendor_boot header, vbmeta hexdump
python3 /tmp/hakios-x6886/parse_avb.py                  # vbmeta field decode
# inline python byte scans for AVB0/AVBf in stock and our images, chain-descriptor name "boot"
cat refs/oosexgang-xnu27/loader/chainboot.py
diff <(cat build/chainboot-x6886.py) <(cat refs/oosexgang-xnu27/loader/chainboot.py)   # identical
cat refs/oosexgang-xnu27/tools/xnu-cycle.sh
cat refs/akita/mk-first-boot.sh
cat refs/akita/u-boot/files/board/samsung/exynos-mobile/hakios-akita.env
cat refs/oosexgang-xnu27/docs/porting.md
sed -n '60,100p' refs/oosexgang-xnu27/docs/troubleshooting.md
sed -n '110,140p' refs/oosexgang-xnu27/docs/building.md
cat refs/oosexgang-xnu27/devices/x6886/board.toml
grep -n 'KERNEL_LOAD' refs/oosexgang-xnu27/build/boards/x6886/hakios_platform.h
command -v mtkclient mtk.py lz4 python3
python3 -c 'import serial, usb'
```
