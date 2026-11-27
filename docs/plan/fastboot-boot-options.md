# `fastboot boot` on Infinix X6886 / stock Transsion LK — investigation report

Read-only investigation. No device contact. No writes to `otadump`, `device-dump`, or any
port-tools path. The only files created are analysis scripts and a staged Python package under
`/tmp/hakios-x6886/`, plus this report.

Route: `aosp-root-router` → `aosp-bootloader-lk`.

## 1. Definitive answer

**No. `fastboot boot` cannot be enabled on this LK without replacing or patching LK itself,
which requires a flash.** There is no OEM command, property, or unlock state that turns it on.

VERIFIED: the stock LK's fastboot command table contains **no `boot` entry**. It has exactly
these registered prefixes (decoded from `fastboot_register()` calls at file offsets in the
extracted `lk` payload; add `0x200` for offsets in the wrapped `lk.img`):

| Registered prefix | call site (extracted payload) | handler |
|---|---|---|
| `getvar:` | `0x89e8` | `0x90a0` |
| `reboot` | `0x8a04` | `0x9168` |
| `reboot-bootloader` | `0x8a20` | `0x9194` |
| `reboot-fastboot` | `0x8a3c` | `0x91c8` |
| `reboot-recovery` | `0x8a58` | `0x92c4` |
| `continue` | `0x8a74` | `0x9364` |
| `set_active:` | `0x8a90` | `0x92d0` |
| `download:` | `0xa858` | (table-driven) |
| `flash:` | `0xa874` | (table-driven) |
| `erase:` | `0xa890` | (table-driven) |
| `oem ultraflash:` | `0xa8b0` | (table-driven) |
| `oem ultraflash_en` | `0xa8cc` | (table-driven) |
| `ultraflash:` | `0xa8e4` | (table-driven) |

`fastboot boot` sends a `download:` (accepted) followed by `boot` — which is not in this table,
so LK answers `unknown command`. This exactly matches the observed
`FAILED (remote: 'unknown command')`.

INFERENCE (high confidence): the `boot` command was deliberately removed by Transsion/MTK;
this is not a transient gate. The exact same failure is documented across MTK devices,
including Transsion (see sources).

INFERENCE (high confidence): the only ways to add it are (a) patch the LK binary to register a
`boot` handler and flash `lk_a`/`lk_b` with a valid signed/accepted image, or (b) boot a
different bootloader (U-Boot/ABL) in place of LK. Both require a flash of a bootloader
partition, which this task forbids. Penumbra/fenrir-style LK patches do exactly this — they do
not magically enable RAM boot in the running LK.

## 2. Why the other messages appear

VERIFIED (strings + disassembly of the extracted `lk` payload; offsets are in wrapped `lk.img`):

| String | offset in `lk.img` | decoded role |
|---|---|---|
| `unknown command` | `0xbe21d` | default dispatcher response |
| `No support by lock control` | `0xbe202` | status → message mapper, lock branch |
| `No support by security control` | `0xa787d` | status → message mapper, security branch |
| `command parameter is not allowed!` | `0xa469e` | **belongs to the `set_active:` handler**, not to `boot` |
| `INFO: fastboot disabled by payjoy!!!` | `0xa89d0` | PayJoy path string |
| `check_payjoy_flag` | `0xa2e76` | TKV-backed PayJoy flag check |
| `allow_fastboot` / `Include transsion flag allow to fastboot !!!` | `0xb061b` / `0xb3a55` | Transsion flag strings |
| `boot-fastboot` | `0xb0581` | string present, **not registered** |
| `boot` | `0xac136` | string present in a string pool next to `arch/arm64/mmu.c` and `frp paritition size`; it is a partition/debug token, **not** a registered command |

The `continue` handler (`0x9364`) calls the status/fail helpers at `0x8204` and `0x8234`; the
status mapper selects `No support by security control` from an internal state word at
global `+0x138` (LK image base `0xffff000050f00000`, from `lkpatcher`). So the observed
`fastboot continue → No support by security control` is an internal LK security-state refusal,
not a missing command.

