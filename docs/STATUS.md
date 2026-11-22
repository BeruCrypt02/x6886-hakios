# Status

Date: 2026-11-22.

Host: x86_64 Linux, 4 cores, 7.7 GiB RAM total, about 2 GiB available at setup time, 72 GiB free disk. QEMU default `QEMU_MEMORY=6G` is too large for this host. Use `QEMU_MEMORY=1536M` or `2G` and swap.

Dump: `/home/berucrypt00/otadump`, fully unpacked. It contains images and extracted trees. It does not contain GPT head/backup dumps, by-name map, getvar output, dmesg, or live DTB capture.

Phone: slot A runs ColorOS 17. Slot B parity not checked yet. Stock LK rejects `fastboot boot` with `unknown command`.

Upstream: HakiOS default branch is now `common`, XNU `xnu-12377.121.6` for Darwin 26/27. The old `main` and `mtk` branches remain.

Next gates:
1. QEMU Apple-free kernel and shell.
2. IPSW for Darwin userland.
3. X6886 board package from live DTB or dump DTBO.
4. Slot B parity and P7 check.
5. U-Boot for MT6789 and slot B boot only.
