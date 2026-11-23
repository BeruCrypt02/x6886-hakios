# X6886 boot image build

`boot-x6886.img` is built locally (22 MB, not committed — see .gitignore).
Rebuild from the oosexgang HakiOS checkout after `tools/device.py add x6886`:

```sh
cd refs/oosexgang-xnu27
python3 tools/boards.py check          # 1 board, 2 SoCs, all consistent
python3 tools/boards.py gen            # HAKIOS_HAS_UFS=1, HAKIOS_SOC_UFS=0x11270000
PATH=/home/berucrypt00/x6886-hakios/tools/root/usr/lib/llvm-20/bin:$PATH bash loader/build.sh
python3 loader/mkpayload.py -o /tmp/payload-x6886.bin \
    kernel=../xnu-12377-hakios/BUILD/obj/mach.development.bcm2837 \
    ramdisk=../../hakios-x6886/ios27/root26-darwin.apfs
python3 loader/chainboot.py /home/berucrypt00/otadump/images/boot.img \
    build/loader/mtkloader.bin /tmp/boot-x6886.img /tmp/payload-x6886.bin
```

Verified: loader 36256 bytes with UFS strings; image header v4, page 4096,
22.8 MB (under the 32 MB boot partition limit).

`loader/ufs.h` and `chainboot-x6886.py` here are the working copies of the
patches applied to the checkout's `loader/` (UFS-aware loader + v4/lz4 boot
image support). The checkout itself is a reference (`refs/`), so edits there
are not part of this repo; these copies are.