INFERENCE (medium confidence): the security-state refusal is fed by the PayJoy/TKV
(`check_payjoy_flag`) and/or Transsion `allow_fastboot` logic. Whether the PayJoy flag is set
on this unit cannot be determined from the dump; it would require a read-only on-device check
that this investigation was explicitly told not to perform.

CORRECTION to prior docs: `command parameter is not allowed!` is emitted by the `set_active:`
handler (it compares the argument against `"a"` and `"b"` at `0x92dc–0x934c`). It is **not**
evidence of a gated `boot` command.

Also note: `plan/DECISION.md` and `plan/lk-bootb-feasibility.md` describe the device as
`locked / green / enforcing`. The live state supplied for this investigation is
`unlocked:yes, secure:no`. Those docs may be stale on that point; treat the live report as
authoritative.

## 3. Every `oem` string in the LK image (wrapped `lk.img` offsets)

```
  a2d3c [FASTBOOT] oem p2u off!
  a2d55 oem cdms
  a8539 [FASTBOOT] oem p2u on!
  a8551 [FASTBOOT] oem printk-ratelimit on!
  ac1ba oem ultraflash_en
  ad2ad oem tran_skip_confirm_key
  af0f1 oem printk-ratelimit
  b0603 oem tran_unlock
  b218d oem dump_pllk_log
  b3412 oem off-mode-charge
  b3426 oem mrdump_chkimg
  b39ea oem set_enckey
  b498e oem get_key
  b688e [FASTBOOT] oem printk-ratelimit off!
  bef87 oem usb2jtag
  c1213 oem mrdump_fallocate
  c18ff oem ultraflash:
  c38db oem mrdump_out_set
  c38fc oem get_socid
  c3f47 transsion oem unlock failed
  c4c45 oem p2u
```

The same pool is duplicated in the `aee` sub-image at roughly `+0x1ad000` (e.g. `[FASTBOOT] oem p2u on!` at `0x253ee5`).

VERIFIED: of all of these, only **`oem ultraflash:` and `oem ultraflash_en`** are actually
registered in the main fastboot command table. There is no plain `oem` catch-all, no
`oem boot`, no `oem fastboot`, and no `oem allow_fastboot` command registered.

INFERENCE: the other `oem ...` strings are either leftovers from a shared MTK/Transsion source
base, or belong to a secondary dispatch path not reachable from the standard fastboot table on
this build. None of them is a RAM-boot enabler.

## 4. Local reference ports

VERIFIED — `refs/oosexgang-xnu27/docs/porting.md`:
- line 27: requires "a way to write one partition (`fastboot flash boot_a`)".
- lines 33–35: recovery/fastbootd live inside `boot`; a `boot_a` that hangs before LK's keys
  are read leaves no fastboot; their rescue was **BootROM download mode with mtkclient**
  (`mtk.py w boot_a <the rooted image>`). They explicitly say to check BootROM reachability
  **before** the first flash. Slot B is never written.
- lines 42–56: chainloader lives inside `boot_a` as Android's "kernel"; it defaults to Android
  and only starts XNU on the `HAKIOS:BOOT-XNU` marker in ramoops.
- lines 64–65: "A boot from RAM alone (`fastboot boot` of such an image, nothing written) has
  not been made to work with this loader: every run writes `boot_a`."
- lines 56–57: an image that always starts XNU with no watchdog is how the first phone got
  trapped — do not use `-DMENU_DEFAULT_XNU=1` on device.

VERIFIED — `refs/akita/README.md`:
- line 24 and lines 30–35: Akita boots via **Google's bootloader** with `fastboot boot` of a
  U-Boot image (header v4, U-Boot as an arm64 Image). That works because it is a Pixel-style
  Google bootloader with a real `boot` command. It is **not** applicable to Transsion LK.
- No Transsion LK trick is documented in either ref tree.

## 5. Online sources (real URLs only)

