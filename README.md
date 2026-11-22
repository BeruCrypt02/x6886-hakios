# HakiOS for Infinix X6886

Target: Infinix Hot 60 Pro+ (X6886), MediaTek MT6789 / Helio G200, 8 GB RAM, UFS 2.2, 1080x2400 AMOLED, Mali-G57 MC2.

This repository holds device-specific work for booting Darwin/XNU on the X6886. Upstream code stays in the oosexgang repositories; this tree keeps the board package, build recipes, QEMU checks, notes, and safe flashing procedures.

## Safety rules

- Slot A is Android. Do not write to it.
- Do not modify GPT, super, userdata, metadata, lk, preloader, seccfg, vbmeta, or tee.
- Stock Transsion LK does not implement `fastboot boot`. It answers `unknown command`.
- Device tests target slot B only after parity and P7 checks, and only after explicit approval.
- No `fastboot flash`, `fastboot erase`, or `fastboot --set-active` runs without explicit approval.

## Layout

- `docs/` notes, status, device data, research.
- `refs/` shallow clones of upstream and MT6789 reference projects, ignored by git.
- `tools/` local toolchain and utilities, ignored by git.
- `board/` X6886 board package from the live device tree.
- `build/` build outputs, ignored by git.

## Host setup

Source `env.sh`, then use `tools/bin` and `tools/root/usr/bin`. The tools include QEMU 8.2, clang/llvm 20, lld 20, dtc, aarch64 cross tools, magiskboot, gh, adb, and fastboot.
