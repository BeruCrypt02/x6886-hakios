# X6886 GPU firmware sources — what the XNU GPU path actually needs

Status: READ-ONLY research. No source/build/boot change, no flash, no device access, no
firmware payload written. Analysis date 2026-11-26.

## VERDICT

**The XNU GPU path needs ZERO firmware blobs.** Neither `valhall-1691526.wa` nor
`mali_csffw.bin` is required for the selected Job Manager (JM) v9 path.

- `mali_csffw.bin` is CSF (Command Stream Frontend) firmware. X6886 is Valhall v9 JM,
  which has no CSF microcontroller. It is not needed, and must not be copied from a
  CSF-generation device.
- `valhall-1691526.wa` is an Arm `mali_kbase` **driver workaround** blob (dummy-job
  execution for a GPU hang), not a hardware boot microcode. It is needed only by a
  faithful Linux `kbase` port that reproduces that workaround. The existing
  HelioDarwin XNU JM path on a sibling MT6789 / Mali-G57 r0p1 device programs JM
  registers directly, loads no firmware at all, and has confirmed hardware JM job
  completions including fragment work.
- Both names are real on the stock Android image, and **neither payload exists locally**
  in `/home/berucrypt00`. That is a blocker only for a `kbase` clone; it is not a
  blocker for the XNU JM path.

Evidence tags: **[V]** = verified in local file this session or in
`docs/reference/dump-analysis/gap-gpu-firmware.md`; **[E]** = external authoritative source (URL in
§7); **[I]** = inference explicitly marked as such.

---

## (a) What the Linux `mali_kbase` driver requests

Stock driver module: `mali_kbase_mt6789_a16w_jm.ko` in
`/home/berucrypt00/otadump/vendor_dlkm/lib/modules/` (1,605,128 bytes). Version string
`version=r54p1-12eac0 (UK version 11.46)`. The device's own spec dump records
`OpenGL ES 3.2 v1.r54p1-12eac0.31596c752a7e2ed34e05ad43c82dd58d`
(`/home/berucrypt00/hakios-x6886/devices/x6886/evidence/X6886-specs.txt:67`).

| Blob | On-device path | Evidence | Role |
| :-- | :-- | :-- | :-- |
| `valhall-1691526.wa` | `/vendor/firmware/valhall-1691526.wa` | literal NUL-terminated `.rodata` string at `.ko` offset `0x9245e`; near it `kbase_dummy_job_wa_load.wa_name` (`0x180695`) and `kbase_device_firmware_init_once`; `request_firmware`/`release_firmware` strings; error `WA blob missing ... driver probe will be failed` (`0xa6a27`); explicit SELinux file context in three copies of `vendor_file_contexts` | **[V]** Dummy-job workaround blob. The AOSP kbase implementation defines `DUMMY_JOB_WA_BINARY_NAME "valhall-1691526.wa"`; its header says "Implementation of the dummy job execution workaround for the GPU hang issue" **[E]**. The blob is parsed as signature `0x4157` version `2`, its payload is copied into GPU-accessible memory, and a privileged dummy job is run on a job slot after power-up and MMU init but before normal work **[E]**. |
| `mali_csffw.bin` | `/vendor/firmware/mali_csffw.bin` | explicit SELinux file context in three copies of `vendor_file_contexts` (`vendor/etc/`, `vendor_boot/ramdisk/`, `vendor_boot/recovery_ramdisk/`); the earlier `sffw.bin` hit is its tail. **No** literal `mali_csffw.bin` exists in the `.ko` — all `sffw` strings there are trace fields (`csffw_cycle`, `KBASE_TL_KBASE_CSFFW_*`) **[V]** | **[E/I]** CSF firmware for Valhall v10+ GPUs with a Command Stream Frontend. DDK r54p1 is a shared release that carries both JM and CSF code, so a generic vendor policy can list the path even when this chip uses JM. |

Clarifications that matter:

1. **The `.ko` has no embedded firmware.** `readelf -S` shows only standard module
   sections; there is no ELF section carrying either payload **[V]**.
