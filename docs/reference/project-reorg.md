# X6886 Project Reorganization Proposal (SM6475-shape)

**Status:** PROPOSAL ONLY. Nothing in this document has been executed. No files were moved,
no code was written or edited, no build was run, no device was accessed, nothing was flashed.

**Reference consulted:** `/home/berucrypt00/.hermes/cache/scratch/hakios-sm6475` — layout and
conventions only. No file content from that repository is imported, copied or adapted into this
project. The reference is read-only.

**Project root:** `/home/berucrypt00/hakios-x6886` is a symlink to the real directory
`/home/berucrypt00/x6886-hakios`. Both names appear in this repository's scripts. Paths below use
`/home/berucrypt00/hakios-x6886` (= `$ROOT`); the alias must stay in place until Phase 4.

---

## 1. Goal and constraints

Reshape this device-specific X6886 tree into the same **roles** as the SM6475 reference:

| Layer | Reference role | X6886 current counterpart |
|---|---|---|
| Common | `configs/`, `drivers/`, `tools/`, `docs/`, `xnu*`, `userland/`, `sysimage/`, `qemu/`, `Makefile`, `ci/` | `tools/`, `build/*.sh`, `drivers/*.c`, `ios27/`, `refs/`, `docs/`, `plan/`, `docs/reference/dump-analysis/` |
| Device | `devices/manifest.conf`, `devices/<name>/{device.conf,configs/,prebuilt/,soc/,*.args,README.md}` | `devices/x6886/` (board.toml), `devices/x6886/legacy/` (old DTS), `board/`, `devices/x6886/evidence/`, `devices/x6886/legacy/darwin16/` |
| SoC | `socs/<name>/` for shared SoCs; `devices/<name>/soc/` for device-carried ones | `socs/mt6789/soc.toml` |

Constraints held throughout:

1. Move/rename files only. No content copied from the reference, no new driver code, no new
   defconfig contents, no rewriting of boot-image logic.
2. The offline boot-image check (`build/verify-x6886-final.py`, 13 checks, and
   `tools/verify-x6886-final.c`) must pass with the **same** image bytes before and after every
   phase. These checks read files only; they do not flash.
3. Nothing here depends on device state. No `fastboot`, `adb`, slot change or write to `boot`.
4. Several critical inputs are **git-ignored** (`ios27/`, `refs/`, `tools/`, `build/*`,
   `build/boards/`, `*.img`, `*.bin`, `*.dtb`). Git cannot roll those back; they are handled
   explicitly in §7.

---

## 2. Reference shape (what we imitate)

```
hakios-sm6475/
  configs/          defconfig, common_defconfig, boot-args.lst, symbols.tsv, *.config
  devices/          manifest.conf, README.md, <device>/{device.conf,configs/,prebuilt/,soc/,*.args,README.md,pull-*.sh}
  socs/             README.md, <soc>/ for SoCs shared by more than one device
  drivers/          README.md, map, <subsystem>/ + <subsystem>/conf/
  tools/            host and build scripts, checks, provenance list
  docs/             index.md + getting-started/, porting/, devices/, driver-review/, reference/, concepts/
  xnu/ xnu-deps/ xnuloader/ userland/ sysimage/ qemu/ prebuilt/ ci/ hakios-metal/
  Makefile, xnu-build.sh, xnu-env.sh, mkxnuimg.py, VERSION, LICENSE, NOTICE
```

Conventions worth carrying over: a single top-level entry point; a device list in a manifest;
SoC facts in their own layer; kernel driver sources grouped by subsystem with a `map` and `conf`;
review/planning documents under `docs/driver-review/`; boot arguments catalogued in `configs/`;
build outputs in `build/`, never committed.

