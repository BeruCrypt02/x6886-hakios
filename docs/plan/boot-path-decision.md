# Boot-path decision

The U-Boot chainload was studied as a way to get unrestricted `fastboot boot`
on the X6886. This page records the decision without authorizing a device
write.

## Proposal

Leave stock LK untouched, put U-Boot in `boot_b`, switch to slot B, and use
U-Boot's fastboot to load the kernel and system image from RAM. That would
avoid patching LK, but the exact recipe is not proven on this phone.

## Findings

| Claim | Verdict | Evidence |
| --- | --- | --- |
| MT6789 U-Boot exists and builds | true | `mnnoee/u-boot-mtk-g99`, `sysretq0/loki` |
| UFS blocks the port | false | mainline `ufs-mediatek.c` matches the dump's `mediatek,mt8183-ufshci` |
| Flashing only `boot_b` switches slots | false | slot state is changed by `set_active`, not by writing a partition |
| LK accepts Android boot header v4 | true | dump analysis, page 4096, gzip/LZ4 kernels |
| `boot_b` is writable | unknown | stock scatter marks it `NONE`/`INVISIBLE`/`is_download:false` |

## Hard gates

1. **AVB**: vbmeta has a boot chain descriptor; boot.img carries embedded
   vbmeta. Device state is `locked` / `green` / `enforcing`. A modified boot
   image needs a signing key or an explicit unlock.
2. **PayJoy / P7**: LK contains `check_payjoy_flag` and
   `INFO: fastboot disabled by payjoy!!!`. Whether the flag is active requires a
   read-only device check.
3. **Rescue path**: no local `mtkclient`, `avbtool`, `pyserial`, or `pyusb`
   install is staged. Without a rescue path, a failed write can strand the
   phone.

## Decision

Do not write to the phone for the U-Boot path yet.

Safe work in parallel:

- Stage rescue tooling locally without contacting the device.
- Continue the resident-loader path, which has already reached XNU and
  launchd without modifying LK or the slot state.
- Keep the board evidence and offline verifier as the source of truth.

Any future write needs explicit approval, a verified current slot, a tested
rollback image, and a decision on signing versus unlock.
