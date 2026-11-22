# X6886 device data

From the device spec and the unpacked dump.

- Model: Infinix Hot 60 Pro+, X6886.
- SoC: MediaTek MT6789, Helio G200, 2x A76 + 6x A55.
- RAM: 8 GB LPDDR4X.
- Storage: UFS 2.2.
- Display: 1080x2400 AMOLED, 120 Hz, 144 Hz mode listed.
- GPU: Mali-G57 MC2.
- Touch: `mtk-tpd` id 6, 2401x1081.
- Audio: `mt6789mt6366`.
- Charger: sc8950 and sc8548.
- Sensors: lsm6dso, qmc6308, mn79911.
- NFC: tms.
- Fingerprint: tran_keys, Goodix.
- Build: BP2A.250605.031.A3, XOS 16 / firmware 155SP15.
- Kernel in dump: 6.12.38-android16-gki.
- A/B device, dynamic super.

Firmware layout from the dump is in `super_partitions.json` and `mtk/partition_info.json`. Panel config lives in the extracted `odm/etc/vconfig/display/` tree.
