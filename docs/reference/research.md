# Research

## HakiOS upstream

Default branch: `common`. Kernel: `xnu-12377.121.6`, Darwin 26 and 27. Docs at `Documentation/` in that branch.

Porting guide: `Documentation/porting.md`. It assumes Qualcomm, Mu-Silicium UEFI, and `fastboot boot`. X6886 meets none of those three conditions.

QEMU guide: `Documentation/qemu.md`. `make qemu` builds the open kernel and a tiny root with no Apple files. This is the first gate on the host.

Darwin 27 notes: `Documentation/darwin26-boot.md`. The system image recipe needs an IPSW from Apple and parts of the team's working tree that are not published.

Ports registry site: `hakios-developers.com` loads now. It lists zero ports. It has no MT6789 entry.

## MT6789 references found

Repositories found through GitHub repo search:

- `squeeeezy/HelioDarwin` - iOS 16 XNU bring-up on Infinix Note 12 2023 X676C (MT6789), based on `MakrSas/ios-topaz`.
- `lib-fluff-so/KLiOS716` - XNU patches, rootfs tooling and a macOS VM for TECNO Spark 30 Pro (MT6789), with an ARM64 shim.
- `woaphone/UniXNU-mt6789-common` - shared MT6789 platform support for UniXNU, work in progress.
- `maxexcloo/ios-jellystar` - experimental iOS 16 XNU on Unihertz Jelly Star (MT6789).

No public Advan Tab V8 HakiOS repository was found. No `hakios-mt6789` repository exists in the oosexgang organisation.

Clones are kept under `refs/` and ignored by git.

## Dump facts

`images/boot.img`: Android boot header version 4, 64 MiB, kernel `lz4_legacy` 18,885,902 bytes, no ramdisk. Real ramdisk lives in `vendor_boot.img`.

`images/lk.img`: 3,148,992 bytes. Fastboot command strings include `flash:`, `erase:`, `getvar:`, `continue`, `reboot`. No `boot:` command. String `INFO: fastboot disabled by payjoy!!!` present.

`images/dtbo.img`: 8 MiB. Panel and touch nodes must be extracted from it with dtc.
