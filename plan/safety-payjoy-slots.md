# Safety analysis — PayJoy/P7, A/B slots, and the U-Boot → `boot_b` plan
**Target:** Infinix X6886 (MT6789, eMMC, A/B, stock Transsion LK + PayJoy/P7)
**Scope:** READ-ONLY evidence collection from local dumps only. No device access, no writes to dumps, no execution of any adb/fastboot command.
**Sources allowed/used:** `/home/berucrypt00/otadump` (stock dump), `/home/berucrypt00/hakios-x6886/device-dump` (live-derived text captures), project tree `tools/` (none found), system `PATH`.
**Explicitly NOT accessed:** `/home/berucrypt00/Documents/port-tools` (forbidden by task), any phone connection.

**Label convention**
- **VERIFIED** — directly observed in a named local file/line or PATH check.
- **INFERENCE** — reasoned from strings + standard A/B / fastboot semantics; requires runtime confirmation before it can be treated as fact.
- **UNKNOWN** — cannot be determined from dumps; must be checked on-device (documented, not executed).

---

## 0. Executive summary (severity-ranked)

| # | Severity | Finding | Status |
|---|----------|---------|--------|
| F1 | **CRITICAL** | When the PayJoy flag is active, LK prints `INFO: fastboot disabled by payjoy!!!`. That removes the *only* software flash path we plan to use. Whether it kills all fastboot or only unlock/oem commands is not provable from strings. | Strings VERIFIED / scope INFERENCE / current state UNKNOWN |
| F2 | **CRITICAL** | No local download-mode rescue tool is staged: no `mtkclient`/`mtk.py` in project `tools/` or `PATH`; `adb` and `fastboot` are also absent from `PATH`. Loss of fastboot + loss of slot A = no local recovery. | VERIFIED |
| F3 | **HIGH** | The lock is not a Android property: LK reads a **TKV-backed** flag (`tran_tkv_get_payjoy_flag`, `check_payjoy_flag`). `ro.vendor.tran.anti.crack.p7.support=1` is only *compile-time feature support*, not proof the device is locked. | Mechanism INFERENCE / props VERIFIED |
| F4 | **HIGH** | An Android-side runtime service (`paytrigger_hal_service`, RPMB/TEE-backed) can set/clear device-lock info. A "locked" state can therefore be produced while Android is running, without touching LK. | VERIFIED (files + running prop) |
| F5 | **MEDIUM** | AVB is still locked/enforcing in the captured state (`locked`, `green`, `enforcing`, `ro.boot.flash.locked=1`, ARB versions `6`). A raw U-Boot in `boot_b` is not a valid `boot.img`; slot B will fail verification regardless of which slot is active, unless vbmeta verification is disabled for that slot too. | props/strings VERIFIED / outcome INFERENCE |
| F6 | **MEDIUM** | `boot_b` is **not provisioned** in the stock flash package (`file_name: NONE`, `is_download: false`, `operation_type: INVISIBLE`). Do not assume it currently holds a known-good stock boot image; verify on-device. | VERIFIED |
| F7 | **INFO** | There is **no `recovery` partition**. Recovery components live in `vendor_boot` (`recovery_ramdisk`), while `boot.img` is GKI with an empty ramdisk. Keeping slot A active preserves the A-side recovery path. | VERIFIED |
| F8 | **VERIFIED** | Flash to `boot_b` alone does **not** change the active slot. LK exposes a separate explicit `set_active:` / `set_active_slot` command; no auto-switch evidence exists. | VERIFIED |

**Bottom line:** the plan is survivable *only* if (a) slot A stays active and untouched, (b) fastboot is confirmed reachable before any write, and (c) an offline MTK download-mode rescue tool is staged **before** the first flash. The single most dangerous failure mode is in §7.

---

## 1. PayJoy / P7 — every exact string, file, and line

### 1.1 LK (`/home/berucrypt00/otadump/images/lk.img`)
Extracted with `strings -n 4`; line numbers below are positions in that `strings` output (stable for this image).