2. **Only the WA blob is visibly requested by this kernel module.** The dump analysis
   found no `mali_csffw.bin` literal in any binary; the CSF path is established by
   SELinux policy, not by an observed `request_firmware` call **[V]**. Do not upgrade
   that inference into "the JM driver loads CSF firmware on X6886".
3. **Neither payload is an OTA file.** `all_files.txt` has 94 firmware entries and no
   match for `mali`, `gpu`, `sffw`, `csffw`, `valhall`, or `.wa` **[V]**.

---

## (b) What a Job Manager-only v9 path needs

### Does it need CSF? — NO

- X6886's GPU node is `arm,mali-valhall`, the driver is `_jm`, and the architecture is
  Valhall v9 Job Manager; HelioDarwin explicitly states "Valhall v9 Job Manager (JM)
  GPU. It does not use the newer CSF submission interface" **[V]**.
- Independent sources agree: v9 and older use JM job chains; CSF starts at v10 (G310,
  G510, G610, G710, and v11 G615/G715). Arm's own security-scope note says early
  Valhall GPUs are JM and later Valhall GPUs are CSF **[E]**.
- CSF firmware is consumed by a firmware-resident MCU that reads command-stream rings.
  A v9 JM GPU fetches job chains directly from hardware job-slot registers; there is no
  MCU to boot. The `mali_csffw.bin` SELinux rule on X6886 is generic policy from the
  shared r54p1 vendor build, not evidence that this silicon uses CSF **[I, high
  confidence]**.
- Contrast in-tree: `refs/akita/drivers/gpu/hakios.akita_mali.c` (Mali-G715, v11 CSF)
  defines `FW_NAME "mali_csffw-r54p2.bin"` and explicitly loads it and starts the MCU
  before submitting jobs. Wrong generation, wrong filename, wrong submission model —
  do not copy that path to X6886 **[V]**.

### Does it need the WA blob? — NO for the XNU JM path

- The blob implements a *driver* workaround: it supplies a dummy job chain and payload
  that `kbase` executes once after power-up/MMU init to avoid a specific GPU hang
  (issue 1691526) **[E]**. It is not a microcode image that the JM block requires in
  order to fetch and execute a normal job chain.
- Even inside AOSP kbase, loading is gated by `wa_blob_load_needed()`; the published
  implementation can return false, i.e. this is a per-platform opt-in, not a universal
  hardware dependency **[E]**.
- Decisive local evidence: `refs/heliodarwin/wiki/{MaliMetal-Driver,MT6789-GPU-Power-Sequence}.md`
  and the HelioDarwin `MaliMetal/` + `xnu/` source contain **zero** references to
  `request_firmware`, `csffw`, `.wa`, `wa_blob`, or `firmware_load` **[V]**. That path
  powers the rails, sets up 4 KiB GPU page tables, writes `JS_HEAD_NEXT` /
  `JS_COMMAND_NEXT = START` directly, and has confirmed ten hardware JM terminal
  results (including fragment work) on an MT6789 Mali-G57 r0p1 phone. The same GPU
  family and DDK generation therefore completes real JM jobs with no blob **[V]**.
- Boundary of the claim: this proves the blob is not a prerequisite for JM job
  submission/completion on a sibling device. It does **not** prove that the WA never
  helps stability on X6886, which has no XNU device run in this task. No evidence
  supports adding it to the XNU path, and no payload is available locally **[I]**.

### Reconciliation with existing docs

- `docs/X6886-GPU-PLAN.md:103` states "Firmware | READY | JM needs no CSF firmware. No
  firmware file is missing from the dump." The first clause is correct and is confirmed
  here. The second clause is too broad if read as a statement about stock Linux
  `kbase`: the two blobs genuinely are absent from the dump
  (`docs/reference/dump-analysis/gap-gpu-firmware.md`). It is correct only for the XNU JM path, which
  needs neither blob.
