# X6886 boot image build

The boot image is generated locally and is not committed. The authoritative
offline release gate is:

```sh
python3 tools/verify-x6886-final.py
# RESULT: 13/13 checks passed
```

## Source layout

- Loader sources: `xnuloader/`
- Prebuilt loader blob: `devices/x6886/prebuilt/mtkloader.bin`
- Generated board header: `build/boards/x6886/hakios_platform.h`
- Payload builder: `tools/build-payload-x6886.py`
- Boot image packer: `tools/chainboot-x6886.py`
- Preferred repack: `tools/repack-magiskboot.sh`

## Rebuild sequence

```sh
. ./env.sh
bash tools/build-kernel-x6886.sh
bash tools/build-loader-elf-x6886.sh
python3 tools/build-payload-x6886.py build/payload-x6886.bin
sh tools/repack-magiskboot.sh \
  "$DUMP_ROOT/images/boot.img" \
  devices/x6886/prebuilt/mtkloader.bin \
  build/payload-x6886.bin \
  "$PWD/build/boot-HakiOS-X6886.img"
python3 tools/verify-x6886-final.py
```

The verifier reads the final image, the committed loader blob, the scratch
ELF under `/tmp/opencode/elf-final2/mtkloader.elf`, and the board/loader
sources. It never flashes anything. If it reports `6/13`, rebuild the ELF first.
