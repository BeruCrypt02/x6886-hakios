# App Store and userspace fixes: offline research

Date: 24 Nov 2026. Scope: local refs under `refs/`, local `ios27/` images, and local docs. No adb, no fastboot, no network. Paths are relative to the repo root. A line reference uses `file:line`.

This answers four questions. It records what a file says, not what a screenshot outside this repo claims.

## Summary

| Feature | Status | Evidence file:line | Portable? | Notes |
|---|---|---|---|---|
| App Store UI | Opens to a spinner or tab bar, no catalog | `refs/hakios-common/userland/userland/mkjobs.py:105-114`, `refs/hakios-common/userland/README.md:47`, `refs/hakios-common/userland/README.md:222` | Yes | UI state only. The referenced screenshot files are not in the local tree. |
| Real network from the store stack | Not reached in the measured 16.5 run | `refs/hakios-common/userland/userland/mkjobs.py:138-150`, `refs/hakios-common/components/apple-id/first-generation/inject_store.c:35-51` | Yes | The phone fetched Apple's bag by hand with HTTP 200. `appstored` built a bag request but never resumed a task. |
| Apple ID sign-in patches | Source exists, opt-in, no local success log | `refs/hakios-common/components/apple-id/src/inject_kc.c:4-20`, `refs/hakios-common/components/apple-id/src/inject_kc.c:421-436`, `refs/hakios-common/darwin/userland/root26-darwin.sh:397-401` | Yes | Rewrites one keychain protection class and supplies a bag `absinthe-requests` value. Needs Secure Enclave backed tokens that this hardware cannot make. |
| FairPlay, anisette, Mescal, activation | Watchers and delegates exist, no full FairPlay | `refs/hakios-mtk/docs/x6826b-sep-appstore.md:42-50`, `refs/hakios-common/components/apple-id/first-generation/inject_fairplay.c:1-22`, `refs/hakios-common/components/apple-id/src/inject_mescal.m:1-17` | Partly | Mescal signing delegates to a host `sapd`. Anisette headers exist as hooks. FairPlay daemon exits and is not replaced. |
| Live IPA download and install from Apple | Not solved | `refs/hakios-mtk/README.md:37`, `refs/hakios-common/components/apple-id/src/inject_store.c:28-30`, `refs/akita/STATUS.md:62` | No | No local path from App Store purchase to `installd`. One USB IPA tool exists in the akita ref only. |
| Weather and WeatherKit | App present, no data fix | `refs/akita/STATUS.md:29`, `refs/akita/STATUS.md:62`, `refs/hakios-common/Documentation/darwin26-boot.md:211`, `refs/hakios-common/Documentation/darwin26-boot.md:255` | Partly | No WeatherKit patch in any ref. Current blockers are account and service, with location as a second dependency. |
| locationd Mescal signer patch | Portable library source, inert in this image | `refs/hakios-common/components/location/inject_locationd.c:1-24`, `refs/hakios-common/CHANGELOG.md:129-133`, `ios27/root26-darwin-tree/etc/inject.conf:12` | Yes | The patch short circuits one FairPlay wait. The current pruned image ships no `locationd` binary or job. |
| GPS and GNSS on MT6789 | Not ported on X6886 | `docs/DRIVER-MATRIX.md:14`, `refs/oosexgang-xnu27/kernel/overlay/bsd/dev/mtk_connsys.c:1-2`, `refs/akita/drivers/gnss/hakios.akita_gnss.c:1-2` | No | The userspace signer fix does not provide positions. Other ports carry device specific GNSS drivers. |
| Indonesian dev claim: tri LTE SIM and Metal UI | No local evidence | `docs/DRIVER-MATRIX.md:13-14`, `refs/heliodarwin/README.md:25`, `refs/heliodarwin/README.md:29`, `refs/akita/STATUS.md:17` | No | Claims live in external screenshots. Local X6886 boot stops at the XNU entry handoff. |

## App Store

