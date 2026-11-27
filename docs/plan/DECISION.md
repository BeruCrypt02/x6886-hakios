# X6886 boot path — decision after full research

Four subagents researched the U-Boot chainload plan (2026-11-23). All reports
are in `plan/`. This file is the decision input. Nothing here was executed on
the phone; no flash, no slot change, no writes.

## The proposal we tested

Gemini's suggestion: leave stock LK untouched, flash U-Boot to `boot_b`, switch
to slot B, and use U-Boot's full fastboot including `fastboot boot` from RAM.
That would avoid patching LK and give an unrestricted fastboot over USB.

## Verdict: the idea is sound in spirit, but the exact recipe is wrong and three hard gates block it

### What is TRUE
- MT6789 U-Boot exists and builds (`mnnoee/u-boot-mtk-g99`, X676B; also the
  `sysretq0/loki` + `sysretq0/advan-x1-uboot` line, upstream U-Boot 16 patches).
- UFS is not a blocker: mainline `drivers/ufs/ufs-mediatek.c` matches our DT's
  `mediatek,mt8183-ufshci` exactly; LK already runs a full UFS stack.
- If the payload is delivered over USB with `fastboot boot`, U-Boot needs no
  UFS driver at all.
- Mechanically, LK accepts Android boot header v4, page 4096, LZ4-legacy or
  gzip kernel, loads at ~`0x40010000`, passes `x0 = DTB`, MMU off.

### What is WRONG in the proposal
- No working project puts U-Boot in `boot_b` as a "kernel". `mnnoee` replaces
  `lk_a`/`lk_b`; `loki` chainloads from a *patched* stock LK. Both keep a stock
  LK backup. The "flash boot_b only" path is unproven.
- Flashing `boot_b` alone does not change the active slot. Current slot is
  `_a`; `set_active b` is forbidden without explicit approval.

### The three hard gates (all verified from the dump)
1. **AVB**: vbmeta has a `boot` chain descriptor; `boot.img` has an embedded
   vbmeta. Device is `locked` / `green` / `enforcing`. A modified boot image
   lands in Red State before LK jumps. Needs the signing key or an unlock.
2. **PayJoy / P7**: LK has `check_payjoy_flag` and
   `INFO: fastboot disabled by payjoy!!!` (TKV-backed, not a property). Whether
   it is active now is unknown without a read-only device check.
3. **No local rescue**: `mtkclient`/`mtk.py`, `avbtool`, `pyserial`, `pyusb`
   are absent. If fastboot gets disabled by a failed write, there is no
   recovery tool staged. `fastboot`/`adb` exist only under
   `~/Desktop/platform-tools-latest-linux/platform-tools`, not in PATH.

Also: `boot_b`/`vbmeta_b` are marked `NONE`/`INVISIBLE`/`is_download:false` in
the stock scatter, so even their writability is unconfirmed.

## What we can do without touching the phone (safe now)

1. **Stage the rescue tooling**: install `mtkclient` + `pyserial`/`pyusb` in a
   local venv/micromamba env, no sudo (mirrors the toolchain install). Verify
   it can enumerate the MT6789 USB download device without writing.
2. **Build a U-Boot image offline** for X6886 using `mnnoee/u-boot-mtk-g99` as
   a base, with our verified constants: UFS at `0x11270000`, GICD/GICR
   `0x0c000000`/`0x0c040000`, LK FB `0xfe0c0000`, ramoops `0x48090000`,
   kernel load `0x40010000`. Pack as Android boot v4 (LZ4 legacy). Do not flash.
3. **Write the bring-up plan** with explicit gates, each requiring Beru's
   approval: read-only PayJoy/slot check → stage rescue → unlock path decision
   (signing key vs `oem unlock`) → only then any write, to slot B only, with
   `current-slot=_a` verified before and after.
4. **Continue the actual port work in parallel**: the iOS userland already
   boots in QEMU; the panel init blob (254 DCS commands) and FT3683G protocol
   are decoded. A first display driver path can be prepared and tested in
   QEMU without the phone.

## What needs Beru's explicit decision before any device write

- Which unlock path: signing key for our boot image, or `fastboot oem unlock`
  (wipes userdata — forbidden unless explicitly approved), or abandon the
  U-Boot plan for the `loki` patched-LK path (needs an LK backup).
- Whether to stage `mtkclient` now (read-only install, no phone contact).
- Whether QEMU-first or device-prep-first is the priority.

## Recommendation

Do steps 1 and 4 now (safe, parallel). Get the rescue tool staged and advance
the software side in QEMU. Do not flash, do not change slot, do not touch
PayJoy state. Treat the U-Boot path as one candidate, not the settled plan,
until the read-only PayJoy/slot check and an unlock-path decision exist.
