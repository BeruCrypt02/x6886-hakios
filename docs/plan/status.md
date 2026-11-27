# Current status

Updated 2026-11-27. This page records verified boot progress, not a promise
that the phone is usable as a daily driver.

## Boot gate

| Gate | Evidence | State |
| --- | --- | --- |
| Host verifier | `RESULT: 13/13 checks passed` | complete |
| XNU entry | `JUMPED to XNU entry` in pstore | complete |
| UFS block server | `ufs: 1 partitions ... /dev/ufs_darwin` | complete |
| APFS super mount | `MOUNT_DIAG ok=1` | complete |
| Root switch | `vfs_switch_root ... error: 0`, `SWITCH_ROOT_DIAG ok=1` | complete |
| launchd exec | `EXEC_DIAG mode=1 argv1=/sbin/launchd.darwin` | complete |
| Home screen | no verified screenshot or log | open |

## Userland findings

| Finding | Meaning |
| --- | --- |
| `failed to query hardware target: 22` | fixed by the kernel `hw.target` fallback to `D22AP` |
| `the OS cryptex from ...: No such file or directory` | expected on this image; the inline shared-cache bind is the fallback |
| `skipping preserved mountpoint ... error: 2` | optional iOS volumes absent from the pruned system image |
| `8 checks, 2 failed` | the two cryptex checks; not fatal |
| `[X6886-mali] STATUS SURVEY-ONLY` | driver reads clocks but makes no unproven writes |

## Host and device

- Host: x86_64 Linux, about 7.7 GiB RAM. QEMU guest memory stays at 1024M or
  1536M; the upstream 6G default is too large.
- Dump: `/home/berucrypt00/otadump`, local and ignored by git.
- Phone: slot A runs ColorOS 17. Slot B parity has not been checked.
- Stock LK rejects `fastboot boot` with `unknown command`.

## Next work

1. Confirm launchd progress past `EXEC_DIAG` with a fresh pstore capture.
2. Check the shared region and `libSystem` mapping in the boot log.
3. Add `cpufreq=1` and remove `display-scale=2` from the boot configuration.
4. Drop high-cost jobs that do not work on this hardware.
5. Keep Android as the system of record until a home screen is verified.