**Choice for MT6789:** put it in `socs/mt6789/` (reference's shared-SoC location) because the SoC
is not exclusive to the X6886. If strict single-device ownership is preferred, the reference also
allows `devices/x6886/soc/`; that is the one sanctioned alternative, and the exact source file is
the same (`socs/mt6789/soc.toml`). Pick one and record it in `devices/manifest.conf` when that file is
authored. Do not keep copies in both places.

---

## 3. Target tree

```
hakios-x6886/
  README.md                    env.sh                     configs/        (new; placeholder only)
  devices/
    README.md                  manifest.conf              (new; doc only)
    x6886/
      README.md                board.toml                 device.conf     (new; doc only)
      prebuilt/                device-tree.{dts,dtb}      overlays.{dts,dtb}
                               mtkloader.bin              firmware/*.bin
      evidence/dump/           (devices/x6886/evidence/*)
      legacy/                  board.dts  soc.dtsi  README.md  devices/x6886/legacy/darwin16/
  socs/mt6789/soc.toml
  drivers/                     audio/ power/ bluetooth/ camera/ modem/ core/ gpu/ input/ wifi/
  xnuloader/                   (build/loader/*.{c,S,h})
  tools/                       (existing toolchain + all build/check scripts)
  build/                       (output only: README.md + ignored images)
  ios27/  refs/                (unchanged, ignored)
  sysimage -> ios27            (compatibility symlink; see Phase 2)
  docs/
    X6886-PROJECT-REORG.md     (this file), plus:
    getting-started/  porting/  devices/  driver-review/  reference/docs/reference/dump-analysis/  plan/
```

Removed as top-level directories after their contents are moved: `board/`, `device/`, `soc/`,
`kernel/`, `tools/`, `devices/x6886/legacy/darwin16/`, `docs/reference/dump-analysis/`, `devices/x6886/evidence/`, `plan/`.

---

## 4. Exact move / copy table

`git mv` for tracked files, `mv` for ignored/untracked files, `cp` only where the original must
remain as a compatibility shim. All paths are relative to `$ROOT`.

| # | Area | Source | Destination | How | Phase | Note |
|---|---|---|---|---|---|---|
| 1 | SoC layer | `socs/mt6789/soc.toml` | `socs/mt6789/soc.toml` | `git mv` | 2 | The only SoC source of truth. Update `build/boards/x6886/hakios_platform.h` comment that names `soc/`. |
| 2 | Device doc | `docs/DEVICE.md` | `devices/README.md` | `git mv` | 1 | Acts as the device-layer README, matching reference `devices/README.md`. |
| 3 | Device metadata | `devices/x6886/board.toml` | `devices/x6886/board.toml` | keep | — | Canonical device config; no content change here. |
| 4 | Device README | `devices/x6886/README.md` | `devices/x6886/README.md` | keep, links only | 3 | Fix links to `socs/`, `devices/x6886/evidence/`, `docs/driver-review/`. |
| 5 | Board DTS | `devices/x6886/prebuilt/device-tree.dts` | `devices/x6886/prebuilt/device-tree.dts` | `git mv` | 2 | Name matches `devices/parrot/prebuilt/device-tree.dts`. |
| 6 | Board DTB | `board/x6886-base.dtb` | `devices/x6886/prebuilt/device-tree.dtb` | `mv` | 2 | Ignored (`*.dtb`); must be backed up separately. |
| 7 | Overlay DTS | `devices/x6886/prebuilt/overlays.dts` | `devices/x6886/prebuilt/overlays.dts` | `git mv` | 2 | — |
| 8 | Overlay DTB | `board/x6886-overlays.dtb` | `devices/x6886/prebuilt/overlays.dtb` | `mv` | 2 | Ignored. |
| 9 | Loader blob | `build/loader/mtkloader.bin` | `devices/x6886/prebuilt/mtkloader.bin` | `cp` first, `mv` in Phase 4 | 2→4 | Tracked; verifier and repack script point here. Keep a shim until scripts are updated. |
| 10 | WiFi/BT firmware | `devices/x6886/prebuilt/firmware/*.bin` (4) | `devices/x6886/prebuilt/firmware/` | `git mv` | 3 | — |
| 11 | WiFi RAM blobs | `drivers/soc2_2_ram_wifi_1d_1_hdr.bin`, `..._1e_1_hdr.bin`, `WIFI_RAM_CODE_soc2_2_1d_1.bin`, `WIFI_RAM_CODE_soc2_2_1e_1.bin` | `devices/x6886/prebuilt/firmware/` | `git mv` | 3 | 4 files; all firmware now in one device-prebuilt directory. |
| 12 | Superseded DTS | `devices/x6886/legacy/board.dts` | `devices/x6886/legacy/board.dts` | `git mv` | 3 | README already calls this layout superseded. |
| 13 | Superseded DTSI | `devices/x6886/legacy/soc.dtsi` | `devices/x6886/legacy/soc.dtsi` | `git mv` | 3 | Do **not** merge with `socs/mt6789/soc.toml`; different format. |
| 14 | Superseded README | `devices/x6886/legacy/README.md` | `devices/x6886/legacy/README.md` | `git mv` | 3 | — |
| 15 | Device dump | `devices/x6886/evidence/` (17 files, incl. ignored `fdt.dtb`) | `devices/x6886/evidence/` | `mv`, then `git add -A` | 3 | Whole-directory move keeps ignored dump bytes together. |
| 16 | Darwin16 alternate path | `devices/x6886/legacy/darwin16/` (tracked sources + ignored images/blobs) | `devices/x6886/legacy/devices/x6886/legacy/darwin16/` | `mv`, then `git add -A` | 3 | Legacy boot path; `HERE`-relative script paths survive the move. |
| 17 | Driver: audio | `drivers/audio/hakios.x6886_audio.c` | `drivers/audio/hakios.x6886_audio.c` | `git mv` | 3 | — |
| 18 | Driver: battery | `drivers/power/hakios.x6886_battery.c` | `drivers/power/hakios.x6886_battery.c` | `git mv` | 3 | — |
| 19 | Driver: charger | `drivers/power/hakios.x6886_charger.c` | `drivers/power/hakios.x6886_charger.c` | `git mv` | 3 | — |
| 20 | Driver: BT | `drivers/bluetooth/hakios.x6886_bt.c` | `drivers/bluetooth/hakios.x6886_bt.c` | `git mv` | 3 | — |
| 21 | Driver: camera | `drivers/camera/hakios.x6886_camera.c` | `drivers/camera/hakios.x6886_camera.c` | `git mv` | 3 | — |
| 22 | Driver: cellular | `drivers/modem/hakios.x6886_cell.c` | `drivers/modem/hakios.x6886_cell.c` | `git mv` | 3 | — |
| 23 | Driver: keys | `drivers/input/hakios.x6886_keys.c` | `drivers/core/hakios.x6886_keys.c` | `git mv` | 3 | No `input/` for keypad? See note below. |
| 24 | Driver: touch | `drivers/input/hakios.x6886_touch.c` | `drivers/input/hakios.x6886_touch.c` | `git mv` | 3 | — |
| 25 | Driver: GPU | `drivers/gpu/hakios.x6886_mali.c` | `drivers/gpu/hakios.x6886_mali.c` | `git mv` | 3 | **Dirty in working tree** — see §7 pre-flight. |
| 26 | Driver: USB | `drivers/core/hakios.x6886_usb.c` | `drivers/core/hakios.x6886_usb.c` | `git mv` | 3 | Reference has no `usb/`; `core/` is the closest subsystem. |
| 27 | Driver: wifi | `drivers/wifi/hakios.x6886_wifi.c` | `drivers/wifi/hakios.x6886_wifi.c` | `git mv` | 3 | — |
| 28 | Driver notes (11) | `drivers/PORT-{AUDIO,BATTERY,BT,CAMERA,CELL,CHARGER,GPU,KEYS,TOUCH,USB,WIFI}.md` | `docs/driver-review/` (same names) | `git mv` | 3 | `gpu-driver.md` is dirty — see §7. |
| 29 | Loader sources | `build/loader/{start.S,mtkloader.c,ufs.h,ufs_darwin.h,mtkdbg.h,font8x16.h}` | `xnuloader/` (same names) | `git mv` | 2 | `mtkdbg.h`, `font8x16.h` are tracked; all must exist at new path before scripts switch. |
| 30 | Generated board headers | `build/boards/hakios_board_table.h`, `build/boards/x6886/hakios_platform.h` | keep in place; optional shim `devices/x6886/configs/ -> ../../build/boards` | `cp`/symlink | 2 | Ignored but build-critical; generated from `board.toml` + `soc.toml`. Do not move before Phase 4. |
| 31 | Build scripts | `tools/*` (8 files: 5 tracked, 3 untracked) | `tools/` (same names) | `git mv` + `mv` | 4 | `build-kernel-*.sh`, `build-loader-*.sh`, `build-payload-*.py` are untracked. |
| 32 | Image packer | `build/chainboot-x6886.py` | `tools/chainboot-x6886.py` | `git mv` | 4 | No repo-absolute paths; safe once callers updated. |
| 33 | Repack script | `build/repack-magiskboot.sh` | `tools/repack-magiskboot.sh` | `mv` | 4 | Untracked/ignored; back it up first. |
| 34 | Python verifier | `build/verify-x6886-final.py` | `tools/verify-x6886-final.py` | `git mv` | 4 | Update internal path constants in same commit. |
| 35 | C verifier | `tools/verify-x6886-final.c` | `tools/verify-x6886-final.c` | `git mv` | 4 | Update `#define IMAGE/LOADER/LOADER_ELF`. |
| 36 | Build README | `build/README.md` | `build/README.md` | keep, edit links | 4 | Recipe points at old `build/loader`, `soc/`, `board/` paths. |
| 37 | Dump analysis | `docs/reference/dump-analysis/` (12 md files) | `docs/reference/docs/reference/dump-analysis/` | `git mv` | 1 | Documentation only; no build dependencies. |
| 38 | Bring-up plan | `plan/` (11 files, incl. ignored `.patch`? patch is tracked) | `docs/plan/` | `git mv` | 1 | Keep `phase2-ufs-darwin-reference.patch` byte-identical. |
| 39 | Plan doc | `docs/PLAN.md` | `docs/plan/bringup.md` | `git mv` | 1 | Contains stale absolute paths under `Documents/Default Project/...`; fix links only. |
| 40 | Status doc | `docs/STATUS.md` | `docs/plan/status.md` | `git mv` | 1 | — |
| 41 | Driver matrix | `docs/driver-matrix.md` | `docs/driver-review/driver-matrix.md` | `git mv` | 1 | Many source-path references in prose; update links, not claims. |
| 42 | Driver audit | `docs/X6886-DRIVER-AUDIT.md` | `docs/driver-review/audit.md` | `git mv` | 1 | — |
| 43 | Subsystem plans | `docs/X6886-{AUDIO,GPU,PMIC-THERMAL,SENSORS-RESEARCH}.md` | `docs/driver-review/` (same names) | `git mv` | 1 | — |
| 44 | FaceID spec | `docs/FACEID-2D-SPEC.md` | `docs/reference/faceid-2d.md` | `git mv` | 1 | — |
| 45 | App Store research | `docs/APPSTORE-USERSPACE-RESEARCH.md` | `docs/reference/appstore-userspace.md` | `git mv` | 1 | — |
| 46 | General research | `docs/RESEARCH.md` | `docs/reference/research.md` | `git mv` | 1 | — |
| 47 | Hardware notes | `docs/reference/hardware.md` | `docs/reference/hardware.md` | `git mv` | 1 | — |
| 48 | iOS27 handoff | `docs/HANDOFF-IOS27-BOOT.md` | `docs/getting-started/ios27-boot.md` | `git mv` | 1 | — |
| 49 | Super image | `docs/SUPER-IMAGE.md`, `docs/PHASE1-SUPER-PACKAGING.md` | `docs/porting/super-image.md`, `docs/porting/super-image-phase1.md` | `git mv` | 1 | — |
| 50 | Config layer | `configs/README.md` | new placeholder | doc only | 1 | **No existing file is a defconfig.** Content authoring (`x6886_defconfig`, `boot-args.lst`) is explicitly out of scope. |
| 51 | Compatibility link | — | `sysimage -> ios27` | `ln -s` | 2 | Optional; lets references use reference-style `sysimage/` without moving 2+ GB of ignored files. |

Note on #23/#26: the reference has no `usb/` subsystem and puts buttons under `input/`. If the
project prefers exact subsystem semantics, use `drivers/input/hakios.x6886_keys.c` instead of
`drivers/core/...`; either way `kernel/` disappears. Decide once and record it in `drivers/map`
when that file is authored. No file content changes in this proposal.

After Phase 4 the source directories are empty and can be removed with
`rmdir board device soc kernel/x6886 kernel scripts darwin16 dump-analysis device-dump plan`.

---

## 5. Build scripts that must be updated

All of these contain absolute paths or directory assumptions. "Move" refers to table rows
31–35. `refs/`, `ios27/` and `tools/{bin,root}` do **not** move, so their references stay valid
once `$ROOT` is canonical.

| Script | Hardcoded references today | Required change |
|---|---|---|
| `env.sh` | `HAKIOS_ROOT=/home/berucrypt00/x6886-hakios`; `DUMP_ROOT=/home/berucrypt00/otadump`; six absolute `/home/berucrypt00/hakios-x6886/tools/...` variables (`AUTOM4TE`, `M4`, `AUTOM4TE_CFG`, `AC_MACRODIR`, `autom4te_perllibdir`, `AUTOCONF`, `ACLOCAL`, `AUTOHEADER`, `AUTOMAKE`) | Single canonical root; derive all tool paths from `$HAKIOS_ROOT`; add `HAKIOS_SOC=$HAKIOS_ROOT/socs/mt6789`, `HAKIOS_DEVICE=$HAKIOS_ROOT/devices/x6886`. Keep the symlink alias until Phase 4. |
| `tools/build-kernel-x6886.sh` → `tools/` | `ROOT=/home/berucrypt00/hakios-x6886`, `source ./env.sh`, `bash refs/hakios-common/kernel/build.sh` | Source `$HAKIOS_ROOT/env.sh` by absolute path (cwd-independent); `refs/` unchanged. |
| `tools/build-darwin.sh` → `tools/` | `ROOT=/home/berucrypt00/x6886-hakios`, `REPO=$ROOT/refs/hakios-common`, `IOS=$ROOT/ios27`, `bash "$ROOT/tools/fix-ramdisk.sh"` | Last line → `$HAKIOS_ROOT/tools/fix-ramdisk.sh`; rest from `env.sh`. |
| `tools/fix-ramdisk.sh` → `tools/` | `ROOT`, `REPO`, `IOS`, `$REPO/tools/apfs/mkapfs.py` | Only `$ROOT` source and script location; `refs/` unchanged. |
| `tools/build-loader-elf-x6886.sh` → `tools/` | `CLANG/LLD` under `tools/root/usr/lib/llvm-20/bin`; `SRC=$ROOT/refs/oosexgang-xnu27/loader/link.ld`; `-I $ROOT/build/boards/x6886`, `-I $ROOT/build/loader`, `-I $ROOT/refs/.../loader`; sources `build/loader/start.S`, `build/loader/mtkloader.c`; `OUT=/tmp/hakios-x6886/elf-final2` | Include paths → `$HAKIOS_ROOT/devices/x6886/configs` (or keep `build/boards` shim) and `$HAKIOS_ROOT/xnuloader`; sources → `xnuloader/`. Keep the verifier ELF path in sync. |
| `tools/build-payload-x6886.py` → `tools/` | `ROOT`, kernel in `refs/xnu-12377-hakios/BUILD/...`, ramdisk `ios27/root26-darwin.apfs`, output `build/payload-x6886.bin` | Canonical `$ROOT`; inputs/outputs otherwise unchanged. |
| `tools/qemu-darwin.sh` → `tools/` | `ROOT`, `REPO`, `IOS`, kernel in `refs/`, `tools/qemu/QEMU_{CODE,VARS}.fd` | Canonical `$ROOT` only. |
| `tools/run-qemu.sh` → `tools/` | `ROOT`, `UPSTREAM=$ROOT/refs/hakios-common`, `SHELL_ROOT=$ROOT/build/qemu/root.img`, `IOS`, firmware in `tools/qemu/` | Canonical `$ROOT` only; `build/qemu/root.img` must stay. |
| `tools/verify-x6886-final.c` → `tools/` | `#define IMAGE .../build/boot-hakios-x6886-final.img`; `LOADER .../build/loader/mtkloader.bin`; `LOADER_ELF /tmp/hakios-x6886/elf-final2/mtkloader.elf` | Image stays in `build/`; loader → `devices/x6886/prebuilt/mtkloader.bin`; ELF path unchanged. |
| `build/repack-magiskboot.sh` → `tools/` | `CHAIN=/home/berucrypt00/hakios-x6886/build/chainboot-x6886.py`; `MB=/home/berucrypt00/x6886-hakios/tools/bin/magiskboot` | Both → `$HAKIOS_ROOT/tools/...`. |
| `build/chainboot-x6886.py` → `tools/` | No repo-absolute path; only `/tmp/hakios-x6886` scratch | Safe to move unchanged (verify after move). |
| `build/verify-x6886-final.py` → `tools/` | `ROOT`, `IMG`, `LOADER`, `ELF`, `READELF`, `NM`, `START_S=$ROOT/build/loader/start.S`, `PLATFORM_H=$ROOT/build/boards/x6886/hakios_platform.h`, `MTKDBG_H=$ROOT/build/loader/mtkdbg.h`, stock image `/home/berucrypt00/otadump/images/boot.img` | Loader sources → `xnuloader/`; platform header → `devices/x6886/configs/` (or shim); stock image → `$DUMP_ROOT/images/boot.img`. Keep all 13 checks byte-for-byte. |
| `devices/x6886/legacy/darwin16/build-x6886.py` → legacy | `LLVM=/home/berucrypt00/x6886-hakios/tools/root/usr/lib/llvm-20/bin`; `HERE`-relative `shim/`, `tools/`, `payload.bin` | Canonical `$ROOT` for LLVM; `HERE` paths survive the whole-directory move. |
| `devices/x6886/legacy/darwin16/fix_header.py` → legacy | `STOCK=/home/berucrypt00/otadump/images/boot.img`; relative `shim/out/Image`, `payload.bin`, `boot-x6886-final.img` | `$DUMP_ROOT`; relative paths survive. |
| `build/README.md` | Recipe references `build/loader/`, `soc/`, `board/`, `loader/chainboot.py` | Update to `xnuloader/`, `socs/`, `devices/x6886/prebuilt/`, `tools/chainboot-x6886.py`. |
| `docs/driver-matrix.md`, `drivers/gpu-driver.md` | Prose paths to old locations | Link/path text only; claims and evidence unchanged. |

Non-script dependencies to leave untouched: `ios27/*.apfs`, `refs/**`, `tools/{bin,root}`,
`/home/berucrypt00/otadump/images/boot.img`, `build/qemu/root.img`,
`build/boot-*-x6886*.img`, `/tmp/hakios-x6886/elf-final2/mtkloader.elf`.

---

## 6. Keeping the current boot-image build working during the move

Order matters. The current image is produced by: generated board header → `xnuloader` sources →
loader ELF → `mtkloader.bin` → XNUP payload → `chainboot-x6886.py`/`magiskboot` →
`boot-hakios-x6886-final.img`. The verifier reads the final image plus loader sources. Break any
of those paths and the offline check stops, even though nothing is flashed.

**Phase 0 — baseline (no changes).**
- Record `sha256sum build/boot-hakios-x6886-final.img build/loader/mtkloader.bin` and the full
  output of `python3 build/verify-x6886-final.py` (expect 13/13).
- Preserve the dirty working tree before any `git mv`:
  `git status --short` currently shows modified `drivers/gpu-driver.md` and
  `drivers/gpu/hakios.x6886_mali.c`, plus untracked `tools/build-kernel-x6886.sh`,
  `tools/build-loader-elf-x6886.sh`, `tools/build-payload-x6886.py`.
  Commit or `git stash push` first; otherwise a later `git reset` rollback would discard them.
- Back up ignored build inputs: `build/boards/**`, `build/repack-magiskboot.sh`,
  `build/*.img`, `ios27/*.apfs` pointers. Example:
  `tar czf "$HAKIOS_ROOT/../x6886-untracked-backup.tgz" build/boards build/repack-magiskboot.sh`.

**Phase 1 — docs only (rows 1–2, 37–50).** No build input moves. Afterward the verifier must still
be 13/13 with the identical image hash. This phase is fully reversible with `git reset --hard`.

**Phase 2 — add destinations before removing sources (rows 3–16, 29–30, 51).**
- Create `devices/x6886/prebuilt/`, `socs/mt6789/`, `xnuloader/`, `devices/x6886/evidence/`.
- **Copy, do not cut**, loader sources and `mtkloader.bin`: keep `build/loader/` populated until
  every script that names it has been switched. Leave `build/boards/` exactly where it is; it is
  regenerated from the moved TOML files by the upstream generator and is not tracked.
- Add the optional `sysimage -> ios27` symlink; nothing is moved into it.
- Checkpoint: verifier still 13/13, image hash unchanged. At this point old and new paths coexist.

**Phase 3 — code and device layer (rows 17–28).** Driver `.c` files and `PORT-*.md` notes move;
no build script reads these paths in this repository, so the boot-image check is unaffected. The
external XNU build under `refs/` includes drivers by file name via its own tree; if that tree is
ever rebuilt after this phase, confirm its include/copy step is pointed at `drivers/<subsystem>/`
before building. Do not rebuild during the reorg.

**Phase 4 — script cutover and source removal (rows 31–36, 9 final `mv`).**
- Update all scripts in §5 in **one** commit, then switch callers from `build/loader/` to
  `xnuloader/` and `devices/x6886/prebuilt/`.
- Only then delete the compatibility copies in `build/loader/`. Keep `build/boards/` (or replace
  with the `devices/x6886/configs/` shim) because the loader ELF build still needs the generated
  header.
- Re-run the verifier; 13/13 and identical image hash are the gate. If the ELF rebuild is needed,
  rebuild it first and confirm the new ELF reproduces the same loader bytes before regenerating any
  image.
- Finally remove now-empty source directories with `rmdir`.

**Never during the reorganization:** rebuild the final boot image from scratch, regenerate AVB
metadata, run `fastboot`, `adb flash`, change slots, or accept any device-state check as a
substitute for the offline verifier.

---

## 7. Rollback plan

Baseline before Phase 1: create a tag on the clean commit and a tarball of ignored inputs.

```sh
git tag pre-reorg-baseline -m "before X6886 SM6475-shape reorganization"
tar czf "$HAKIOS_ROOT/../x6886-untracked-backup.tgz" \
  build/boards build/repack-magiskboot.sh build/*.img
```

Per-phase rollback (execute in reverse order):

| Phase | Rollback |
|---|---|
| 4 | `git checkout pre-reorg-baseline -- scripts tools build env.sh` (adjust to the files actually changed); restore `build/loader/*` and `build/repack-magiskboot.sh` from the tarball; re-run verifier. |
| 3 | Inverse `git mv` for each `drivers/<sub>/*` and `docs/driver-review/PORT-*.md`; `git checkout pre-reorg-baseline -- kernel/x6886` if renames cannot be reversed cleanly; restore the stashed/committed dirty files. |
| 2 | Remove `sysimage` symlink and `socs/`, `xnuloader/`, `devices/x6886/{prebuilt,evidence}` copies; restore originals from git/tarball; `git checkout pre-reorg-baseline -- soc board device device-dump darwin16 build/loader`. |
| 1 | `git reset --hard pre-reorg-baseline` (docs-only phase; safe once the dirty files from Phase 0 are committed or stashed). |

Global rollback: `git reset --hard pre-reorg-baseline && git clean -fdX` **only** after extracting
the ignored-input tarball; `git clean -fdX` deletes `refs/`, `tools/`, `ios27/` and images and
must never be run casually. Restore the tarball, then re-run `python3 build/verify-x6886-final.py`
and compare its hash with the Phase 0 record. Do not delete the `hakios-x6886` symlink as part of
rollback — it predates this work.

Acceptance after any rollback: 13/13 verifier checks, identical `sha256` of the final image, and
`git status --short` matching the Phase 0 snapshot (including the two modified files).

---

## 8. Risk register

| Risk | Severity | Mitigation |
|---|---|---|
| Ignored build inputs are invisible to git (`build/boards`, `build/loader`, images, `ios27`, `refs`, `tools`) | High | Tar backup in Phase 0; copy-before-cut in Phase 2; hash gate at every phase. |
| Two root aliases (`hakios-x6886` symlink vs `x6886-hakios` real path) cause half-updated scripts | High | Keep the symlink until Phase 4; canonicalize all variables through `env.sh`; grep both spellings before sign-off. |
| `mtkloader.bin` / loader sources move before verifier and packer are updated | High | Move last (Phase 4), with a shim copy retained until then. |
| Dirty tracked files (`gpu-driver.md`, `hakios.x6886_mali.c`) are lost on rollback | Medium | Commit or stash in Phase 0; record `git status --short` output. |
| Untracked scripts (`tools/build-kernel-x6886.sh`, `build-loader-elf-x6886.sh`, `build-payload-x6886.py`, `repack-magiskboot.sh`) are not covered by `git mv` | Medium | Stage them before moving, or move with `mv` and back up. |
| External XNU build includes driver files by path and is not part of this repo | Medium | Do not build during reorg; re-point the external include step before any future kernel build. |
| Documentation links and prose path references go stale | Low | Link-only edits in Phase 1; no claims or evidence altered. |
| SoC ownership ambiguity (`socs/mt6789/` vs `devices/x6886/soc/`) | Low | Pick one in Phase 2; never keep two copies. |
| Reorg is mistaken for permission to flash | High | This document and every phase gate explicitly prohibit device access; verify offline only. |

---

## 9. Out of scope

No code copied from the SM6475 reference; no code written; no new defconfig, `drivers/map`,
`device.conf` or `manifest.conf` **contents** authored (directories/placeholders only); no build
run; no boot image regenerated; no flashing, `fastboot`, `adb`, slot changes or any device access;
no edits to `refs/`, `ios27/`, or the toolchain under `tools/`. This document is the only file
written by this research task.