| strings-line | Exact string | Meaning |
|---|---|---|
| 1770 | `[tran] tran_tkv_get_payjoy_flag is failed` | LK attempts to read a **payjoy flag via a TKV getter**. |
| 1771 | `check_payjoy_flag` | Named check routine. |
| 2089 | `[tran] tran_tkv_get_ext is NULL` | TKV extension table lookup. |
| 2717 | `[tran] %s: tran_tkv_value memcpy success` | TKV value consumed by LK. |
| 4958 | `tran_tkv_get_ext` | Exported TKV accessor. |
| 2552 | `INFO: fastboot disabled by payjoy!!!` | **The lock symptom.** |
| 2553 | `%s:%d: [tran] check age flag enter normal boot` | Same code region; lock/age-flag check happens on normal boot path. |
| 4081, 9342 | `[tran] check age flag enter normal boot` | duplicate/other copy. |
| 3947, 16185 | `set_active_slot` | explicit slot switch routine |
| 1966, 14651 | `current-slot` | fastboot getvar |
| 1667, 14418 | `slot-count` | fastboot getvar |
| 2117, 14774 | `set_active:` | fastboot command parser |
| 3615+ | `flashing get_unlock_ability`, `flashing unlock`, `flashing lock`, `oem tran_unlock`, `Prohibit unlock operation`, `unlock_ability is false/true`, `[UNLOCK_VFY] auth ok/fail`, `SEC_UNLOCK`, `is_unlock_allowed = 0x%x` | unlock/auth machinery is separate from the payjoy check |
| — | `[ANTI_ROLLBACK] magic mismatch, tran_devctrl.magic = 0x%x`, `anti_rollback_ver`, `[SEC] img is rollbacked (img: %d, efuse: %d)`, `[SEC] ARB Check Fail (0x%x)`, `[SEC] Bypass ARB check for %s`, `Error getting rollback index from AVB_GROUP`, `Error getting rollback index from RECOVERY_GROUP` | anti-rollback is enforced in LK against efuse/AVB rollback indexes |

`payjoy` appears **3 times** total in LK strings. `images/lk.img` is 3,149,892 bytes.

### 1.2 `odm/etc/build.prop`
Exact path: `/home/berucrypt00/otadump/odm/etc/build.prop`

```
185: ro.vendor.tran.arb_group_0_version=6
186: ro.vendor.tran.arb_group_1_version=6
187: ro.vendor.tran.arb_pl_version=6
188: ro.vendor.tran.arb_avb_img_version=6
189: ro.vendor.tran.anti.crack.p7.support=1
```

Live confirmation in `/home/berucrypt00/hakios-x6886/device-dump/getprop.txt`:
```
[ro.vendor.tran.anti.crack.p7.support]: [1]
[ro.vendor.tran.arb_avb_img_version]: [6]
[ro.vendor.tran.arb_group_0_version]: [6]
[ro.vendor.tran.arb_group_1_version]: [6]
[ro.vendor.tran.arb_pl_version]: [6]
```

### 1.3 `init.rc`
Exact path: `/home/berucrypt00/otadump/system/system/etc/init/hw/init.rc`

```
1237:     vfy_boot Trigger_Rpmb /vendor/bin/verify_rpmb
1246: on property:sys.boot_completed=1 && property:ro.boot.mode=normal && property:ro.vendor.tran.anti.crack.p7.support=1
1247:     vfy_boot Trigger_P7 /vendor/bin/verify_para
1252:     vfy_boot Trigger_keyfilesvfy /system_ext/bin/verify_key_files
```

This is a **post-boot** trigger (`sys.boot_completed=1`) — distinct from the LK boot-time lock.

### 1.4 `verify_para` binary
`/home/berucrypt00/otadump/vendor/bin/verify_para` — 5,960 bytes, ELF 64-bit aarch64 PIE, stripped, interpreter `/system/bin/linker64`.
Relevant strings: `tran_is_para_correct p7 api=%d`, `verifyPara ver=20241015v01`, `This software version support P7`, `tran_is_para_correct p7 support`, `ro.vendor.tran.anti.crack.p7.support`, `tran_is_para_correct p7 not support`, links against `libtvrfy.so`, `libtrancrypto.so`, `libtran_sec_log.so`. It calls `property_get` and `tran_is_para_correct`. **This is the P7 anti-crack check, not the fastboot gate.**

