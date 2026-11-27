# Mali-native v7 -> Valhall v9 for X6886: read-only feasibility study

Offline study only. No device, boot image, APFS image, kernel tree or ref was modified.
`/home/berucrypt00/otadump` was not touched. Nothing was flashed. All paths are local.

Evidence base: `build/ios27-overlay/MANIFEST.md` (338 verbatim-extracted files, with
SHA256 and line-level citations), `refs/oosexgang-xnu-kernel-mtk-helio` branches
`origin/mali-native` and `origin/ios27`, `refs/heliodarwin/MaliMetal`,
`refs/heliodarwin/xnu/patches`, `docs/X6886-GPU-PLAN.md`, and
`/home/berucrypt00/v9_pinned.xml`.

## Verdict

**NO — the local mali-native (Bifrost v7) userspace cannot be converted into a
Valhall v9 backend for X6886. It must be replaced, not ported.**

The v7 library is not "almost v9". Its device gate, product table, shader compiler,
descriptor packers, job encoder, shader blobs and kernel ABI are all Bifrost v7
specific, and its own source says so: `case 9: return "Valhall v9: not written yet (a
backend of its own); Mesa's panfrost draws it meanwhile"` (`mali-native:userland/src/mali/rwa/rwa_helio.c:93-94`).
There is no v9 backend inside it to enable, patch or reconfigure.

The only locally available Valhall v9 userspace path is the **separate, parallel
MaliMetal stack** (Objective-C Metal frontend -> AIR/SPIR-V -> pinned Mesa v9 PanVK
JM Vulkan backend -> XNU IOKit Job Manager transport). It shares no source with
mali-native and is a replacement, not a conversion target. MaliMetal is present as
**source only**. There is no built v9 backend in this tree and no claim of working
rendering is made here: even on its reference MT6789/X676C phone, the vendor reports
hardware JM completion but **correct pixels remain unvalidated** and the November 6
run produced corrupt blue geometry.

The pinned `v9_pinned.xml` (`<panxml arch="9">`, 75,353 bytes) is a descriptor
packer definition. It supplies register/descriptor field layouts to Mesa's generated
genxml; it is **not** a shader compiler, not a Vulkan driver and not a submission
backend. Possessing it does not make the v7 source v9-capable.

## Why an in-place conversion is impossible (decisive facts)

| # | Evidence | Consequence |
| -- | -- | -- |
| 1 | `mali-native:userland/src/mali/rwa/rwa_mali.c:456` — `if (pan_arch(d->gpu_id) != 7)` | `0x9093` gives `pan_arch == 9`; `rwa_device_open()` returns NULL before a device exists. |
| 2 | `mali-native:.../rwa/rwa_helio.c:11-12,21,32,38-39,93-94` | v9 explicitly "a backend of its own ... genxml v9, Panfrost's PAN_ARCH 9". Only `mt6768`/G52 is `run=true`; every G57 row (`mt6781`, `mt6789`, product `0x9091/0x9093`) is `false`. No G57 was ever run on this library. |
| 3 | `ios27:userland/src/mali/compiler.c:20,60,70,768,772` | Includes `panfrost/compiler/bifrost_compile.h`, calls `bifrost_preprocess_nir()` and `GENX(pan_shader_compile)`; on empty output reports "the Bifrost compiler gave no code". No Valhall compile path. |
| 4 | `ios27:system27/userland/mali27.sh:52,58-59,60` | Builds `-DPAN_ARCH=7`, links `libpan-arch-v7.a`, `libpan-format-v7.a`, `libpanfrost_bifrost.a`. `libpanfrost_valhall_disasm.a` on line 60 is **disassembly only**. |
| 5 | `ios27:userland/src/mali/gpu.cpp:52-64`, `pass.cpp:25` | Accepted products are `0x7090`, `0x7093`, `0x7211`, `0x7212`, `0x7402` only; `0x9093` is refused as "not a Bifrost v7". |
| 6 | `ios27:userland/src/mali/v7.c/v7.h`, `shaders_v7.h`, `README.md:3` | Descriptors and precompiled shader blobs are Bifrost genxml v7. Valhall replaces renderer state and attribute tables with shader-program and resource descriptors. Nothing here is bit-compatible. |
| 7 | `mali-native:.../rwa/kmali.c`, `kmali.h`, `iokit_transport.cpp` | The kernel ABI is the Bifrost `HakiosMali` ioctl transport (GPU base `0x13040000` on MT6768). X6886 needs the v9 `MMK_*` IOKit JM ABI and GPU base `0x13000000`. Different wire protocol. |
| 8 | `mali-native:.../metal/air.cpp:6-11` | The only "support" for a refused GPU is the `libOSMesa.8.dylib` Gallium panfrost fallback under `mali_native=1`. That is Mesa, not mali-native, was never run on G57, and requires an external Mesa build absent from both branches. |
| 9 | `docs/X6886-GPU-PLAN.md` (FDT/DUMP) | X6886 is `mali@13000000`, `arm,mali-valhall`, JM with no CSF, driver `mali_kbase_mt6789_a16w_jm.ko`; no CSF firmware is needed. GPU_ID is **not measured on X6886**; `0x90930010` is an X676C reading only. |

