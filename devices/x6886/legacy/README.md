# X6886 HakiOS board package

Generated from the live Android FDT in `/home/berucrypt00/otadump/vendor_boot/dtb` with `tools/boards.py new`.

No hardware features are enabled. `soc.dtsi` holds GIC and PSCI values taken from the device tree. Display, panel, touch, UFS, GPU, PMIC and connectivity nodes are recorded in `docs/reference/hardware.md` and must be added one at a time with boot arguments.