- `docs/HANDOFF-IOS27-BOOT.md:12` ("NOT CSF. There is NO `mali_csffw.bin` / valhall CSF
  firmware for this chip") is correct in substance: the chip does not *use* CSF
  firmware. A generic SELinux rule still names the file **[V]**.
- `docs/reference/dump-analysis/firmware.md:255` proposed three possibilities for `sffw.bin`, including
  "this driver revision falls back to a no-firmware path." That open question is now
  closed for the XNU path: the chosen path has no firmware loading at all.

---

## (c) Is any equivalent blob available locally? — NO

Searches run this session under `/home/berucrypt00` with `find -xdev -type f ... -not
-path '*/.git/*'`, bounded per top-level directory. Filenames matched: `*.wa`,
`*csffw*`, `*sffw*`, `valhall*`, `*mali*fw*`, `*mali*.bin`.

Directories swept: `otadump`, `x6886-dumps`, `hakios-x6886`, `x6886-hakios`,
`x6886-mamba-build`, `x6886-verify`, `android_device_infinix_X6886-a16-lineage-24.0`,
`Android`, `google_pro_6_12`, `google_pro_6_12 (2)`, `Downloads`, `Documents`,
`Desktop`, `Oppo_A15_CPH2185_MT6765_PU_11_A.11_120321`, both `SP_Flash_Tool` trees,
`Shelby_REC_Tool_v2.2_source`, `nvidia390-check`, `ClaudeCode`, `friend-tool`, `bin`,
`go`, both `GeminiUltra12Month` trees, and `Music`, `Pictures`, `Public`, `Templates`,
`Videos`.

**Result: zero firmware payload files.** No `valhall-*.wa`, no `mali_csffw*.bin`, no
`*sffw*`, no `*mali*.bin` anywhere in the swept tree.

What *does* exist locally, and what it is:

| Local artifact | What it is | Usable as a blob? |
| :-- | :-- | :-- |
| `otadump/vendor/etc/selinux/vendor_file_contexts` (+ 2 ramdisk copies) | plaintext policy lines for `/vendor/firmware/mali_csffw.bin` and `/vendor/firmware/valhall-1691526.wa` | No — names only |
| `otadump/vendor_dlkm/lib/modules/mali_kbase_mt6789_a16w_jm.ko` | driver with the `valhall-1691526.wa` string and WA-loader code | No — no embedded firmware section |
| `otadump/images/vendor_boot.img`, `vendor_boot-debug.img` | LZ4-compressed `vendor_file_contexts` literals at `0x1c4eab4`, `0x1f5e372`, etc.; no gzip/zstd/xz/LZ4-frame/ELF/Mali magic at those offsets; the extracted ramdisk has zero `*.bin` and zero `*.wa` | No — compressed filename text, not payload **[V]** |
| `otadump/vendor/lib64/egl/mt6789/libGLES_mali.so` | 51 MB userspace driver/compiler with version string `1.5 Valhall-"r54p1-12eac0"` | No — no firmware payload |
| `refs/akita/drivers/gpu/...` and `refs/akita/tools/mali-fw-dump.py` | G715/v11 CSF firmware tooling and `mali_csffw-r54p2.bin` name | No — wrong GPU generation and wrong DDK revision |
| `refs/heliodarwin/...` | JM-only XNU implementation, no firmware loading | Not a blob source; it is the reason no blob is needed |

Search-method limit: the wide sweep was filename-based; a byte-signature scan of every
file in `/home/berucrypt00` was not performed (the tree is too large/slow). For the
authoritative dump, `gap-gpu-firmware.md` §3 already records exhaustive content and
byte-level checks (`grep -ral`, `grep -aob` over all `images/*.img`, `all_files.txt`,
and all 463 extracted ramdisk files) with zero GPU payload hits. No evidence suggests a
blob exists elsewhere.

---

## (d) Safe options if a required blob cannot be found

Ordered by preference; all are non-destructive.

1. **Keep the XNU path JM-only and load nothing.** This is the selected design and the
   one with device evidence (HelioDarwin). There is no required blob to source, so the
   absence in the dump is not a blocker. Gate: any XNU GPU code must contain no
   `request_firmware`-equivalent and no `mali_csffw` / `.wa` path **[recommended]**.
2. **If a faithful `kbase` clone is ever required, extract from the exact X6886 stock
   image only.** Both blobs are GPU/DDK/version-specific. Source them from an X6886
   firmware build matched to `r54p1-12eac0`, from `/vendor/firmware/` on that device or
   its OTA payload; record their SHA-256 and sizes before use. Do not substitute a blob
   from another phone, another DDK revision, or another GPU.
3. **Do not synthesize or "find" a WA blob.** Its format is Arm-proprietary
   (signature `0x4157`, version 2, a job-slot index, a job-chain pointer, flags and
   page-aligned payloads). It is documented only in Arm's Valhall Release Notes
   (part DC-06002) **[E]**; any reconstruction would be a guess and must not be
   presented as the real blob.
4. **Do not import a CSF blob into a JM path.** `mali_csffw.bin` would only be relevant
   to a v10+ CSF port (e.g. the G715 akita reference uses `mali_csffw-r54p2.bin`).
   Adding it to X6886 would be dead weight and a provenance risk **[I]**.
5. **No flashing, no device access in this task.** The live `adb` verification commands
   in `gap-gpu-firmware.md` §6 were not executed. They remain the only way to confirm
   on-device payload sizes/hashes if option 2 is ever chosen.
6. **If future XNU testing shows a hang that a WA would mask**, treat it as a new
   hardware-qualification finding, not as proof that the blob was always required;
   revisit option 2 with a version-matched source at that time.

---

## 7. Evidence index

Local (verified this session unless noted):

- `/home/berucrypt00/x6886-hakios/docs/reference/dump-analysis/gap-gpu-firmware.md` — exact names,
  `.ko` string offsets, three SELinux contexts, absence checks, compressed-literal proof.
- `/home/berucrypt00/otadump/vendor_dlkm/lib/modules/mali_kbase_mt6789_a16w_jm.ko` —
  driver strings and version (via prior verified analysis).
- `/home/berucrypt00/hakios-x6886/devices/x6886/evidence/X6886-specs.txt:67` — r54p1-12eac0 version.
- `/home/berucrypt00/x6886-hakios/refs/heliodarwin/wiki/MT6789-GPU-Power-Sequence.md`
  and `MaliMetal-Driver.md` — JM-only path, no firmware references, confirmed JM
  completions.
- `/home/berucrypt00/x6886-hakios/refs/heliodarwin/MaliMetal/`, `xnu/` — zero
  firmware-load references (grep this session).
- `/home/berucrypt00/x6886-hakios/refs/akita/drivers/gpu/hakios.akita_mali.c:56` —
  `FW_NAME "mali_csffw-r54p2.bin"` on G715/v11 CSF (contrast case).
- `/home/berucrypt00/x6886-hakios/docs/X6886-GPU-PLAN.md`,
  `HANDOFF-IOS27-BOOT.md`, `docs/reference/dump-analysis/firmware.md` — prior claims reconciled above.

External (read-only, authoritative):

- AOSP kbase dummy-job WA implementation:
  https://android.googlesource.com/kernel/google-modules/gpu/+/refs/tags/android-12.0.0_r0.36/mali_kbase/mali_kbase_dummy_job_wa.c
  — `DUMMY_JOB_WA_BINARY_NAME "valhall-1691526.wa"`, blob header/payload parsing,
  `wa_blob_load_needed()` gate, "GPU hang issue" comment, DC-06002 error text.
- CSF architecture and mandatory `mali_csffw.bin` on v10+:
  https://github.com/turbofish-pk/linux-graphics-stack-book/blob/main/chapters/part-02-gpu-drivers/ch90-panfrost-panthor-lima.md
  and https://lists.x.org/archives/dri-devel/2023-February/390214.html (`PANCSF_CS_FW_NAME "mali_csffw.bin"`).
- v9 JM vs v10+ CSF table and MT6789 r54p1 JM identification:
  https://github.com/zenithblue-oss/panvk-kbase-android/blob/main/docs/MALI-GPU-ARCHITECTURES.md
  and https://github.com/Noysz/panvk-g99-jm/blob/main/docs/hardware-runtime.md.
- Arm scope note (early Valhall JM, later Valhall CSF):
  https://app.intigriti.com/programs/arm/arm/detail.