## What MaliMetal actually provides (and what it does not)

Present locally as source under `refs/heliodarwin/MaliMetal/`:

- Full Metal frontend: `MaliMetalDevice.m`, `MaliMetalResources.m`, `MaliMetalGraphics.m`,
  `MaliMetalRender.m`, `MaliMetalCommandQueue.m`, `MaliMetalSynchronization.m`,
  `MaliMetalPresentation.m`, `MaliMetalMSL.m`, `MaliMetalAPIExtensions.m`, `MaliMetalMPSBridge.mm`.
- Metal->Vulkan bridge and Vulkan backend: `MaliMetalVulkan.c`, `Vulkan/MaliVulkanBackend.c`,
  `MaliVulkanResources.c`, `MaliVulkanGraphics.c`, `MaliVulkanSynchronization.c`,
  `MaliVulkanProfile.c`, `MaliKmodDarwin.c`.
- Shader path: `Shader/MaliMetalShader*.c`, `Shader/MaliMetalRenderVulkan.c`,
  `Shader/MaliMetalCompute*.c`, `Shader/build_air.py` and three compiler patches.
- v9 register/job structures: `MaliValhallHw.h` (GPU_ID `0x90930010`, JM base `0x1000`,
  job slots `0x1800 + n*0x80`, 32-byte job header, 128-byte alignment, MMIO `0x13000000`).
- Build scripts: `build.py`, `Vulkan/prepare_mesa.py`, `build_mesa.py`, `build_mesa_wsl.py`,
  `apply_xnu.py`, `package_icd.py`, `resolve_macho_entrypoints.py`.
- XNU kernel-side source: patches `0182`, `0189`, `0208`, `0209`, `0210`, `0212` plus
  `0147`, `0148`, `0178` under `refs/heliodarwin/xnu/patches/`.

Not present and not claimed:

- No compiled `.dylib`, `.metallib`, `.bc`, IR archive, `MaliMetal.bundle`, ICD JSON or
  `build.json` anywhere in the local refs or overlay. No binary was synthesized here.
- No working/validated Valhall backend on X6886. MaliMetal's own README states the
  source builds for arm64 iOS, hardware JM completion was recorded on the reference
  phone, but "correct pixels and broader shader coverage remain unvalidated",
  "Metal or Vulkan conformance is not claimed", and the last observed UI was corrupt.
- No local Mesa v9 tree, no PanVK RC3 checkout, no `metal2vulkan` compiler checkout,
  no iOS sysroot, no `ziglang`, no `ld64.lld-18`.

## File-by-file mapping (mali-native v7 -> v9 path)

Disposition legend: **CANNOT REUSE** = v7-only data/code, delete; **REWRITE** = role still
needed, no local drop-in replacement; **REPLACE** = locally present MaliMetal/Mesa v9
source covers the role; **BUILD INPUT** = architecture-neutral, stage verbatim, yields
no APFS binary. Paths without a branch prefix are under `userland/src/mali/` in the
`mali-native` branch unless marked `ios27:`.

