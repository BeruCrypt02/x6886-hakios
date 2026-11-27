#!/bin/bash
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
source "$ROOT/env.sh"
export XNU_SRC="$ROOT/refs/xnu-12377-hakios"
export XNUDEPS="$ROOT/refs/xnu-12377-hakios-deps"
export CC=xnu-clang CXX=xnu-clang++
export BUILD_WERROR=0 BUILD_LTO=0 DO_CTFMERGE=0 BUILD_DSYM=0
bash refs/hakios-common/kernel/build.sh -j2