| Layer | Verdict | Evidence file:line | Portable? | Notes |
|---|---|---|---|---|
| (a) UI visible | Yes | `refs/hakios-common/userland/userland/mkjobs.py:105-107`, `refs/hakios-common/userland/README.md:222` | Yes | The store opens and shows its own LOADING spinner. The tab bar appears after onboarding, then says Cannot Connect. |
| (b) Real Apple requests from the store | Not shown in the measured run; later hooks aim at it | `refs/hakios-common/userland/userland/mkjobs.py:138-150`, `refs/hakios-common/components/apple-id/first-generation/inject_store.c:35-51`, `refs/hakios-common/components/apple-id/src/inject_mescal.m:5-17` | Yes | A manual fetch got HTTP 200 from `bag.itunes.apple.com`. The store itself opened no socket. Mescal hooks target a 401 on `apiToken` and claim a signed 200, but no local run log confirms the result. |
| (c) Real download and install | No | `refs/hakios-mtk/README.md:37`, `refs/hakios-common/components/apple-id/src/inject_store.c:28-30`, `refs/akita/STATUS.md:62` | No | Source says no Apple ID is signed in and none can be, so the store must refuse to install. Akita lists installing applications as not yet. |

The plain answer: this is UI plus sideloading, not a working App Store.

### What the image actually enables

`ios27/root26-darwin-tree/etc/inject.conf:1-12` injects libraries into SpringBoard, Sileo, PosterBoard, Camera, and locationd. It has no line for `libhakios-store.dylib`, `libhakios-mescal.dylib`, `libhakios-kc.dylib`, or `libhakios-mad.dylib`. The dylibs exist in `ios27/root26-darwin-tree/lib/`, but the build enables them only with explicit flags:

- `refs/hakios-common/darwin/userland/root26-darwin.sh:369-385` adds the store and Mescal libraries under `STOREFRONT=1`.
- `refs/hakios-common/darwin/userland/root26-darwin.sh:397-401` adds the keychain library under `SIGNIN=1`.

The local iOS 27 image therefore runs none of the sign-in or store hooks. It does inject the locationd patch.

### Login, FairPlay, and signing code in the refs

`refs/hakios-common/components/apple-id/src/inject_kc.c:4-20` rewrites the keychain class `akpu` to `cku` for `akd`, because no Secure Enclave can wrap the passcode class. `inject_kc.c:421-436` supplies `absinthe-requests` so the media sign-in request avoids an Absinthe signature. Both are userspace libraries. Both stop short of making a purchase work.

`refs/hakios-mtk/docs/x6826b-sep-appstore.md:42-50` states that `fairplayd.H2` exists in Apple's IPSW but the image builder drops files over 10 MiB, and that FairPlay is neither implemented nor bypassed. `refs/hakios-common/components/apple-id/first-generation/inject_fairplay.c:1-22` only watches calls; it replaces nothing. `refs/hakios-common/userland/userland/mkjobs.py:19-25` records fairplayd exiting at once and wanting `com_apple_driver_FairPlayIOKitUserClient`, which the kernel lacks.

`refs/hakios-common/components/apple-id/src/inject_mescal.m:1-17` moves the Mescal signature to a host service at `http://192.168.1.18:8099/sign` (`inject_mescal.m:28`). That is portable userspace code plus a host dependency. It is not a local FairPlay implementation.

Anisette appears as a hook name in `ios27/root26-darwin-tree/lib/libhakios-store.dylib` and in `refs/hakios-common/components/apple-id/src/inject_store.c:4922-4932`, where device anisette headers are injected into the `apiToken` request. No anisette generator service exists in the local tree.

### Sideloading

Sileo with Procursus provides dpkg and apt packages, not App Store installs: `refs/hakios-common/userland/userland/build.sh:125-127` and `refs/hakios-common/userland/userland/mkjobs.py:58-60`. The only local IPA installer is `refs/akita/tools/usbmux-install.py:1-9`, which uses `com.apple.mobile.installation_proxy` and `installd` over USB. It belongs to the Pixel 8a ref, and `refs/akita/STATUS.md:62` still lists installing applications as not yet. No equivalent tool exists in `oosexgang-xnu27`, `hakios-mtk`, or the X6886 tree.

## Weather

| Item | Verdict | Evidence file:line | Portable? | Notes |
|---|---|---|---|---|
| Weather app and WeatherKit in the image | Present | `refs/hakios-common/darwin/tools/select-system.py:24`, `ios27/image-tree-prune/Applications/Weather.app/Weather`, `ios27/image-tree-prune/System/Library/Frameworks/WeatherKit.framework/XPCServices/com.apple.weatherkit.authservice.xpc/com.apple.weatherkit.authservice` | Yes | Stock Apple files. No local patch to them. |
| weatherd job | Shipped | `ios27/jobs26/com.apple.weatherd.plist`, `ios27/image-tree-prune/System/Library/LaunchDaemons/com.apple.weatherd.plist` | Yes | Binary plist, path evidence only. The job advertises `com.apple.weatherd.weatherkit`. |
| WeatherKit or weatherd patch | None found | Search of all refs for `WeatherKit`, `weatherd`, `weather fix`, `weather plist` | Nothing to port | No injected library, plist rewrite, or network shim for Weather in `ios27/root26-darwin-tree/etc/inject.conf:1-12`. |
| Observed result | Widget text or blank widget | `refs/hakios-common/Documentation/darwin26-boot.md:118`, `refs/hakios-common/Documentation/darwin26-boot.md:211`, `refs/hakios-common/Documentation/darwin26-boot.md:255`, `refs/akita/STATUS.md:29` | Yes | On Renoir hardware the widget showed populated content and later Location Access Needed. In QEMU and on akita it stayed white or had no data. |
| Cause recorded upstream | Signed in device and location | `refs/akita/STATUS.md:62` | Partly | Akita says Apple's weather service wants a signed in device. Precise location adds a hardware dependency. |