- https://github.com/MiCode/Xiaomi_Kernel_OpenSource/issues/2356 — "fastboot boot command is
  bugged on Xiaomi MTK devices". Comment #3 (lattice0, Poco M3) reports the exact
  `Booting FAILED (remote: 'unknown command')`; comment #6: "It seems that Mediatek does not
  want to add `fastboot boot`"; comment #2: workaround is `fastboot flash boot` then restore.
- https://xdaforums.com/t/tutorial-how-to-install-twrp-on-device-that-does-not-have-recovery-partition.4572311/
  — Kelexine: "it doesn't work on most mediatek based devices, especially Transsion Devices
  (Tecno, Infinix and Itel)"; user with Infinix Note 10 Pro reports temporary `fastboot boot`
  failing and needing mtkclient to recover.
- https://blog.r0rt1z2.com/posts/reverse-engineering-mediatek-lk/ — describes MTK LK v2
  (AArch64, MMU on, image base from `lkpatcher`), and the `lkpatcher` tool used above.
- https://github.com/R0rt1z2/lkpatcher — used read-only to extract the `lk` sub-image and get
  base `0xffff000050f00000`.
- https://github.com/R0rt1z2/fenrir — PoC secure-boot exploit; patches `bl2_ext`/LK and its
  payload can "register custom fastboot commands". It **flashes a patched LK** to do so, is not
  ported to X6886, and the repo was deprecated/archived on 2026-09-20. X6886 is not in its
  supported-device table (it lists Tecno LG7n/LG8n/LH7n and Xiaomi xaga/duchamp/rodin/dash).
  Not a drop-in solution.
- https://github.com/shomykohai/penumbra and https://penumbra.itssho.my/ — see §6.

Negative result: no source was found documenting an OEM command that enables `fastboot boot`
on Transsion LK. That is consistent with the disassembled command table.

## 6. Penumbra

VERIFIED from https://github.com/shomykohai/penumbra and its docs
(`.../Penumbra/Antumbra/CLI`, `.../Penumbra/Antumbra/Install`, `.../Penumbra/FAQ`,
`.../Mediatek/Common/DA/Download-Agent`, `.../Mediatek/Exploits/Carbonara`):

- **(a) Enable `fastboot boot`: NO.** Penumbra/Antumbra is a BootROM/DA-mode flash and readback
  tool (`antumbra pgpt`, `read`, `write`, `erase`, `seccfg`, `peek`). It never touches the
  running LK's fastboot command table and has no fastboot-server functionality. It cannot add
  a `boot` command.
- **(b) Guaranteed rescue if `boot_a` fails: NOT GUARANTEED — best available, conditional.**
  It can write `boot_a` from DA mode:
  `antumbra write boot_a <stock.img> --da <DA.bin>`, then `antumbra reboot normal --da <DA.bin>`.
  It only works if the MT6789 BootROM download path is reachable, a compatible DA is available,
  and the device's DAA/SLA/DA-SLA auth is satisfied. The FAQ explicitly says SLA/DAA cases need
  official DA, engineering preloader, or paid auth, and that devices cannot be brute-forced
  through those errors. Carbonara is patched on post-2024 DA versions, so do not assume it is
  available here.
- VERIFIED: no Penumbra binary or checkout was found locally under `/home`, `/opt`, `/tmp`,
  `/root`, `/srv`, `/media`, `/mnt` (search depth 9; no `*penumbra*` path other than nothing).
  The claim "already on the operator's machine" did not hold in this investigation. Obtaining
  and staging it would need a separate approved step.

## 7. Safest path to test XNU, given the constraint

1. **Preferred — no phone at all.** Keep XNU bring-up in QEMU (the akita ref already runs its
   loader/kernel in QEMU with `virtualization=on`). This is the only path with zero boot_a risk.
2. **If a device run is required:** use the oosexgang chainloader design in a **new `boot_a`
   image that still boots Android by default** (XNU only via the `HAKIOS:BOOT-XNU` ramoops
   marker). Never build with `-DMENU_DEFAULT_XNU=1`. Keep the watchdog/return path enabled so a
   hung XNU run returns to Android.
