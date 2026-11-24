# Phase 1: HakiOS superimage packaging

Deployment runs from a stock Recovery boot. Recovery flashes a prepared super
image and a HakiOS boot image; Penumbra BROM reverts both. The loader no longer
chains Android, so no LK-owned UFS state needs preserving.

## On-device geometry (MTK scatter, SYS65)

| Item | Value |
|---|---|
| Partition name | `super` |
| Physical start | `0x60000000` bytes = LBA `0x60000` (393216) on UFS LUN 2 |
| Size | `0x30bd75000` bytes = `0x30bd75` blocks (3194229), 13083561984 bytes |
| Block size | 4096 bytes (UFS) |

The loader reads the GPT from LUN 2, finds `super` by its UTF-16 name, then
reads the partition's first block. It validates a HakiOS header before it uses
any byte of the container.

## Header (512 bytes, partition LBA 0 = device LBA 0x60000)

| Offset | Size | Field | Value |
|---|---|---|---|
| 0x00 | 8 | magic | `HAKIOS\0\0` |
| 0x08 | 4 | version | `1` (little endian) |
| 0x0c | 4 | flags | `0` |
| 0x10 | 8 | apfs_off | `4096` (one full block past the partition start) |
| 0x18 | 8 | apfs_len | APFS container byte length |
| 0x20 | 480 | reserved | zero |

The rest of block 0 stays zero. The APFS container starts at partition byte
4096, device LBA `0x60001`. The loader checks for `NXSB` (`0x4253584e`) at
container offset 32 before accepting it.

## Container selection and size limit

XNU memory is `0x140000000 + 0x0ff600000`. The system image lands at
`SYSTEM_PA = 0x148000000`. Keeping 64 MiB for the DTB and boot_args caps the
container at `0xf3600000` bytes (4083154944, 3.803 GiB).

`ios27/system27-prune.apfs` is 4297248768 bytes (4.002 GiB). It does not fit.
The loader rejects it and falls back to the embedded md0. Phase 1 therefore
uses `ios27/root26-darwin.apfs` (4194304 bytes) as the APFS container in super.

`load_system_image()` walks consecutive APFS containers. Put a zero block after
the root container so it stops there:

| Partition block | Content |
|---|---|
| 0 | HakiOS header (512 bytes) + zero padding to 4096 |
| 1..1024 | `root26-darwin.apfs` (4194304 bytes) |
| 1025 | one zero block, terminates the walk |
| 1026.. | unused; pad the image to the full super size |

`ios27/flashboot.apfs` and `ios27/system27-prune.apfs` stay out of this image.
They can be appended after the terminator as inert data for a later phase.

## Build command

Run from the project root:

```sh
python3 - <<'PY'
import struct
B = 4096
SUPER = 0x30bd75000
hdr = b'HAKIOS\0\0' + struct.pack('<IIQQ', 1, 0, B, B) + b'\0' * 480
root = open('ios27/root26-darwin.apfs', 'rb').read()
assert len(root) == B and root[32:36] == b'NXSB'
img = hdr + b'\0' * (B - len(hdr)) + root + b'\0' * B
img += b'\0' * (SUPER - len(img))
open('build/hakios-super.img', 'wb').write(img)
PY
```

Verify offline before flashing:

```sh
python3 - <<'PY'
import struct
d = open('build/hakios-super.img', 'rb').read(3 * 4096)
assert d[:8] == b'HAKIOS\0\0'
ver, flags, off, length = struct.unpack_from('<IIQQ', d, 8)
assert (ver, flags, off, length) == (1, 0, 4096, 4096)
assert d[4096 + 32:4096 + 36] == b'NXSB'
assert d[8192:12288] == b'\0' * 4096
print('header and first APFS container OK')
PY
```

## Flashing from stock Recovery

Push both images, then write them with `dd` (TWRP or a root shell):

```sh
adb push build/hakios-super.img /sdcard/hakios-super.img
adb push build/boot-x6886-final.img /sdcard/hakios-boot.img
adb shell 'dd if=/sdcard/hakios-super.img of=/dev/block/by-name/super bs=4096 conv=fsync'
adb shell 'dd if=/sdcard/hakios-boot.img of=/dev/block/by-name/boot_a bs=4096 conv=fsync'
```

Do not touch GPT, preloader, LK, tee, userdata, metadata, `tr_product`,
`tr_region`, or slot B. Revert with Penumbra BROM.

## Loader log lines for the next live test

- `super at block ... blocks ...` GPT lookup succeeded on LUN 2.
- `super: HakiOS v1 header, APFS at +0x1000 (4 MB)` header accepted.
- `super: no APFS header, using embedded md0 root26-darwin.apfs` header missing
  or no `NXSB`; XNU boots from the embedded ramdisk.
- `root: super APFS as md0` / `root: embedded md0 root26-darwin.apfs` final choice.

UFS bring-up logs each stage: `refclk on (PRE+POST)`, `host reset done`,
`HCE cycled`, `device reset done`, `link up`, `PA_PWRMODE`,
`transfer list handed off`, `fDeviceInit done`, `LUN2 GPT ready`. Any failure
clears `ufs_list_phys` and the loader falls back to the embedded XNUP ramdisk.