### 1.5 PayJoy package references (Android side)
- `/home/berucrypt00/otadump/system/system/etc/permissions/platform.xml:406-409` — comment `SDD: add for payjoy by jiahua.li 20231012 start/end`, `allow-in-data-usage-save` for `com.payjoy.status` and `com.payjoy.access`.
- `/home/berucrypt00/otadump/system_ext/etc/vconfig/tranusf/config/doze/doze.json:36-37`, `hiber.json:10-11`, many `tranpm_*.json` whitelists/policies — `com.payjoy.access`, `com.payjoy.status`, `com.payjoy.status.df`.
- `/home/berucrypt00/otadump/tr_product/etc/vconfig/magellan/refresh_rate_config.xml:858` — refresh-rate entry for `com.payjoy.status`.
- `/home/berucrypt00/otadump/system_ext/etc/vconfig/thub/thub_features_config.xml:15` — `com.transsion.hubcore.finance.payjoy.TranPJComponentImpl`.
- No file named `*payjoy*` exists in the dump's `all_files.txt`; the runtime lock is not an APK file in these partitions.

### 1.6 Paytrigger HAL — the runtime lock writer
| Path | Evidence |
|---|---|
| `/home/berucrypt00/otadump/vendor/bin/hw/vendor.transsion.hardware.security.paytrigger@1.0-service` | Symbols `paytrigger_setDeviceLockInfo`, `paytrigger_getDeviceLockInfo`, `paytrigger_clear_rpmb`, `get_rpmb_status`; strings `clearRpmbArea start`, `getDeviceLockInfo start`, `setDeviceLockInfo start`, `PAYTRIGGER_CA`, `paytrigger_hal aidl 20250315 ver =%s` |
| `/home/berucrypt00/otadump/vendor/lib64/libpaytrigger.trustonic.so` | `/mnt/vendor/persist/paytrigger/{hmac_key,rsa_priv,rsa_pub,reg_info}`, `paytrigger_encrypt_and_sign_data`, `paytrigger_verify_and_return_data`, TEE CA `PAYTRIGGER_CA` |
| `/home/berucrypt00/otadump/vendor/lib64/vendor.transsion.hardware.security.paytrigger-V1-ndk.so` | AIDL `IPayTrigger` methods: `setDeviceLockInfo`, `getDeviceLockInfo`, `clearRpmbArea`, `getRpmbStatus`, `getHmacKey`, `importRsaMsgPub` |
| `/home/berucrypt00/otadump/vendor/etc/init/vendor.transsion.hardware.security.paytrigger@1.0-service.rc` | `service paytrigger_hal_service /vendor/bin/hw/...` |
| `/home/berucrypt00/otadump/system_ext/framework/paytrigger-simlock.jar` (+ oat/vdex) | SIM-lock side of the same mechanism |
| `device-dump/getprop.txt` | `[init.svc.paytrigger_hal_service]: [running]`, `[init.svc_debug_pid.paytrigger_hal_service]: [1604]` |

---

## 2. What enables the lock, and what it blocks

**VERIFIED:** LK contains `check_payjoy_flag` and `[tran] tran_tkv_get_payjoy_flag is failed`. The getter name is literally `tran_tkv_get_payjoy_flag`; TKV is a real region on this device (`cmdline.txt` contains `tkv=9653190656`, and the scatter has `tkv_a`/`tkv_b` with `file_name: tkv.img`). Live props show `ro.vendor.tran.rpmb.support=1` and `ro.vendor.tran.keystoredb.protect.support=1`.

**INFERENCE (high confidence):** the PayJoy lock condition is a **flag stored in the TKV area (likely RPMB/protected storage), read by LK before/around fastboot initialization** — not `ro.vendor.tran.anti.crack.p7.support`. That prop only says the P7 feature is compiled in; it is `1` on this device even though nothing in the dump shows the lock engaged. The runtime writer is the paytrigger HAL + `paytrigger-simlock` (§1.6), which persists lock info to RPMB and can clear it.

**VERIFIED string / INFERENCE scope:** the symptom is `INFO: fastboot disabled by payjoy!!!`. The wording says *fastboot* is disabled, not "unlock disabled", which points to the **fastboot interface being disabled wholesale** rather than just `flashing unlock`/`oem` commands. This cannot be proven from strings alone. **UNKNOWN:** whether the flag is active on this unit, and precisely which commands are blocked. Must be checked at runtime (§6 step 4). Do not assume the device is unlocked just because `ro.oem_unlock_supported=1` or because `get_unlock_ability` exists in LK.

**Important distinction:** `verify_para` / `Trigger_P7` runs *after* `sys.boot_completed` and calls `tran_is_para_correct`; it is an anti-crack check, **not** the boot-time fastboot gate. Do not conflate the two.

