# Bring-up plan

This page lists the gates in order. A gate is complete only when its evidence
is in the repository or in a log that is quoted verbatim. Nothing here
authorizes a partition write.

## Gate 0 — host toolchain

Complete.

- Toolchain under `tools/root/` and `tools/bin/`: LLVM 20, lld 20, QEMU 8.2,
  dtc, cross gcc, magiskboot, adb, fastboot.
- `env.sh` derives the repository root from its own path.
- The offline verifier is the release gate.

Verification:

```sh
python3 tools/verify-x6886-final.py
# RESULT: 13/13 checks passed
```

## Gate 1 — XNU without Apple userland

Complete.

- Kernel source: `refs/xnu-12377-hakios` with the X6886 overlay.
- Small APFS root: `ios27/root26-darwin.apfs`.
- QEMU scripts: `tools/qemu-darwin.sh`, `tools/run-qemu.sh`.

Keep QEMU guest memory at 1024M or 1536M with
`QEMU_ACCEL='tcg,tb-size=128'` on the current host. The upstream 6G default
exceeds available RAM.

## Gate 2 — iOS 27 inputs

Complete as local inputs, not committed.

- System image: `ios27/system27-prune.apfs`.
- RAM disks: `ios27/flashboot.apfs`, `ios27/root26-darwin.apfs`.
- Userland patches and job list live in the ignored `ios27/` and `refs/`
  trees. Build scripts reproduce them.

## Gate 3 — X6886 board data

Complete in `devices/x6886/`.

| File | Role |
| --- | --- |
| `board.toml` | Device configuration |
| `prebuilt/device-tree.dts` | Board DTS from the dump |
| `prebuilt/overlays.dts` | overlay DTS from the dump |
| `evidence/` | Dump captures and device observations |
| `legacy/` | Superseded DTS and Darwin 16 attempt |

## Gate 4 — loader and storage

Complete to the point of a read-only Darwin root.

- Loader sources: `xnuloader/`.
- UFS block server publishes the APFS container as `/dev/ufs_darwin`.
- Loader publishes `md0` and `md1` from the two RAM disks.
- `MOUNT_DIAG ok=1` proves the APFS super mount from UFS.

## Gate 5 — root switch and launchd

Work in progress.

- `vfs_switch_root` swaps the system volume to `/` and the boot volume to
  `/private/preboot`.
- `EXEC_DIAG mode=1 argv1=/sbin/launchd.darwin` proves the handoff to Apple's
  launchd.
- Remaining blockers are in userland: cryptex fallback, shared region, CPU
  speed, and the display/graphics handover.

## Gate 6 — device write

Not started. No partition write is authorized by this document.

Read-only checks first: `fastboot getvar all`, `fastboot oem device-info`,
by-name partition listing, and a comparison of `lk_a`/`lk_b`, `tee_a`/`tee_b`,
`vbmeta_a`/`vbmeta_b`.

Before any write, resolve the three hard gates in
[boot-path-decision.md](boot-path-decision.md): AVB state, PayJoy/P7 state,
and a local rescue path.
