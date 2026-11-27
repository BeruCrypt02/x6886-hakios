# X6886 Bluetooth port manifest — transport only, backend reported MISSING

Branch `feat/x6886-real-port`, 100% offline: no adb, no fastboot, no build, no flash.
Written: `drivers/bluetooth/hakios.x6886_bt.c`, this manifest, and four raw firmware blobs in
`devices/x6886/prebuilt/firmware/`. No `.so`, `.ko` or config file was copied. No iscle source was read.

## What the allowed sources actually contain
- `refs/oosexgang-xnu-kernel-mtk-helio/kernel/overlay/bsd/dev/mtk_btsk.c` (325 lines) is a **real**
  Skywalk kernel-pipe provider: it registers `com.apple.nexus.hakios_bt`, owns the H4 TX/RX rings
  and calls `mtk_connsys_bt_send()` for host packets (`:123-164,168-215,250-325`). It is the source
  of `hakios.x6886_bt.c` verbatim except for names, prefixes and the boot gate.
- `mtk_bt_skel.c` (699 lines) is **only a skeleton**: its platform table is empty
  (`.name = NULL`, `:104-106`) and its own header says "has not run on a phone: there is no port for
  it to run on yet" (`:20`). It needs `power_on`, `function_on`, `send`, `receive` from the phone.
- Those four ops exist only in `mtk_connsys.c` (`:701,802,827,1931-1956`) — **outside the allowed
  source list** and blocked on the same unverified MT6366 VCN supplies as the Wi-Fi port
  (`wifi-driver.md` section 5). There is therefore **no complete real MTK Bluetooth driver** in the
  permitted osexgang sources. No fake `mtk_connsys_bt_send` / power-on / WMT stub was written:
  the symbol is left `extern` at `hakios.x6886_bt.c:29`.

## Boot safety / the final audit fix
- **Default OFF, transport only.** `x6886_bt_init()` returns 0 without touching anything unless
  `x6886_bt=1` is given; with the boot-arg on but no registered backend it returns a clean `ENXIO`.
- The missing `mtk_connsys_bt_send` extern was **removed**: TX now goes through the function
  pointer supplied by `x6886_bt_backend_register(fn)` from the CONNSYS owner
  (`hakios.x6886_bt.c:36,224,420`). If no backend is registered, TX packets are dropped with a log.
  There is **no fake stub** and no power-on / WMT / STP implementation in this file.
- `x6886_bt_ready_set(true)` only reports controller readiness; it does not satisfy the backend
  requirement. Bounded log counter (200 lines, `mtk_btsk.c:37`) and bounded 16 KB RX queue
  (`:25-27`). Clean error returns on every provider step; no panic, no guessed register.
- **Claim:** the H4/Skywalk transport is real; a working Bluetooth controller is NOT claimed
  because no real MTK backend exists in the permitted sources.
- All diagnostics go to printf and pstore `0x480af000` via `X6886_LOG` (`[X6886-bt]`).

## Raw firmware copied (data only)
Source: `/home/berucrypt00/otadump/vendor/firmware/` -> `devices/x6886/prebuilt/firmware/`.
| Blob | Bytes | SHA256 |
|---|---:|---|
| `soc2_2_ram_bt_1d_blr_1_hdr.bin` | 691052 | `dd47dd827739c6b281808ec3610ae57599aa539a39ef56a97e12f32569882aea` |
| `soc2_2_ram_bt_1e_1_hdr.bin` | 692436 | `5b2ec9f0caf35406cbe9376774ed7743f87359da9f33953dd1bb0716849e9b3a` |
| `soc2_2_ram_mcu_1d_blr_1_hdr.bin` | 198012 | `0c352f13609bf1f292ae32444b8f8213e7b166978acbed3a4cc52cbaf08d684a` |
| `soc2_2_ram_mcu_1e_1_hdr.bin` | 197924 | `ccbaea9ddce118e4d1013b5c6df10c1893207d4649075a481945663c82ef737a` |
`file(1)` = `data` for all four. The firmware is carried for the CONNSYS/BT owner; this transport
file does not download it.