| mali-native file | v7 role | Disposition | v9 replacement / action |
| -- | -- | -- | -- |
| `rwa/rwa_mali.c` | `rwa_*` contract, batching, v7 job chains | REWRITE | No drop-in. Split across `MaliMetalDevice.m`, `MaliMetalCommandQueue.m`, `MaliMetalRender.m` + `Vulkan/MaliVulkanBackend.c`. Batch/preload ideas are design guidance only. |
| `rwa/rwa_helio.c` | SoC/GPU table, `arch==9 -> "not written"` | CANNOT REUSE | Replace with `MaliMetalDevice.m` product handling for `0x90930010` + `MaliValhallHw.h`. The v7 product table is invalid for v9. |
| `rwa/rwa_priv.h` | internal sysvals, slot/UBO limits | CANNOT REUSE | `MaliMetalTypes.h`, `Vulkan/MaliVulkanInternal.h`, `MaliMetalKernelABI.h`. |
| `rwa/rwa_shader.c` | AIR -> NIR -> Bifrost compile, fixed state | CANNOT REUSE | AIR -> SPIR-V via `Shader/build_air.py` + `Shader/MaliMetalShader*.c`; pipeline state via `Vulkan/MaliVulkanGraphics.c`. Bifrost NIR lowering has no v9 analogue. |
| `rwa/kmali.c`, `rwa/kmali.h` | Bifrost `HakiosMali` ioctl ABI | CANNOT REUSE | `Vulkan/MaliKmodDarwin.c/.h`, `Vulkan/mesa/pan_kmod_xnu.c`, `MaliMetalKernelABI.h` + XNU patches `0182/0189/0208/0209/0210/0212`. |
| `compiler.c`, `compiler.h` | in-process Mesa Bifrost compiler | CANNOT REUSE | External pinned AIR compiler (`metal2vulkan` `a2c440b8…`) -> SPIR-V, consumed by Mesa v9 PanVK. Delete the `bifrost_compile.h` include and `libpanfrost_bifrost.a` link. |
| `v7.c`, `v7.h` | genxml v7 descriptor pack/unpack | CANNOT REUSE | Pinned `v9_pinned.xml` -> Mesa `genxml/v9.xml` generated packers inside Mesa v9; `MaliValhallHw.h` for register map. No hand port of these files. |
| `shaders_v7.h` | precompiled Bifrost shader words | CANNOT REUSE | No v9 blob exists. Shaders come from AIR/SPIR-V at build/run time via `build_air.py`; no binary is present locally. |
| `gpu.cpp` | BO alloc, submit, wait over Transport | REWRITE | `Vulkan/MaliVulkanResources.c`, `MaliVulkanSynchronization.c`, `MaliKmodDarwin.c` (native BOs, per-client VM, JM submit/completion). v7 product table `modelOf()` is deleted. |
| `pass.cpp`, `pass.hpp` | device init, tiler heap, clear pass, v7 descriptors | REWRITE | `MaliMetalGraphics.m`, `MaliMetalRender.m`, `Vulkan/MaliVulkanGraphics.c`. v7 tiler heap/sample positions layout is not reusable. |
| `draw.cpp`, `draw.hpp` | draw encoding | REWRITE | `MaliMetalRender.m` + `Vulkan/MaliVulkanGraphics.c` canonical draw planner. |
| `blit.cpp`, `blit.hpp` | blits (mali-native only) | REWRITE | `MaliMetalPresentation.m` / `MaliMetalRender.m`; IOSurface uses bounded CPU staging, **not** zero-copy. |
| `metal.cpp`, `metal.hpp` | Metal glue inside native lib | REPLACE | Full ObjC frontend in `MaliMetal*.m/.mm`; the `rwa_*` contract it served disappears. |
| `iokit_transport.cpp` | old IOKit transport | CANNOT REUSE | `Vulkan/MaliKmodDarwin.c` + patched XNU `MMK_*` ABI. |
| `fake_gpu.cpp`, `fake_gpu.hpp` | host fake GPU, v7 descriptor decode | CANNOT REUSE | No v9 fake GPU locally. Drop `host_test`'s descriptor assertions or author a v9 fake; not part of this scaffold. |
| `probe.cpp`, `tests/host_test.cpp` | `mali-probe` / host test | REWRITE | No `mali-probe` in MaliMetal. Use `MALIMETAL_TRACE_RENDER` bounded diagnostics; device acceptance still required. |
| `program.h`, `mali.hpp` | shared C++ types | CANNOT REUSE | `MaliMetalTypes.h`, `MaliMetalMSL.h`. |
| `Makefile` | builds v7 (`PAN_ARCH=7`, Bifrost libs) | REPLACE | `build.py` (Zig clang + ld64.lld) + `Vulkan/*.py` + `Shader/build_air.py`. |
| `README.md` | v7 documentation | REPLACE | `refs/heliodarwin/MaliMetal/README.md` + `wiki/MaliMetal-Driver.md`. |
| `tools/bishaders.c`, `mk-shaders.sh`, `airkernels.c` | v7 shader blob generation | CANNOT REUSE | `Shader/build_air.py`; no generated `.bc`/`.metallib` is staged. |
| `tools/airdump.c`, `airrender.c`, `rwacheck.c`, `rwagen.c`, `rwavariant.c`, `mk-rwa-host.sh` | RWA/Bifrost test tools | CANNOT REUSE | No v9 equivalents. Drop. |
| `tools/probe-picture.py` | host probe helper | BUILD INPUT | Only if a v9 fake is authored; otherwise drop. |
| `ios27:system27/userland/mali27.sh` | v7 build/install script | REPLACE | `build.py` + `Vulkan/package_icd.py` + `tools/sweet-root-tweaks.py --malimetal-only`. |
| `ios27:system27/userland/metal27.sh` | builds `HakiosMetal` over `libOSMesa.8.dylib` | REPLACE | `build.py` builds `MaliMetal.bundle`; the `rwa_*` plugin path is gone. |
| `userland/build.sh`, `mkeagl.py`, `mkjobs.py`, `rc.jobs.*`, LaunchDaemon plists | generic userland packaging | BUILD INPUT | Architecture-neutral; may be staged verbatim, but installs no v9 GPU backend. |