---

## 3. A/B slot safety

### 3.1 Static slot-suffixed partitions (from `mtk/MT6789_Android_scatter.txt`)
Unique `partition_name: *_a|_b` entries (both EMMC and UFS sections agree):

```
boot_a / boot_b                 vbmeta_a / vbmeta_b
vbmeta_system_a / _b            vbmeta_vendor_a / _b
dtbo_a / dtbo_b                 vendor_boot_a / vendor_boot_b
init_boot_a / init_boot_b       lk_a / lk_b
tkv_a / tkv_b                   tee_a / tee_b
gz_a / gz_b                     scp_a / scp_b
spmfw_a / spmfw_b               sspm_a / sspm_b
mcupm_a / mcupm_b               dpm_a / dpm_b
pi_img_a / pi_img_b             md1img_a / md1img_b
logo_a / logo_b                 connsys_bt_a / _b
connsys_gnss_a / _b             connsys_wifi_a / _b
preloader_b                     (preloader is in EMMC_BOOT1/BOOT2, only one `_b`)
```

Requested key partitions — exact scatter blocks:
- `boot_a` — SYS35, linear/physical `0x33200000`, size `0x4000000`, `file_name: boot.img`, `is_download: true`, `operation_type: UPDATE`.
- `boot_b` — SYS57, linear/physical `0x55200000`, size `0x4000000`, `file_name: NONE`, `is_download: false`, `operation_type: INVISIBLE`, `is_upgradable: true`.
- `vbmeta_a` — SYS11, `file_name: vbmeta.img`, `is_download: true`, size `0x800000`.
- `vbmeta_b` — SYS14, `file_name: NONE`, `is_download: false`, `operation_type: INVISIBLE`.
- `vendor_boot_a` SYS36 / `vendor_boot_b` SYS58 (both `0x4000000`); `init_boot_a` SYS37 / `_b` SYS59 (`0x800000`); `dtbo_a` SYS38 / `_b` SYS60 (`0x800000`).

**VERIFIED:** no image file for `boot_b` exists anywhere in the dump (`find ... -iname '*boot_b*'` → empty). `boot_b` is not part of the factory flash set.

### 3.2 Logical (super) partitions
`/home/berucrypt00/otadump/super_partitions.json` and `mtk/partition_info.json`: all 32 logical partitions are slot-suffixed and grouped `main_a` / `main_b` — `odm`, `odm_dlkm`, `product`, `system`, `system_dlkm`, `system_ext`, `vendor`, `vendor_dlkm`, plus Transsion `tr_carrier`, `tr_company`, `tr_manifest`, `tr_misc`, `tr_overlayfs`, `tr_preload`, `tr_product`, `tr_region`. `super` size `13,083,561,984` bytes; groups `main_a`/`main_b` only. These are unaffected by a `boot_b`-only write.

### 3.3 Does `flash boot_b` switch slots?
**VERIFIED:** LK contains distinct `set_active:` / `set_active_slot` fastboot entry points plus `current-slot` and `slot-count`. There is no string or evidence of automatic slot switching on partition write.
**INFERENCE (standard A/B semantics):** `fastboot flash boot_b <img>` writes only the `boot_b` partition and leaves the boot-control metadata (slot priority/tries/successful flags) untouched. The active slot changes only via `fastboot set_active b`, a successful OTA, or slot-failure fallback. Therefore **do not issue `set_active b`**, and verify `current-slot` before and after the write. Note that `fastboot flash boot_b` and `fastboot flash --slot b boot` are equivalent in effect — neither sets active.

Live captured state: `ro.boot.slot_suffix = _a` → **slot A is currently active**.

---

## 4. Recovery path and boot-ROM rescue

