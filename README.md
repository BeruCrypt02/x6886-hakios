# HakiOS for Infinix Hot 60 Pro+ (X6886)

Device-specific bring-up work for Darwin/XNU (HakiOS) on the Infinix X6886:
MediaTek MT6789 / Helio G200, 8 GB RAM, UFS 2.2, 1080x2400 AMOLED, Mali-G57 MC2.

This repository tracks the board package, kernel drivers, loader sources, build
scripts, evidence, and notes. The large iOS system image, upstream checkouts,
and local toolchain are intentionally not committed; see `.gitignore`.

## Status

The current work reaches the Darwin root mount, switches the root to the system
volume, starts `/sbin/launchd.darwin`, and continues into userland bring-up.
The home screen is not yet a supported daily-driver state. See
[docs/plan/STATUS.md](docs/plan/STATUS.md) for the exact gate-by-gate record.

## Layout

| Path | Purpose |
|---|---|
| `devices/x6886/` | Device manifest, board config, prebuilt loader/DTB/firmware, evidence, legacy paths |
| `socs/mt6789/` | Shared SoC facts for the Helio G200 |
| `drivers/` | X6886 kernel drivers grouped by subsystem |
| `xnuloader/` | Resident loader sources (`mtkloader.c`, `ufs.h`, `ufs_darwin.h`, `start.S`) |
| `tools/` | Build, payload, QEMU, and verification scripts |
| `ios27/` | Local, ignored iOS 27 system and RAM images |
| `refs/` | Local, ignored upstream checkouts |
| `docs/` | Getting started, porting notes, driver reviews, plan, and dump evidence |
| `build/` | Generated boot images and ELF/object output, ignored by git |

Start at [docs/index.md](docs/index.md).

## Prerequisites

- Linux host with the local toolchain under `tools/root/` and `tools/bin/`.
- Upstream checkouts under `refs/` (ignored).
- Stock boot image and device dumps under `/home/berucrypt00/otadump` (ignored).
- iOS 27 images under `ios27/` (ignored).

Source the environment first:

```sh
cd /path/to/x6886-hakios
. ./env.sh
```

## Build

The offline verifier is the release gate. It reads only local files and never
touches the device:

```sh
python3 tools/verify-x6886-final.py
# RESULT: 13/13 checks passed
```

Rebuild the loader symbols used by that verifier:

```sh
bash tools/build-loader-elf-x6886.sh
```

Rebuild the kernel and payload only when changing their sources:

```sh
bash tools/build-kernel-x6886.sh
python3 tools/build-payload-x6886.py build/payload-x6886.bin
```

Repack a boot image with magiskboot (absolute output path required):

```sh
sh tools/repack-magiskboot.sh \
  "$DUMP_ROOT/images/boot.img" \
  devices/x6886/prebuilt/mtkloader.bin \
  build/payload-x6886.bin \
  "$PWD/build/boot-HakiOS-X6886.img"
```

## Flashing safety

This project does not flash automatically. If you flash manually:

- Do not modify GPT, `super`, `userdata`, `metadata`, `lk`, `preloader`, `secfg`,
  `vbmeta`, or `tee`.
- Stock Transsion LK does not implement `fastboot boot`; it answers
  `unknown command`.
- Confirm the active slot before writing. Both `boot_a` and `boot_b` must be
  written explicitly if the experiment targets both slots.
- Never treat a partition write as successful until the written hash matches
  the built image.

See [docs/plan/boot-image-safety.md](docs/plan/boot-image-safety.md).

## Troubleshooting

| Symptom | First check |
|---|---|
| `RESULT: 6/13` from the verifier | Rebuild `tools/build-loader-elf-x6886.sh`; the ELF under `/tmp/opencode` is scratch |
| `MOUNT_DIAG ok=0 errno=5` | UFS block-server read failure; see `docs/driver-review/x6886-audit.md` |
| `failed to query hardware target: 22` | Kernel `hw.target` sysctl fallback missing |
| Text console hides the Apple logo | Loader must set `v_display=1` and `PE_create_console()` must use text mode during `-v` |
| Cryptex mount ENOENT | Expected on this image; the inline shared-cache bind is the fallback |

## Contributing

See [CONTRIBUTORS.md](CONTRIBUTORS.md) and
[docs/reference/project-reorg.md](docs/reference/project-reorg.md). Keep large
binary inputs out of git and run the 13/13 verifier before committing build
changes.

## License

See upstream HakiOS/XNU licenses for the imported sources. This repository's
original notes and scripts are provided as-is for bring-up research.