## Exact build order (documented, NOT executed in this study)

All outputs and third-party sources must live **outside** the HelioDarwin Git checkout.
Variables (substitute real local paths; these are NOT present in this repo):

```sh
export M=/home/berucrypt00/hakios-x6886/refs/heliodarwin/MaliMetal
export WORK=/home/berucrypt00/v9-dep-work          # fresh path outside any Git checkout
export SYSROOT=/path/to/iPhoneOS16.7.sysroot       # proprietary; supply libSystem + reexports,
                                                   # libobjc, Foundation, CoreFoundation, Metal, IOKit, IOSurface
export COMPILER=/path/to/metal2vulkan@a2c440b8     # pinned external checkout
export AIR_BUILD=/home/berucrypt00/v9-air-build
export OUT=/home/berucrypt00/hakios-x6886/build/ios27-overlay-v9/out
```

0. Toolchain prerequisites (verify, do not assume): `python3 -m pip install ziglang`;
   Rust >= 1.98 with `aarch64-apple-ios` (`rustup target add aarch64-apple-ios`; 1.97
   only with `build_air.py --allow-rust-197`); `ld64.lld-18` at `/usr/bin/ld64.lld-18`
   or via `--linker`; network access to GitLab and GitHub. None of these were installed
   or used in this study.

1. Cross-check the local descriptor XML against the pinned tree (the pinned tree wins):

```sh
sha256sum /home/berucrypt00/v9_pinned.xml
# after step 2:
cmp /home/berucrypt00/v9_pinned.xml "$WORK/mesa-v9/src/panfrost/lib/genxml/v9.xml"
```

2. Reconstruct pinned Mesa v9 + PanVK JM-v9 RC3 (Mesa `5a07217f034b3e50d8c7c7794f97a2df1742613b`,
   panvk-kbase-android `fd6ed91eff1c8d3a83307a2348b959bb93797011`; patch families
   `common android kbase-common app-loader wsi csf csf-v11 jm-v9`):

```sh
python3 "$M/Vulkan/prepare_mesa.py" --work "$WORK"
# optional mirror (commit still verified): add --mesa-repo <mirror-url>
```

3. Apply the XNU transport overlay (`mesa-xnu-port.json`, `mesa-xnu-baseline/`):

```sh
python3 "$M/Vulkan/apply_xnu.py" --work "$WORK"
```

4. Build and package the arm64 iOS PanVK ICD:

```sh
python3 "$M/Vulkan/build_mesa.py" --work "$WORK" --sysroot "$SYSROOT" \
  --libcxx-headers /usr/lib/llvm-18/include/c++/v1 --jobs 6
# produces "$WORK/dist-ios/libpanvk_sweetmali.dylib", panvk-sweetmali.json, LICENSES/
# optional: --qualify-g57-fp16 (exact GPU_ID 0x90930010; numeric FP16 UNQUALIFIED)
# on Windows build_mesa.py auto-dispatches to build_mesa_wsl.py via WSL Ubuntu
# standalone re-package only (never relinks): python3 "$M/Vulkan/package_icd.py" --work "$WORK"
```