3. **Before the first flash, stage rescue and prove it read-only:** confirm BootROM USB VCOM
   enumeration, have the stock `boot.img` and a backed-up live `boot_a` on the host, and have a
   working DA + auth path. Without this, do not flash.
4. `fastboot boot` is not part of any safe path here — it does not exist on this LK.

## 8. Exact recovery procedure if `boot_a` is flashed and fails

Stock known-good image on host: `/home/berucrypt00/otadump/images/boot.img`
(67,108,864 B; Android header v4, page 4096, LZ4-legacy kernel; verified in
`plan/lk-bootb-feasibility.md`).

A. **Fastboot still reachable** (LK fastboot runs before the kernel, so hardware-key entry may
still work even if Android never starts):
```sh
fastboot devices
fastboot getvar current-slot          # expect _a; record it
fastboot flash boot_a /home/berucrypt00/otadump/images/boot.img
fastboot reboot
```
If a live backup was taken, prefer that backup over the dump image. Do not flash `lk*`,
`vbmeta*`, or `super` during recovery.

B. **Fastboot NOT reachable — BootROM download mode (the documented Transsion rescue):**
1. Power off. Enter MTK BootROM download mode (button combination or test point). Confirm the
   host sees the MTK USB VCOM device (`0x0e8d:0x0003`).
2. Read-only sanity check first:
   ```sh
   ./antumbra pgpt --da <compatible_DA.bin>
   ```
3. Restore:
   ```sh
   ./antumbra write boot_a /home/berucrypt00/otadump/images/boot.img --da <compatible_DA.bin>
   ./antumbra reboot normal --da <compatible_DA.bin>
   ```
   (`mtk.py w boot_a <image>` is the equivalent mtkclient command used by the oosexgang port.)
4. If DAA/SLA/auth errors appear (Penumbra FAQ: `0x7017`, `0x7024`, `0x1D0D`, "No signer
   available", DA SLA rejection), stop — that is precisely why reachability must be proven
   **before** the first flash, not after a failure.

C. **A/B note:** if `boot_b` is genuinely present and bootable on this unit, an alternative is
to leave `boot_a` untouched and test in B. Prior scatter analysis says `lk_b`/`vbmeta_b` are
`NONE` and slot B writability is unconfirmed, so verify on-device before relying on this.
Never change the active slot without explicit approval.

## 9. Claim ledger

| # | Claim | Status |
|---|---|---|
| 1 | LK command table has 13 distinct registered prefixes, no `boot` | VERIFIED (disassembly of all `fastboot_register` calls) |
| 2 | `fastboot boot` therefore returns `unknown command` | VERIFIED (mechanism) + matches live observation |
| 3 | No oem command enables RAM boot | VERIFIED (command table) + INFERENCE (no external source found) |
| 4 | `continue` refusal comes from an internal security-state branch | VERIFIED (status mapper + handler) |
| 5 | That state is PayJoy/TKV or Transsion `allow_fastboot` | INFERENCE (medium) |
| 6 | `command parameter is not allowed!` is the `set_active:` parameter check | VERIFIED (handler at `0x92d0`) |
| 7 | MTK/Transsion LKs generally lack `fastboot boot` | VERIFIED via Xiaomi issue #2356 + XDA thread |
| 8 | oosexgang loader always writes `boot_a`; no RAM-boot trick | VERIFIED (local doc lines 64–65) |
| 9 | Akita's `fastboot boot` relies on Google bootloader, not Transsion LK | VERIFIED (local README) |
| 10 | Penumbra cannot enable `fastboot boot` | VERIFIED (docs + tool scope) |
| 11 | Penumbra can restore `boot_a` from BootROM DA, conditionally | VERIFIED (CLI docs); availability on this unit INFERENCE |
| 12 | Penumbra is already installed locally | NOT VERIFIED — search found none |
| 13 | BootROM reachability must be proven before first flash | VERIFIED (oosexgang doc + Penumbra FAQ) |

## 10. What was NOT done

No fastboot command was sent. No flash, slot change, unlock, or reboot was performed. No file
under `/home/berucrypt00/otadump` or any port-tools path was modified. No phone was contacted.