### 4.1 Recovery
**VERIFIED:**
- No `partition_name: recovery` anywhere in the scatter; no `/dev/block/by-name/recovery` reference in the searched files. `ro.product.ab_ota_partitions` *lists* `recovery` (Transsion's copied Qualcomm-style OTA list), but there is no physical partition.
- `/home/berucrypt00/otadump/boot/header_info.json`: `header_version: 4`, `ramdisk.size: 0`, note `"empty (normal for GKI boot)"`. So `boot.img` carries no ramdisk.
- `/home/berucrypt00/otadump/vendor_boot/ramdisk/` contains the vendor/generic ramdisk (`system/etc/init/hw/init.rc`, `first_stage_ramdisk/`, `lib/modules/`), and `/home/berucrypt00/otadump/vendor_boot/recovery_ramdisk/init.recovery.mt6789.rc` contains the recovery init (`import /init.recovery.project.rc`, `start adbd`, `exec u:r:update_engine:s0 ... /system/bin/mtk_plpath_utils`).

So recovery is in `vendor_boot`, not in `boot`. **INFERENCE:** replacing only `boot_b` with U-Boot does not delete the A-side recovery ramdisk. Because we keep slot A active and do not touch `vendor_boot_a`, recovery remains reachable on slot A. Slot B recovery would be lost (its kernel/ramdisk pair is broken), which is acceptable **only** if slot B is never made active.

### 4.2 Boot-ROM (MTK download mode) rescue availability
**VERIFIED (negative):**
- No `tools/` directory found anywhere under `/home/berucrypt00/hakios-x6886`.
- No `mtk.py`, `mtkclient`, or `*mtk*client*` file found in the project tree.
- `command -v mtk`, `command -v mtk.py`, `command -v mtkclient` → all **NOT in PATH**.
- Python `importlib.util.find_spec('mtkclient')` → `None`.
- `adb` and `fastboot` → **NOT in PATH** (so even the planned fastboot commands need a tool path supplied at execution time).
- `avbtool` → NOT in PATH; python `avb` module → `None`.
- Available locally: `python3`, `strings`, `file`, `xxd`, `readelf`, `objdump`.

**Conclusion:** there is **no locally staged SoC-level rescue tool**. The boot ROM download mode itself is an MTK hardware feature (preloader is present in `images/preloader*.img`, `images/lk.img`, scatter `preloader`/`preloader_backup` in `EMMC_BOOT1/BOOT2`), but no client exists here to talk to it. This must be fixed before any write.

---

## 5. Concrete pre-flash checklist (DOCUMENTED ONLY — no command executed)

All commands below are read-only queries. Run from a shell where `adb`/`fastboot` are explicitly on `PATH` (they are not by default here).

### Step A — stage the rescue path BEFORE anything else (gate)
1. Install/stage `mtkclient` (or equivalent MTK download-mode client) locally and verify `python3 mtk.py --help` works.
2. Confirm a known-good stock `boot.img` backup file exists off-device and its SHA-256 is recorded (dump copy: `/home/berucrypt00/otadump/images/boot.img`).
3. Record `sha256sum` of the U-Boot image that will be flashed.

### Step B — Android-side state (read-only, while booted normal)
```sh
adb devices
adb shell getprop ro.boot.slot_suffix                    # expect [_a]
adb shell getprop ro.boot.mode                           # expect [normal]
adb shell getprop ro.boot.vbmeta.device_state            # expect [locked]
adb shell getprop ro.boot.verifiedbootstate              # expect [green]
adb shell getprop ro.boot.veritymode                     # expect [enforcing]
adb shell getprop ro.boot.flash.locked                   # expect [1]
adb shell getprop ro.vendor.tran.anti.crack.p7.support   # [1] = feature present, NOT lock active
adb shell getprop ro.vendor.tran.arb_avb_img_version
adb shell getprop ro.vendor.tran.arb_group_0_version
adb shell getprop ro.vendor.tran.arb_group_1_version
adb shell getprop ro.vendor.tran.arb_pl_version
adb shell getprop init.svc.paytrigger_hal_service        # [running] expected
adb shell getprop ro.vendor.tran.rpmb.support
adb shell getprop ro.vendor.tran.keystoredb.protect.support
adb shell ls -l /dev/block/by-name/ | grep -E 'boot|vbmeta|dtbo|vendor_boot|init_boot|tkv|lk'
adb shell cat /proc/partitions
```
**Stop condition:** if `ro.boot.slot_suffix` is not `_a`, STOP — the whole plan assumes A is the known-good slot.

### Step C — fastboot reachability / PayJoy check (gate)
```sh
adb reboot bootloader
fastboot devices
fastboot getvar product
fastboot getvar current-slot                 # expect: _a
fastboot getvar slot-count                   # expect: 2
fastboot getvar is-unlocked                  # informational
fastboot getvar get_unlock_ability           # informational
fastboot getvar all
```
**Stop condition:** if the LK message `INFO: fastboot disabled by payjoy!!!` appears, or `fastboot devices` is empty / getvars time out → **ABORT**, do not flash. Do not try to "work around" it with `oem tran_unlock`; that is a separate auth path and may trip anti-rollback.

### Step D — verify slot A and slot B state before write
```sh
fastboot getvar slot-successful:_a
fastboot getvar slot-successful:_b
fastboot getvar slot-unbootable:_a
fastboot getvar slot-unbootable:_b
fastboot getvar slot-retry-count:_a
fastboot getvar slot-retry-count:_b
fastboot getvar partition-size:boot_a
fastboot getvar partition-size:boot_b
```
(If these getvars are unsupported by this LK, fall back to `fastboot getvar all` output and note the gap.)
**Stop condition:** `slot-successful:_a` must not indicate failure and `slot-unbootable:_a` must not be `yes`. If slot A is not known-good, do not proceed.

### Step E — the write (ONLY after explicit user approval; documented, not executed)
```sh
fastboot flash boot_b <u-boot-image>
```
Do **not** add `set_active`, do **not** use `fastboot flash --slot a`, do **not** flash `vbmeta*`, `boot_a`, `lk*`, `preloader*`, `tkv*`, or any super logical partition in this operation.

### Step F — immediate post-write verification (before reboot)
```sh
fastboot getvar current-slot                 # MUST still be: _a
fastboot getvar slot-successful:_a
fastboot getvar slot-unbootable:_a
fastboot continue                            # or: fastboot reboot  (NO set_active)
```
Then, once booted:
```sh
adb shell getprop ro.boot.slot_suffix        # MUST still be [_a]
adb shell getprop ro.boot.verifiedbootstate  # expect [green] on slot A
```
If `current-slot` ever reads `_b`, stop and fix slot selection before rebooting.

---

## 6. The single most dangerous failure mode

**Failure mode:** *Accidentally making slot B active (or damaging `boot_a`) while fastboot is unavailable — because PayJoy/TKV has disabled it and no MTK download-mode client is staged locally.*

Chain: `flash boot_b` (or an unintended `set_active b`, or an OTA/slot-failure fallback) → device boots slot B → the raw U-Boot image is not a valid AVB-signed `boot.img`, so LK rejects it (`[SEC] img is rollbacked` / `androidboot.verifiedbootstate=red` / ARB failure) → slot B marked unbootable → fallback attempts slot A → if slot A was also touched, or if `fastboot` is blocked by PayJoy and there is no local download-mode client, there is **no remaining local path to write a boot image**. eMMC preloader/LK are in `EMMC_BOOT1/BOOT2` and `tkv`/`efuse` hold the lock; none of that is repairable from a disabled fastboot shell.

**Why this is the worst:** every other hazard (AVB mismatch, ARB version, broken recovery in B, unprovisioned `boot_b`) is confined to slot B and is harmless while A is active. This one destroys the A-side escape route.

**How to avoid it — hard gates:**
1. Never run `fastboot set_active b`. Never pass `--slot a`. Target literally `boot_b` only.
2. Verify `current-slot == _a` **before** and **after** the flash (§5 C and F).
3. Do not write `boot_a`, `vbmeta*`, `lk*`, `preloader*`, `tkv*`, `efuse`, or any super partition in this operation.
4. Confirm fastboot is reachable and the PayJoy message is absent before the first write (§5 C).
5. Stage a working `mtkclient` download-mode client **before** the write (§5 A).
6. Keep the stock `boot.img` and its SHA-256 off-device; keep the U-Boot image hash recorded.
7. Treat `ro.vendor.tran.anti.crack.p7.support=1` as feature presence only — it is not proof of lock state, and it is not proof of safety either.

---

## 7. Coverage notes / open UNKNOWNs
- Exact condition that sets the TKV payjoy flag (which app/server writes it, what triggers it) — not determinable from dumps.
- Whether PayJoy disables all fastboot or only unlock/oem commands — string wording only; runtime confirmation required.
- Whether PayJoy is active on this specific unit right now — no on-device flag exposed via `getprop`; runtime check required.
- Current contents/validity of `boot_b` on the device — stock scatter says it is unprovisioned; on-device getvars required.
- Exact ARB rollback index stored in efuse/RPMB vs the `=6` property values — `avbtool` unavailable locally; not verified.
- All of the above are documented as checks in §5; none were executed during this analysis.