5. Build the pinned AIR compiler provider (render ABI 4 / compute ABI 2):

```sh
python3 "$M/Shader/build_air.py" --compiler-source "$COMPILER" \
  --compiler-patch "$M/Shader/compiler-patches/0001-coherent-tile-image-color-fetch.patch" \
  --compiler-patch "$M/Shader/compiler-patches/0002-static-init-boolean-fc-absorption.patch" \
  --compiler-patch "$M/Shader/compiler-patches/0003-tile-image-srgb-fetch-contract.patch" \
  --shader-tile-image --out "$AIR_BUILD"
```

6. Build the Metal bundle (strict arm64/iOS link, internal-symbol and platform checks):

```sh
python3 "$M/build.py" --sysroot "$SYSROOT" \
  --vulkan-headers "$WORK/mesa-v9/include" \
  --air-provider "$AIR_BUILD" \
  --vulkan-icd "$WORK/dist-ios/libpanvk_sweetmali.dylib" \
  --out "$OUT" --linker /usr/bin/ld64.lld-18
# add --wsl on Windows. --gpu-timing and --mps-common stay OFF: both are unqualified.
```

7. Only after a real build, stage into a mounted root (NOT run here; no device touched):

```sh
python3 refs/heliodarwin/tools/sweet-root-tweaks.py MOUNTED_ROOT \
  --malimetal-build "$OUT" --malimetal-only
# target: /System/Library/Extensions/MaliMetal.bundle/
```

Order is mandatory: the bundle build verifies the ICD provenance manifest and the AIR
provider build manifest, so steps 4 and 5 must precede step 6. `package_icd.py` requires
the WSL/Linux build tree and `llvm-nm-18`.

## What is missing (exactly)

1. **All built v9 binaries.** No `libpanvk_sweetmali.dylib`, no `MaliMetal` bundle
   binary, no `libMaliMetalAIR.a`, no `panvk-sweetmali.json`, no `.metallib`/`.bc`.
   None may be invented; the `build/ios27-overlay-v9/MANIFEST.md` skeleton marks every
   APFS binary as NOT STAGED.
2. **External source checkouts.** Only `v9_pinned.xml` (75,353 bytes, `arch=9`) and the
   three AIR compiler patches are local. Mesa `5a07217f…`, PanVK RC3 `fd6ed91…` and
   `metal2vulkan` `a2c440b8…` must be fetched and hash-verified.
3. **Toolchain/sysroot.** No `ziglang`, no `ld64.lld-18` on PATH, no iOS 16.7 sysroot
   with the proprietary frameworks. `cargo` exists but the `aarch64-apple-ios` target
   was not verified.
4. **Target skew.** MaliMetal pins iOS 16.7 / 20H392 and `-target aarch64-ios.16.7`;
   this project's overlay work is `ios27`. Objective-C selector completeness requires
   runtime validation on the actual target; compilation alone cannot prove it.
5. **X6886 device facts.** GPU_ID/AS_PRESENT/JS_PRESENT unread on X6886; no XNU PWRAP
   or rail driver; clock/DVFS policy blocked (`opp15` malformed); GPU node has no
   `iommus` and M4U is unbound; CPU pages 16 KiB vs GPU pages 4 KiB; only three of
   five GPU interrupts are live; LK still owns scanout.
6. **Kernel-side integration.** The XNU JM patches (`0182/0189/0208/0209/0210/0212`)
   are source for porting, not applied here. The November 6 run rejected the clock
   query; patch `0210` logs stages but the cause is unidentified.
7. **Qualification.** Even on the reference phone, correct pixels, numeric FP16,
   MPS compute dispatch, ASTC upload and frame rate are unverified; `MALIMETAL_TRACE_RENDER`
   host samples are not GPU-execution observations.

## Acceptance gates (not satisfied by this study)

1. Local build: ICD `build.json`/provenance checks and `MaliMetal.bundle` strict link
   pass with all hashes recorded.
2. Offline shader corpus compiles and validates before any phone boot.
3. On device: rails and MFG domains read back; GPU_ID recorded from X6886 itself;
   an identity-mapped and a page-table-mapped buffer each complete a job; a fragment
   job reaches a terminal record with zero JS/MMU/GPU fault bits.
4. A known static pattern appears on the 1080x2400 panel before any SpringBoard
   animation. Only then can rendered pixels be evaluated.
