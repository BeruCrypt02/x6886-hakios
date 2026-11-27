# Build configuration

This directory holds the device-facing configuration layer. The current X6886
port keeps its generated board header under `build/boards/x6886/`; the future
layout exposes `x6886_defconfig`, `boot-args.lst`, and the generated platform
header through `devices/x6886/configs/`.

No defconfig is authored yet: adding one is a separate change from this
repository reorganization.