There is no weather fix to copy into `system27-prune.apfs`. The account side is the same userspace wall as the store. A GPS backed fix would also need an MT6789 location path, which `docs/DRIVER-MATRIX.md:14` marks NOT-PORTED for X6886.

## GPS and locationd

| Item | Verdict | Evidence file:line | Portable? | Notes |
|---|---|---|---|---|
| Mescal signer patch | Source exists | `refs/hakios-common/components/location/inject_locationd.c:1-24` | Yes | Replaces `-[CLMescalSigner initInSilo:]` with the nil return that locationd already uses when FairPlay setup fails. |
| Port provenance and verification | From OnePlus Nord 2 port, not verified locally | `refs/hakios-common/components/location/inject_locationd.c:19-23`, `refs/hakios-common/CHANGELOG.md:129-133` | Yes | The changelog says Settings Privacy opens and an app asking for location no longer stays black on the source port, and not yet seen on ours. |
| Build wiring | Unconditional entry in image builder | `refs/hakios-common/darwin/userland/root26-darwin.sh:366-368`, `ios27/root26-darwin-tree/etc/inject.conf:12` | Yes | The built dylib is `ios27/root26-darwin-tree/lib/liblocationd-silo.dylib`. |
| Current image effect | Inert | `refs/hakios-common/userland/userland/mkjobs.py:217`, `refs/hakios-common/darwin/tools/select-system.py:92`, `ios27/jobs26/` | No | `locationd` is in DENY, so no job plist is generated. The pruned system tree has no `usr/libexec/locationd`; the full `ios27/rootfs/usr/libexec/locationd` is 40 MB and the builder drops daemons over 10 MiB. |
| Sensor feed to locationd | Portable, different subsystem | `refs/hakios-common/CHANGELOG.md:170-177` | Yes | Feeds orientation and accelerometer events from `hw.sensors.*` to locationd through backboardd. Not a GNSS fix and not yet seen on their phone. |
| GNSS hardware driver | Not ported on X6886 | `docs/DRIVER-MATRIX.md:14`, `refs/oosexgang-xnu27/kernel/overlay/bsd/dev/mtk_connsys.c:1-2`, `refs/akita/drivers/gnss/hakios.akita_gnss.c:1-2` | No | MT_CONNSYS covers GPS channel 2 in the MT6768 ref. Akita uses a Samsung GNSS chip on SPI. Neither is X6886 code. |

So the locationd fix is portable userspace source. It cannot produce a GPS fix by itself, and it does not run in the current pruned image because the daemon binary and its job were left out.

## External screenshots and other ports

| Claim | Local evidence | Evidence file:line | Verdict |
|---|---|---|---|
| Tri operator LTE SIM working | None in this repo | `docs/DRIVER-MATRIX.md:14` marks cellular NOT-PORTED. `refs/heliodarwin/README.md:29` says cellular support is not established. `refs/akita/STATUS.md:58` lists mobile network as not yet. | External screenshots only. |
| Metal UI working | Yes on other hardware, not on X6886 | `refs/akita/STATUS.md:17`, `refs/akita/STATUS.md:36`, `refs/heliodarwin/README.md:25`, `docs/DRIVER-MATRIX.md:13` | Other ports have Metal. X6886 graphics is NOT-PORTED and the local boot log ends at the kernel handoff (`phase1-hw.log:35-36`). |
| App Store content working | No | `refs/hakios-mtk/README.md:37`, `refs/hakios-common/userland/README.md:47` | Both refs say content and sign-in do not work. |

No file in `docs/`, `plan/`, or the local refs mentions the Indonesian developer, Tri, Telkomsel, or an SIM based LTE result. Treat the claim as screenshot-only until a local run log or source file backs it.
