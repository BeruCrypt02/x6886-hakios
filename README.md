# HakiOS · Infinix X6886

[Device](devices/x6886/README.md) · [Docs](docs/index.md) · [Bring-up](docs/plan/bringup.md) · [Status](docs/plan/status.md) · [Drivers](docs/driver-review/driver-matrix.md) · [Contributing](CONTRIBUTING.md)

HakiOS runs Apple's open-source XNU kernel with a Darwin user space on the
Infinix Hot 60 Pro+ (X6886). This repository holds the device package: board
data, kernel drivers, the resident loader, build scripts, evidence, and the
bring-up record. Upstream code stays in `refs/`; the iOS system image stays in
`ios27/`. Neither is committed.

> Experimental. The phone's Android installation remains the system of record.
> No partition write is automated here. Read the safety rules before flashing.

## What is here

- **One device, explicitly.** Every constant comes from the live X6886 dump or
  an observed boot log; no value is inherited from a Qualcomm port without
  being marked as reference-only.
- **Layered data.** The device lives in `devices/x6886/`, the SoC in
  `socs/mt6789/`, drivers in `drivers/`, loader sources in `xnuloader/`.
- **Offline release gate.** `tools/verify-x6886-final.py` checks the final boot
  image, loader blob, ELF, and board sources without touching the phone.
- **Evidence beside claims.** Driver manifests cite dump files and line
  numbers. Unverified hardware stays marked as such.

## How it boots

```
boot ROM -> preloader -> LK (stock, untouched)
   LK loads boot_a: mtkloader, Android Image, XNUP payload
      mtkloader: starts UFS block server, maps ramdisk images, jumps to XNU
         XNU: mounts the Darwin root, switches to the system volume, starts launchd
            ... the run ends -> log to ramoops -> the phone returns to Android
```

Verified so far: UFS read path, APFS super mount, root switch, launchd exec,
and the first userland services. The home screen is not yet a daily-driver
state; see [status](docs/plan/status.md).

## Quick start

```sh
cd /path/to/x6886-hakios
. ./env.sh
python3 tools/verify-x6886-final.py
# RESULT: 13/13 checks passed
```

Rebuild the loader symbols if the verifier reports `6/13`:

```sh
bash tools/build-loader-elf-x6886.sh
python3 tools/verify-x6886-final.py
```

## Build

```sh
bash tools/build-kernel-x6886.sh
python3 tools/build-payload-x6886.py build/payload-x6886.bin
sh tools/repack-magiskboot.sh \
  "$DUMP_ROOT/images/boot.img" \
  devices/x6886/prebuilt/mtkloader.bin \
  build/payload-x6886.bin \
  "$PWD/build/boot-HakiOS-X6886.img"
```

The verifier reads only local files. `ios27/`, `refs/`, the toolchain, and
generated boot images are local inputs and are ignored by git.

## Safety

- Do not write GPT, `super`, `userdata`, `metadata`, `lk`, `preloader`,
  `secfg`, `vbmeta`, or `tee`.
- Stock Transsion LK rejects `fastboot boot` with `unknown command`.
- Confirm the active slot before any write. Writing `boot_a` and `boot_b` is
  explicit, never implicit.
- A successful fastboot write is proven only by comparing the written hash with
  the built image.

Full rules: [docs/plan/boot-image-safety.md](docs/plan/boot-image-safety.md).

## Device

| Device | SoC | State |
| --- | --- | --- |
| Infinix Hot 60 Pro+ (X6886) | MediaTek MT6789 / Helio G200 | Bring-up; userland work in progress |

Board facts: [devices/x6886/README.md](devices/x6886/README.md). SoC facts:
[socs/mt6789/soc.toml](socs/mt6789/soc.toml).

## Contributing and licences

Read [CONTRIBUTING.md](CONTRIBUTING.md). Imported XNU, HakiOS, and IPSW-derived
material keep their upstream licences; this repository's notes and scripts are
provided for bring-up research.
