# Tools

Build and verification scripts for the X6886 port.

| Script | Purpose |
|---|---|
| `build-kernel-x6886.sh` | Build the XNU kernel from `refs/xnu-12377-hakios` |
| `build-loader-elf-x6886.sh` | Build the loader ELF used by the offline verifier |
| `build-payload-x6886.py` | Build kernel + two RAM disk payload |
| `chainboot-x6886.py` | Place the loader, Android Image, and payload in a boot image |
| `repack-magiskboot.sh` | Preferred repack path using magiskboot |
| `verify-x6886-final.py` | 13-check offline release gate |
| `check-mali-v9.py` | Static validator for the Mali v9 driver |
| `qemu-darwin.sh`, `run-qemu.sh` | QEMU checks |

The binary toolchain under `tools/bin/`, `tools/root/`, `tools/qemu/`, `tools/debs/`,
and package archives are local only and ignored by git.
