#!/usr/bin/env bash
# Build an already prepared Darwin 22 tree with caller-supplied KDK/CTF tools.
set -euo pipefail
: "${HOUJI_XNU:?path to prepared XNU source}"
: "${HOUJI_KDK:?path to extracted KDK payload}"
: "${HOUJI_FAKEROOT:?path containing usr/local/bin/ctfconvert, ctfmerge, ctfdump}"
: "${HOUJI_BUILD_ROOT:?new/local kernel output directory}"
port_root=$(cd "$(dirname "$0")/.." && pwd)
export DEVELOPER_DIR=${DEVELOPER_DIR:-$(xcode-select -p)}
export PATH="$DEVELOPER_DIR/Toolchains/XcodeDefault.xctoolchain/usr/bin:$DEVELOPER_DIR/usr/bin:$PATH"
export KERNEL_BUILD_DATE='Mon Aug 14 05:06:07 UTC 2023'
export KERNEL_BUILD_OBJROOT=1b191cb
mkdir -p "$HOUJI_BUILD_ROOT"
make -C "$HOUJI_XNU" install -j"${HOUJI_JOBS:-8}" \
  SDKROOT=macosx TARGET_CONFIGS='DEVELOPMENT ARM64 SWEET' BUILD_WERROR=0 BUILD_LTO=0 \
  SRCROOT="$HOUJI_XNU" OBJROOT="$HOUJI_BUILD_ROOT/xnu.obj" \
  SYMROOT="$HOUJI_BUILD_ROOT/xnu.sym" DSTROOT="$HOUJI_BUILD_ROOT/fakeroot" \
  FAKEROOT_DIR="$HOUJI_FAKEROOT" KDKROOT="$HOUJI_KDK" \
  TIGHTBEAMC=tightbeamc-not-supported RC_DARWIN_KERNEL_VERSION=22.6.0 \
  MEMORY_SIZE="$(sysctl -n hw.memsize)" SYSCTL_HW_PHYSICALCPU="$(sysctl -n hw.physicalcpu)" \
  SYSCTL_HW_LOGICALCPU="$(sysctl -n hw.logicalcpu)" KERNEL_BUILDS_IN_PARALLEL=1 \
  CTFCONVERT="$HOUJI_FAKEROOT/usr/local/bin/ctfconvert" \
  CTFMERGE="$HOUJI_FAKEROOT/usr/local/bin/ctfmerge" CTFDUMP="$HOUJI_FAKEROOT/usr/local/bin/ctfdump" \
  IIG="$port_root/tools/iig-darwin22.py"
