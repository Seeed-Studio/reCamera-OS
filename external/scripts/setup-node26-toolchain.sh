#!/bin/bash
# One-time build-host setup for Node.js >= 24 on reCamera (riscv64/C906).
#
# Node >= 24 requires GCC >= 12.2 (C++20) while the in-tree cvitek toolchain
# is Xuantie GCC 10.2, so the target nodejs package is compiled with the
# riscv-collab riscv64-linux-musl toolchain instead (see nodejs.mk,
# NODEJS_TC_ROOT). Additionally the GCC 16 runtime replaces the GCC 10 one in
# the cvitek sysroot, because:
#   - node links against libstdc++ from GCC 16 (GLIBCXX_3.4.3x symbols);
#   - GCC 16 emits fence.tso (Ztso) by default, which the C906 does not
#     implement -> SIGILL; the prebuilt libstdc++ is binary-patched back to
#     full fences (patch_fencetso.py from the nodejs package).
#
# Idempotent: safe to re-run. Run on the build host (or inside the builder
# container with the repo and toolchain mounted), e.g.:
#   external/scripts/setup-node26-toolchain.sh
#
# Environment:
#   TC_ROOT   riscv-collab toolchain dir (default ~/toolchains/riscv64-musl)
#             Layout: $TC_ROOT/bin/riscv64-unknown-linux-musl-gcc (from
#             https://github.com/riscv-collab/riscv-gnu-toolchain/releases,
#             riscv64-musl-ubuntu-24.04-gcc.tar.xz, unpacked with
#             --strip-components=1)
#   HOST_TOOLS  cvitek toolchain root (default <repo>/host-tools/gcc/riscv64-linux-musl-x86_64)
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)
TC_ROOT=${TC_ROOT:-$HOME/toolchains/riscv64-musl}
HOST_TOOLS=${HOST_TOOLS:-$REPO_ROOT/host-tools/gcc/riscv64-linux-musl-x86_64}
PATCH_PY=$REPO_ROOT/external/buildroot/package/nodejs/patch_fencetso.py
TC_TRIPLE=$TC_ROOT/riscv64-unknown-linux-musl
QEMU_LIBS=$(dirname "$TC_ROOT")/qemu-libs

fail() { echo "ERROR: $*" >&2; exit 1; }

[ -x "$TC_ROOT/bin/riscv64-unknown-linux-musl-gcc" ] || fail \
  "riscv-collab toolchain not found at $TC_ROOT
   download riscv64-musl-ubuntu-24.04-gcc.tar.xz from
   https://github.com/riscv-collab/riscv-gnu-toolchain/releases and unpack:
     mkdir -p $TC_ROOT && tar xJf <tarball> -C $TC_ROOT --strip-components=1"
[ -f "$PATCH_PY" ] || fail "patch_fencetso.py not found at $PATCH_PY"

echo "== toolchain: $($TC_ROOT/bin/riscv64-unknown-linux-musl-gcc -dumpversion)"

# 1. qemu-user lookup dir: musl's dynamic loader IS libc.so; expose it under
#    the PT_INTERP name so mksnapshot & co. can run under qemu during the
#    build (v8-qemu-wrapper-tc.in points here).
mkdir -p "$QEMU_LIBS/lib"
ln -sfn "$(cd "$TC_ROOT" && pwd)/sysroot/lib/libc.so" "$QEMU_LIBS/lib/ld-musl-riscv64.so.1"
echo "== qemu-libs: $QEMU_LIBS/lib/ld-musl-riscv64.so.1 -> sysroot/lib/libc.so"

# 2. Patch fence.tso out of the GCC 16 libstdc++ into a side file once.
STDCPP_NEW=$TC_ROOT/libstdc++.so.6-gcc16-notso
python3 "$PATCH_PY" "$TC_TRIPLE/lib/libstdc++.so.6.0.35" "$STDCPP_NEW"

# 3. Swap the runtime libs inside every cvitek sysroot ABI variant dir.
#    buildroot's external-toolchain package copies libstdc++ by the GCC-10
#    versioned NAME (libstdc++.so.6.0.28), so the new content must live
#    under that name; resolution happens via SONAME (libstdc++.so.6).
for D in sysroot/lib64/lp64d sysroot/lib64/lp64 sysroot/lib64v0p7_xthead/lp64d; do
  L=$HOST_TOOLS/$D
  [ -d "$L" ] || continue
  if grep -aq GLIBCXX_3.4.33 "$L/libstdc++.so.6.0.28" 2>/dev/null; then
    echo "== $D: already swapped, skipping"; continue
  fi
  for f in libgcc_s.so.1 libatomic.so.1.2.0; do
    [ -f "$L/$f" ] && cp -a "$L/$f" "$L/$f.gcc10-bak"
    cp "$TC_TRIPLE/lib/$f" "$L/$f"
  done
  cp "$STDCPP_NEW" "$L/libstdc++.so.6.0.28"
  chmod 0755 "$L/libstdc++.so.6.0.28"
  ln -sfn libstdc++.so.6.0.28 "$L/libstdc++.so.6"
  ln -sfn libatomic.so.1.2.0 "$L/libatomic.so.1"
  echo "== $D: swapped (originals kept as *.gcc10-bak)"
done

cat <<'EOF'

Done. Build-host notes:
  * The builder container must mount the toolchain parent dir at
    /opt/toolchains (nodejs.mk expects /opt/toolchains/riscv64-musl and
    /opt/toolchains/qemu-libs):
      -v <toolchains-parent>:/opt/toolchains
  * nodejs.mk NODEJS_TC_ROOT/NODEJS_QEMU_LIBS can override these paths.
  * sscma-node fetches its source from GitHub inside the build; on hosts
    where GitHub is unreachable configure a proxy for the build container:
      git config --system http.https://github.com.proxy http://<proxy>:7897
EOF
